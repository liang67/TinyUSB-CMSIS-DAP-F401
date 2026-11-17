/**
  ******************************************************************************
  * @file    tusb_config.h
  * @brief   TinyUSB configuration for the DAPLink (CMSIS-DAP v2 + CDC ACM).
  ******************************************************************************
  */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------*/
/* Board / MCU                                                       */
/*------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined (OPT_MCU_STM32F4)
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

/* STM32F401CCU6 has USB OTG FS only, on rhport 0. */
#define BOARD_TUD_RHPORT            0
#define BOARD_TUD_MAX_SPEED         OPT_MODE_DEFAULT_SPEED

/* Enabled device stack, full speed */
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           BOARD_TUD_MAX_SPEED

/* Legacy name used by some TinyUSB headers. */
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* Memory alignment for USB buffers */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

/*------------------------------------------------------------------*/
/* Device classes                                                    */
/*------------------------------------------------------------------*/
#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              1

/* CDC-ACM ring buffers.  Generous sizes so that a 115200 baud serial link
   never overruns between two tud_task() passes. */
#define CFG_TUD_CDC_RX_BUFSIZE      512
#define CFG_TUD_CDC_TX_BUFSIZE      512
#define CFG_TUD_CDC_EP_BUFSIZE      64

/* CMSIS-DAP v2 bulk endpoint.  DAP_PACKET_SIZE is 64; the FIFOs must hold at
   least two packets so that a queued response is not truncated. */
#define CFG_TUD_VENDOR_RX_BUFSIZE   512
#define CFG_TUD_VENDOR_TX_BUFSIZE   512
#define CFG_TUD_VENDOR_EPSIZE       64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
