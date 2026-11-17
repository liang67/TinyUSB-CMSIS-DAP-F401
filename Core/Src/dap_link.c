/**
  ******************************************************************************
  * @file    dap_link.c
  * @brief   DAPLink core: CMSIS-DAP v2 packet engine over TinyUSB, plus the
  *          CDC-ACM <-> USART1 bridge and the shared status LED.
  *
  * Data flow
  *   Vendor bulk OUT  -> DAP_ProcessCommand() -> Vendor bulk IN   (CMSIS-DAP v2)
  *   CDC bulk OUT     -> USART1 TX (PA9)                          (serial bridge)
  *   USART1 RX (PA10) -> CDC bulk IN                              (serial bridge)
  *
  * All processing runs in the main loop context (tud_task() dispatches the
  * class callbacks), so no re-entrancy protection is required beyond the
  * interrupts used for the USART transfer itself.
  ******************************************************************************
  */

#include <string.h>

#include "main.h"
#include "usart.h"
#include "usb_descriptors.h"
#include "DAP_config.h"
#include "DAP.h"
#include "tusb.h"

/* -------------------------------------------------------------------------- */
/* Small byte ring buffer                                                     */
/* -------------------------------------------------------------------------- */

#define UART_FIFO_SIZE  1024U            /* must be a power of two */

typedef struct {
  uint8_t           buf[UART_FIFO_SIZE];
  volatile uint32_t head;                /* written by producer */
  volatile uint32_t tail;                /* written by consumer */
} byte_fifo_t;

static byte_fifo_t _uart_to_usb;         /* USART1 RX -> CDC IN */
static byte_fifo_t _usb_to_uart;         /* CDC OUT  -> USART1 TX */

static inline void fifo_reset(byte_fifo_t *f) {
  f->head = 0U;
  f->tail = 0U;
}

static inline uint32_t fifo_count(const byte_fifo_t *f) {
  return (f->head - f->tail) & (UART_FIFO_SIZE - 1U);
}

static inline uint32_t fifo_space(const byte_fifo_t *f) {
  return (UART_FIFO_SIZE - 1U) - fifo_count(f);
}

static inline bool fifo_push(byte_fifo_t *f, uint8_t byte) {
  uint32_t next = (f->head + 1U) & (UART_FIFO_SIZE - 1U);
  if (next == f->tail) {
    return false;                        /* full */
  }
  f->buf[f->head] = byte;
  f->head = next;
  return true;
}

static inline bool fifo_pop(byte_fifo_t *f, uint8_t *byte) {
  if (f->head == f->tail) {
    return false;                        /* empty */
  }
  *byte = f->buf[f->tail];
  f->tail = (f->tail + 1U) & (UART_FIFO_SIZE - 1U);
  return true;
}

/* -------------------------------------------------------------------------- */
/* USART1 <-> USB buffering                                                   */
/* -------------------------------------------------------------------------- */

static uint8_t _uart_rx_chunk[64];
static uint8_t _uart_tx_chunk[64];
static volatile bool _uart_tx_busy;

/* -------------------------------------------------------------------------- */
/* CMSIS-DAP packet buffers                                                   */
/* -------------------------------------------------------------------------- */

#if (DAP_PACKET_SIZE > 64)
#error "This port assumes DAP_PACKET_SIZE <= 64 (single FS bulk packet)"
#endif

/* A single tud_task() drain can dispatch more than one vendor RX completion,
   so arriving requests are queued instead of overwriting one shared buffer.
   One slot is always kept free to distinguish full from empty. */
#define DAP_QUEUE_DEPTH   (DAP_PACKET_COUNT + 1U)

static uint8_t           _dap_req_queue[DAP_QUEUE_DEPTH][DAP_PACKET_SIZE];
static uint8_t           _dap_response[DAP_PACKET_SIZE];
static uint16_t          _dap_rsp_len;      /* response still to be written out */
static volatile uint32_t _dap_req_head;
static volatile uint32_t _dap_req_tail;

/* -------------------------------------------------------------------------- */
/* Status LED (PC13, active low, shared by every status indication)           */
/* -------------------------------------------------------------------------- */

typedef enum {
  LED_MODE_OFF = 0,
  LED_MODE_HEARTBEAT,      /* USB enumerated, no debugger attached      */
  LED_MODE_CONNECTED,      /* debugger attached, target halted/running  */
  LED_MODE_ERROR
} led_mode_t;

static volatile led_mode_t _led_mode = LED_MODE_OFF;
static volatile uint32_t   _led_tick;
static bool                _led_on;

