/**
  ******************************************************************************
  * @file    DAP_config.h
  * @brief   CMSIS-DAP board (pin / timing / capability) configuration.
  *
  * Target hardware : WeAct MiniSTM32F4x1 V3.1 (STM32F401CCU6, 25 MHz HSE)
  * Firmware        : CMSIS-DAP v2 over TinyUSB, plus CDC-ACM <-> USART1 bridge
  *
  * Pin usage
  *   PB13  SWCLK / TCK          (push-pull output, driven by firmware)
  *   PB14  SWDIO / TMS          (push-pull output <-> input with pull-up)
  *   PB0   nRESET               (open-drain output, low = assert)
  *   PC13  status LED           (active low, shared by all status reports)
  *   PA9   USART1_TX   AF7
  *   PA10  USART1_RX   AF7
  *   PA11  USB_OTG_FS_DM AF10
  *   PA12  USB_OTG_FS_DP AF10
  *
  * This project only implements SWD (no JTAG pins are wired on the board),
  * therefore DAP_JTAG is 0.  DAP_SWJ_Pins() nevertheless references the
  * TDI / TDO / nTRST hooks unconditionally, so benign stubs are provided.
  ******************************************************************************
  */

#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#include <string.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Configuration of the DAP                                                   */
/* -------------------------------------------------------------------------- */

/** Processor clock frequency [Hz] used by the bit-bang delay calibration. */
#define CPU_CLOCK               (84000000U)

/** Number of clock cycles the GPIO write macro needs (BSRR write = 2 cycles). */
#define IO_PORT_WRITE_CYCLES    (2U)

/** Debug Unit: SWD is supported, JTAG is not (no TDI/TDO/nTRST on this board). */
#define DAP_SWD                 (1)
#define DAP_JTAG                (0)
#define DAP_JTAG_DEV_CNT        (0U)

/** Default debug port after DAP_Connect(DAP_PORT_AUTODETECT): SWD. */
#define DAP_DEFAULT_PORT        (1U)

/** Default SWJ clock: 1 MHz. */
#define DAP_DEFAULT_SWJ_CLOCK   (1000000U)

/** Maximum DAP packet size in bytes (one USB FS bulk packet). */
#define DAP_PACKET_SIZE         (64U)

/** Number of DAP packets that can be queued. */
#define DAP_PACKET_COUNT        (2U)

/** SWO is not supported by this firmware build. */
#define SWO_UART                (0)
#define SWO_UART_DRIVER         (0)
#define SWO_UART_MAX_BAUDRATE   (0U)
#define SWO_MANCHESTER          (0)
#define SWO_BUFFER_SIZE         (0U)
#define SWO_STREAM              (0)

/** DAP UART commands are not implemented; the CDC bridge is a plain pipe. */
#define DAP_UART                (0)
#define DAP_UART_DRIVER         (0)
#define DAP_UART_RX_BUFFER_SIZE (0U)
#define DAP_UART_TX_BUFFER_SIZE (0U)
#define DAP_UART_USB_COM_PORT   (0)

/** Target information is reported by the debugger, not fixed here. */
#define TARGET_FIXED            (0)

/* -------------------------------------------------------------------------- */
/* Timestamp source (DWT cycle counter, enabled by DAP_SETUP())               */
/* -------------------------------------------------------------------------- */

/** Frequency of the timestamp counter [Hz]. */
#define TIMESTAMP_CLOCK         (CPU_CLOCK)

/** Returns the current timestamp, in TIMESTAMP_CLOCK ticks. */
#define TIMESTAMP_GET()         (DWT->CYCCNT)

/* -------------------------------------------------------------------------- */
/* Firmware identification strings                                            */
/* -------------------------------------------------------------------------- */

extern const char *DAP_SerialNumberString(void);

#define DAP_VENDOR_NAME         "WeAct"
#define DAP_PRODUCT_NAME        "WeAct STM32F401CC DAPLink"
#define DAP_PRODUCT_FW_VERSION  "1.0.0"

/* -------------------------------------------------------------------------- */
/* Raw GPIO helpers (direct register access: the bit-bang loops are timing     */
/* critical, HAL_GPIO_WritePin() would be far too slow)                       */
/* -------------------------------------------------------------------------- */

#define DAP_SWCLK_PORT          GPIOB
#define DAP_SWCLK_PIN           GPIO_PIN_13
#define DAP_SWCLK_PIN_POS       (13U)

#define DAP_SWDIO_PORT          GPIOB
#define DAP_SWDIO_PIN           GPIO_PIN_14
#define DAP_SWDIO_PIN_POS       (14U)

#define DAP_NRESET_PORT         GPIOB
#define DAP_NRESET_PIN          GPIO_PIN_0
#define DAP_NRESET_PIN_POS      (0U)

#define DAP_LED_PORT            GPIOC
#define DAP_LED_PIN             GPIO_PIN_13
#define DAP_LED_PIN_POS         (13U)

