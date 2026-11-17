/**
  ******************************************************************************
  * @file    usb_descriptors.h
  * @brief   USB descriptor helpers shared by the TinyUSB stack and the app.
  ******************************************************************************
  */

#ifndef __USB_DESCRIPTORS_H__
#define __USB_DESCRIPTORS_H__

#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Interface / string indices -------------------------------------------------*/

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_VENDOR,
  ITF_NUM_TOTAL
};

#define STRID_LANGID        0
#define STRID_MANUFACTURER  1
#define STRID_PRODUCT       2
#define STRID_SERIAL        3
#define STRID_CDC           4
#define STRID_VENDOR        5

/* Vendor request codes -------------------------------------------------------*/

/* wIndex value that selects the Microsoft OS 2.0 descriptor set. */
#define MS_OS_20_DESC_WINDEX  7U

/* Returns the STM32 96-bit unique device ID formatted as 24 hex characters. */
const char *board_usb_serial_number(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_DESCRIPTORS_H__ */