static void led_write(bool on) {
  _led_on = on;
  /* PC13 is active low. */
  HAL_GPIO_WritePin(DAP_LED_PORT, DAP_LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void led_task(void) {
  uint32_t now = HAL_GetTick();

  switch (_led_mode) {
    case LED_MODE_OFF:
      if (_led_on) {
        led_write(false);
      }
      break;

    case LED_MODE_HEARTBEAT:
      /* Short flash every second. */
      if ((now - _led_tick) >= 1000U) {
        _led_tick = now;
        led_write(true);
      } else if (_led_on && (now - _led_tick) >= 30U) {
        led_write(false);
      }
      break;

    case LED_MODE_CONNECTED:
      /* Solid on: debugger attached. */
      if (!_led_on) {
        led_write(true);
      }
      break;

    case LED_MODE_ERROR:
      /* Fast blink. */
      if ((now - _led_tick) >= 100U) {
        _led_tick = now;
        led_write(!_led_on);
      }
      break;

    default:
      break;
  }
}

/* -------------------------------------------------------------------------- */
/* Hooks referenced from DAP_config.h                                         */
/* -------------------------------------------------------------------------- */

const char *DAP_SerialNumberString(void) {
  return board_usb_serial_number();
}

void DAP_LedConnected(uint32_t state) {
  if (state != 0U) {
    _led_mode = LED_MODE_CONNECTED;
  } else if (tud_mounted()) {
    _led_mode = LED_MODE_HEARTBEAT;
  } else {
    _led_mode = LED_MODE_OFF;
  }
}

void DAP_LedRunning(uint32_t state) {
  /* The board has a single LED shared by all indications: a running target is
     indistinguishable from a connected one, so just keep it lit. */
  (void)state;
  if (_led_mode == LED_MODE_OFF && tud_mounted()) {
    _led_mode = LED_MODE_HEARTBEAT;
  }
}

uint8_t DAP_ResetTargetPin(void) {
  /* nRESET (PB0) is open drain: drive low to assert, release to deassert. */
  PIN_nRESET_OUT(0U);
  HAL_Delay(20U);
  PIN_nRESET_OUT(1U);
  HAL_Delay(20U);
  return (0U);      /* 0 = target reset was executed */
}

/* -------------------------------------------------------------------------- */
/* USART1 bridging                                                            */
/* -------------------------------------------------------------------------- */

static void uart_start_rx(void) {
  (void)HAL_UARTEx_ReceiveToIdle_IT(&huart1, _uart_rx_chunk, sizeof(_uart_rx_chunk));
}

/* Called from interrupt context with the number of bytes received. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART1) {
    for (uint16_t i = 0U; i < Size; i++) {
      if (!fifo_push(&_uart_to_usb, _uart_rx_chunk[i])) {
        break;                            /* drop on overflow */
      }
    }
    uart_start_rx();                      /* re-arm */
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    _uart_tx_busy = false;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    _uart_tx_busy = false;
    uart_start_rx();
  }
}

/* Move data from the USART1 receive buffer into the CDC IN endpoint. */
static void bridge_uart_to_usb(void) {
  if (!tud_cdc_connected()) {
    return;
  }

  /* Wait for the CDC IN FIFO to have room before taking bytes out of the
     receive FIFO, so that nothing has to be put back (which would reorder). */
  uint32_t avail = tud_cdc_write_available();
  if (avail == 0U) {
    return;
  }

  uint8_t chunk[64];
  uint32_t n = 0U;
  uint32_t limit = (avail < sizeof(chunk)) ? avail : sizeof(chunk);
  while (n < limit && fifo_pop(&_uart_to_usb, &chunk[n])) {
    n++;
  }
  if (n == 0U) {
    return;
  }

  /* write_available() was respected above, so this accepts everything. */
  (void)tud_cdc_write(chunk, n);
  tud_cdc_write_flush();
}

/* Move data from the CDC OUT endpoint into the USART1 transmit buffer. */
static void bridge_usb_to_uart(void) {
  /* Only dequeue from the CDC endpoint as much as the local FIFO can hold,
     otherwise the excess would be silently dropped. */
  while (tud_cdc_available() > 0U && fifo_space(&_usb_to_uart) > 0U) {
    uint8_t chunk[64];
    uint32_t room = fifo_space(&_usb_to_uart);
    uint32_t want = (room < sizeof(chunk)) ? room : sizeof(chunk);

    uint32_t n = tud_cdc_read(chunk, want);
    if (n == 0U) {
      break;
    }
    for (uint32_t i = 0U; i < n; i++) {
      if (!fifo_push(&_usb_to_uart, chunk[i])) {
        break;                  /* cannot happen: room was checked above */
      }
    }
  }

  if (_uart_tx_busy) {
    return;
  }

  uint32_t n = 0U;
  while (n < sizeof(_uart_tx_chunk) && fifo_pop(&_usb_to_uart, &_uart_tx_chunk[n])) {
    n++;
  }
  if (n == 0U) {
    return;
  }

  _uart_tx_busy = true;
  if (HAL_UART_Transmit_IT(&huart1, _uart_tx_chunk, (uint16_t)n) != HAL_OK) {
    _uart_tx_busy = false;
  }
}

/* -------------------------------------------------------------------------- */
/* CMSIS-DAP v2 vendor endpoint                                               */
/* -------------------------------------------------------------------------- */