/* -------------------------------------------------------------------------- */
/* Hooks called from the CMSIS-DAP core                                       */
/* -------------------------------------------------------------------------- */

extern void    DAP_LedConnected(uint32_t state);
extern void    DAP_LedRunning(uint32_t state);
/* Note: cannot be called DAP_ResetTarget() - DAP.c already defines a static
   function with that name for the ID_DAP_ResetTarget command handler. */
extern uint8_t DAP_ResetTargetPin(void);

/* -------------------------------------------------------------------------- */
/* DAP information strings                                                    */
/* -------------------------------------------------------------------------- */

/** Get Vendor Name string. */
__STATIC_INLINE uint8_t DAP_GetVendorString (char *str) {
  memcpy(str, DAP_VENDOR_NAME, sizeof(DAP_VENDOR_NAME));
  return ((uint8_t)sizeof(DAP_VENDOR_NAME));
}

/** Get Product Name string. */
__STATIC_INLINE uint8_t DAP_GetProductString (char *str) {
  memcpy(str, DAP_PRODUCT_NAME, sizeof(DAP_PRODUCT_NAME));
  return ((uint8_t)sizeof(DAP_PRODUCT_NAME));
}

/** Get Serial Number string (24 hex characters of the STM32 unique ID). */
__STATIC_INLINE uint8_t DAP_GetSerNumString (char *str) {
  const char *p = DAP_SerialNumberString();
  uint8_t len = (uint8_t)(strlen(p) + 1U);
  memcpy(str, p, len);
  return (len);
}

/** Get Target Device Vendor string (not fixed -> reported by the debugger). */
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString (char *str) {
  (void)str;
  return (0U);
}

/** Get Target Device Name string (not fixed -> reported by the debugger). */
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString (char *str) {
  (void)str;
  return (0U);
}

/** Get Target Board Vendor string (not fixed -> reported by the debugger). */
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString (char *str) {
  (void)str;
  return (0U);
}

/** Get Target Board Name string (not fixed -> reported by the debugger). */
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString (char *str) {
  (void)str;
  return (0U);
}

/** Get Product Firmware Version string. */
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString (char *str) {
  memcpy(str, DAP_PRODUCT_FW_VERSION, sizeof(DAP_PRODUCT_FW_VERSION));
  return ((uint8_t)sizeof(DAP_PRODUCT_FW_VERSION));
}

/* -------------------------------------------------------------------------- */
/* Port setup                                                                 */
/* -------------------------------------------------------------------------- */

/** Switch SWDIO (PB14) to push-pull output. */
__STATIC_FORCEINLINE void DAP_SWDIO_OutputMode (void) {
  uint32_t moder = DAP_SWDIO_PORT->MODER;
  moder &= ~(3UL << (DAP_SWDIO_PIN_POS * 2U));
  moder |=  (1UL << (DAP_SWDIO_PIN_POS * 2U));
  DAP_SWDIO_PORT->MODER = moder;
  DAP_SWDIO_PORT->OTYPER &= ~(1UL << DAP_SWDIO_PIN_POS);   /* push-pull  */
  DAP_SWDIO_PORT->OSPEEDR |= (3UL << (DAP_SWDIO_PIN_POS * 2U));
  DAP_SWDIO_PORT->PUPDR &= ~(3UL << (DAP_SWDIO_PIN_POS * 2U)); /* no pull */
}

/** Switch SWDIO (PB14) to input with pull-up. */
__STATIC_FORCEINLINE void DAP_SWDIO_InputMode (void) {
  uint32_t moder = DAP_SWDIO_PORT->MODER;
  moder &= ~(3UL << (DAP_SWDIO_PIN_POS * 2U));             /* input      */
  DAP_SWDIO_PORT->MODER = moder;
  DAP_SWDIO_PORT->PUPDR &= ~(3UL << (DAP_SWDIO_PIN_POS * 2U));
  DAP_SWDIO_PORT->PUPDR |=  (1UL << (DAP_SWDIO_PIN_POS * 2U)); /* pull-up */
}

/** Configure JTAG port (not available on this board). */
__STATIC_INLINE void PORT_JTAG_SETUP (void) {
  ;
}

/** Configure SWD port: SWDIO output high, SWCLK low, nRESET released. */
__STATIC_INLINE void PORT_SWD_SETUP (void) {
  DAP_SWDIO_PORT->BSRR = DAP_SWDIO_PIN;                    /* SWDIO high */
  DAP_SWCLK_PORT->BSRR = (uint32_t)DAP_SWCLK_PIN << 16U;   /* SWCLK low  */
  DAP_SWDIO_OutputMode();
  DAP_NRESET_PORT->BSRR = DAP_NRESET_PIN;                  /* open-drain: release */
}