/* Process one queued request and send its response. */
static void dap_process_packet(void) {
  /* Flush a response that did not fit into the vendor IN FIFO last time.
     Commands must never be executed twice (they may move the target),
     so the pending payload is kept until there is room for it. */
  if (_dap_rsp_len > 0U) {
    if (tud_vendor_write_available() < _dap_rsp_len) {
      return;
    }
    (void)tud_vendor_write(_dap_response, (uint16_t)_dap_rsp_len);
    tud_vendor_write_flush();
    _dap_rsp_len = 0U;
  }

  if (_dap_req_tail == _dap_req_head) {
    return;                              /* queue empty */
  }

  const uint8_t *request = _dap_req_queue[_dap_req_tail];
  uint32_t result = DAP_ProcessCommand(request, _dap_response);

  /* Release the slot only after the command has been fully executed. */
  _dap_req_tail = (_dap_req_tail + 1U) % DAP_QUEUE_DEPTH;

  uint32_t rsp_len = result & 0xFFFFU;
  if (rsp_len > DAP_PACKET_SIZE) {
    rsp_len = DAP_PACKET_SIZE;
  }
  _dap_rsp_len = rsp_len;

  if (_dap_rsp_len > 0U && tud_vendor_write_available() >= _dap_rsp_len) {
    (void)tud_vendor_write(_dap_response, (uint16_t)_dap_rsp_len);
    tud_vendor_write_flush();
    _dap_rsp_len = 0U;
  }
}

/* CMSIS-DAP v2 callback: one USB transfer carries exactly one DAP packet. */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize) {
  (void)itf;

  if (bufsize == 0U || bufsize > DAP_PACKET_SIZE) {
    return;
  }

  uint32_t next = (_dap_req_head + 1U) % DAP_QUEUE_DEPTH;
  if (next == _dap_req_tail) {
    return;                              /* queue full: drop */
  }

  memcpy(_dap_req_queue[_dap_req_head], buffer, bufsize);
  /* DAP_ProcessCommand() may look past the actual payload for padding. */
  memset(_dap_req_queue[_dap_req_head] + bufsize, 0, DAP_PACKET_SIZE - bufsize);
  _dap_req_head = next;
}

/* Vendor interface control requests that are not part of MS OS 2.0. */
bool dap_vendor_control_xfer(uint8_t rhport, uint8_t stage,
                             tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;     /* STALL: unsupported vendor control request */
}

/* -------------------------------------------------------------------------- */
/* CDC-ACM callbacks                                                          */
/* -------------------------------------------------------------------------- */

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
  (void)itf;

  if (coding->bit_rate == 0U) {
    return;
  }

  huart1.Init.BaudRate = coding->bit_rate;
  switch (coding->stop_bits) {
    case 2:  huart1.Init.StopBits = UART_STOPBITS_2;   break;
    default: huart1.Init.StopBits = UART_STOPBITS_1;   break;
  }
  switch (coding->parity) {
    case 1:  huart1.Init.Parity = UART_PARITY_ODD;     break;
    case 2:  huart1.Init.Parity = UART_PARITY_EVEN;    break;
    default: huart1.Init.Parity = UART_PARITY_NONE;    break;
  }

  /* On STM32 the parity bit is included in the frame length, so 8 data bits
     plus parity needs a 9-bit word length.  9 data bits are not representable
     without dropping the parity bit, so fall back to 8. */
  if (huart1.Init.Parity == UART_PARITY_NONE) {
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
  } else {
    huart1.Init.WordLength = UART_WORDLENGTH_9B;
  }

  (void)HAL_UART_DeInit(&huart1);
  (void)HAL_UART_Init(&huart1);
  _uart_tx_busy = false;
  uart_start_rx();
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
  (void)itf;
  (void)rts;
  (void)dtr;
}

/* -------------------------------------------------------------------------- */
/* Public interface                                                           */
/* -------------------------------------------------------------------------- */

/* Required by TinyUSB (no weak default exists in tusb.c). */
uint32_t tusb_time_millis_api(void) {
  return HAL_GetTick();
}

void dap_link_init(void) {
  fifo_reset(&_uart_to_usb);
  fifo_reset(&_usb_to_uart);
  _uart_tx_busy = false;

  _dap_req_head = 0U;
  _dap_req_tail = 0U;
  _dap_rsp_len = 0U;

  /* Initialise the CMSIS-DAP core (also enables the DWT cycle counter). */
  DAP_Setup();

  /* Start the USB device stack. */
  (void)tusb_init();

  /* Start listening on the target UART. */
  uart_start_rx();

  _led_mode = LED_MODE_OFF;
  _led_tick = HAL_GetTick();
  led_write(false);
}

void dap_link_task(void) {
  /* Drive the USB stack. */
  tud_task();

  /* USB events may have changed the connection state. */
  if (tud_mounted()) {
    if (_led_mode == LED_MODE_OFF) {
      _led_mode = LED_MODE_HEARTBEAT;
      _led_tick = HAL_GetTick();
    }
  } else {
    _led_mode = LED_MODE_OFF;
  }

  /* CMSIS-DAP command processing. */
  dap_process_packet();

  /* Serial bridge, both directions. */
  bridge_uart_to_usb();
  bridge_usb_to_uart();

  /* Shared status indication. */
  led_task();
}