/** Release the debug port: both lines become inputs, nRESET released. */
__STATIC_INLINE void PORT_OFF (void) {
  DAP_NRESET_PORT->BSRR = DAP_NRESET_PIN;
  DAP_SWDIO_InputMode();
}

/* -------------------------------------------------------------------------- */
/* SWCLK / TCK                                                                */
/* -------------------------------------------------------------------------- */

/** SWCLK (PB13) input. */
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN  (void) {
  return ((DAP_SWCLK_PORT->IDR & DAP_SWCLK_PIN) ? 1U : 0U);
}

/** SWCLK (PB13) = 1. */
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET (void) {
  DAP_SWCLK_PORT->BSRR = DAP_SWCLK_PIN;
}

/** SWCLK (PB13) = 0. */
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR (void) {
  DAP_SWCLK_PORT->BSRR = (uint32_t)DAP_SWCLK_PIN << 16U;
}

/* -------------------------------------------------------------------------- */
/* SWDIO / TMS                                                                */
/* -------------------------------------------------------------------------- */

/** SWDIO (PB14) input. */
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN (void) {
  return ((DAP_SWDIO_PORT->IDR & DAP_SWDIO_PIN) ? 1U : 0U);
}

/** SWDIO (PB14) = 1. */
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET (void) {
  DAP_SWDIO_PORT->BSRR = DAP_SWDIO_PIN;
}

/** SWDIO (PB14) = 0. */
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR (void) {
  DAP_SWDIO_PORT->BSRR = (uint32_t)DAP_SWDIO_PIN << 16U;
}

/** SWDIO (PB14) input. */
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN (void) {
  return ((DAP_SWDIO_PORT->IDR & DAP_SWDIO_PIN) ? 1U : 0U);
}

/** SWDIO (PB14) = bit. */
__STATIC_FORCEINLINE void PIN_SWDIO_OUT (uint32_t bit) {
  if ((bit & 1U) != 0U) {
    DAP_SWDIO_PORT->BSRR = DAP_SWDIO_PIN;
  } else {
    DAP_SWDIO_PORT->BSRR = (uint32_t)DAP_SWDIO_PIN << 16U;
  }
}

/** Switch SWDIO (PB14) to output. */
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE (void) {
  DAP_SWDIO_OutputMode();
}

/** Switch SWDIO (PB14) to input. */
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE (void) {
  DAP_SWDIO_InputMode();
}

/* -------------------------------------------------------------------------- */
/* TDI / TDO / nTRST - not connected on this board, harmless stubs            */
/* -------------------------------------------------------------------------- */

__STATIC_FORCEINLINE uint32_t PIN_TDI_IN (void) {
  return (0U);
}

__STATIC_FORCEINLINE void PIN_TDI_OUT (uint32_t bit) {
  (void)bit;
}

__STATIC_FORCEINLINE uint32_t PIN_TDO_IN (void) {
  return (0U);
}

__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN (void) {
  return (1U);            /* nTRST is never asserted */
}

__STATIC_FORCEINLINE void PIN_nTRST_OUT (uint32_t bit) {
  (void)bit;
}

/* -------------------------------------------------------------------------- */
/* nRESET (PB0, open-drain: 0 = drive low / assert, 1 = release)              */
/* -------------------------------------------------------------------------- */

/** nRESET (PB0) input. */
__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN (void) {
  return ((DAP_NRESET_PORT->IDR & DAP_NRESET_PIN) ? 1U : 0U);
}

/** nRESET (PB0) = bit (open drain, so 1 only releases the line). */
__STATIC_FORCEINLINE void PIN_nRESET_OUT (uint32_t bit) {
  if ((bit & 1U) != 0U) {
    DAP_NRESET_PORT->BSRR = DAP_NRESET_PIN;
  } else {
    DAP_NRESET_PORT->BSRR = (uint32_t)DAP_NRESET_PIN << 16U;
  }
}

/* -------------------------------------------------------------------------- */
/* Status LED (PC13, active low, shared by every status indication)           */
/* -------------------------------------------------------------------------- */

/** Connected LED (shared with the running LED). */
__STATIC_INLINE void LED_CONNECTED_OUT (uint32_t bit) {
  DAP_LedConnected(bit);
}

/** Running LED (shared with the connected LED). */
__STATIC_INLINE void LED_RUNNING_OUT (uint32_t bit) {
  DAP_LedRunning(bit);
}

/* -------------------------------------------------------------------------- */
/* Timestamp (DWT cycle counter)                                              */
/* -------------------------------------------------------------------------- */

/** Board / core initialisation performed once by DAP_Setup(). */
__STATIC_INLINE void DAP_SETUP (void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/** Reset the target: assert nRESET for 20 ms, then release. */
__STATIC_INLINE uint8_t RESET_TARGET (void) {
  return (DAP_ResetTargetPin());
}

#ifdef __cplusplus
}
#endif

#endif /* __DAP_CONFIG_H__ */
