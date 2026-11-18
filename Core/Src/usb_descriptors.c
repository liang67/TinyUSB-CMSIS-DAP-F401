/**
  ******************************************************************************
  * @file    usb_descriptors.c
  * @brief   USB descriptors: CMSIS-DAP v2 (vendor bulk) + CDC ACM.
  *
  * Composite device
  *   ITF 0/1 : CDC-ACM (bridge to USART1, PA9/PA10)
  *   ITF 2   : CMSIS-DAP v2 vendor specific bulk interface
  *
  * A BOS descriptor carries a Microsoft OS 2.0 platform capability so that
  * Windows binds WinUSB to the vendor interface automatically (no .inf
  * needed).  This is what makes the device usable with OpenOCD / pyOCD /
  * DAPLink tools on a fresh Windows host.
  ******************************************************************************
  */

#include "usb_descriptors.h"
#include "DAP_config.h"

/*------------------------------------------------------------------*/
/* Device descriptor                                                */
/*------------------------------------------------------------------*/
tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0210,   /* >= 2.1 so that BOS / MS OS 2.0 is valid */

  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = 0x0D28,   /* mbed / DAPLink */
  .idProduct          = 0x0204,   /* CMSIS-DAP v2  */

  /* Windows caches the "this device has no OS descriptor" answer per
     VID/PID/bcdDevice under HKLM\...\usbflags, and never asks again.  1.0.0
     got such a negative entry cached on this machine, so the MS OS 2.0
     descriptor was ignored and the vendor interface stayed driverless
     (Code 28).  Bumping the device release makes Windows re-enumerate it
     as a new device and fetch the descriptors again. */
  .bcdDevice          = 0x0104,

  .iManufacturer      = STRID_MANUFACTURER,
  .iProduct           = STRID_PRODUCT,
  .iSerialNumber      = STRID_SERIAL,

  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

/*------------------------------------------------------------------*/
/* Configuration descriptor                                         */
/*------------------------------------------------------------------*/

/* Endpoint allocation (STM32F401 FS has EP0..EP3) */
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_VENDOR_OUT  0x03
#define EPNUM_VENDOR_IN   0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN)

uint8_t const desc_configuration[] = {
  /* Config number, interface count, string index, total length,
     attribute (bus powered), power in mA */
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  /* CDC-ACM: interface, string index, EP notif addr/size, EP out, EP in, size */
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8,
                     EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

  /* CMSIS-DAP v2 vendor interface: interface, string index, EP out, EP in, size */
  TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, STRID_VENDOR, EPNUM_VENDOR_OUT,
                        EPNUM_VENDOR_IN, 64)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

/*------------------------------------------------------------------*/
/* BOS descriptor (Microsoft OS 2.0 platform capability)             */
/*------------------------------------------------------------------*/

/* bRequest used by Windows to fetch the MS OS 2.0 descriptor set.  It must
   match the bMS_VendorCode advertised in the BOS descriptor below. */
#define VENDOR_REQUEST_MICROSOFT  0x01

#define BOS_TOTAL_LEN  (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

/* Length of the whole MS OS 2.0 descriptor set */
#define MS_OS_20_DESC_LEN       (10U + 8U + 8U + 20U + 132U)   /* 0xB2 */

/* Configuration subset = its header + the function subset */
#define MS_OS_20_CONFIG_SUBSET_LEN  (8U + 8U + 20U + 132U)     /* 0xA8 */

/* Function subset = its header + the features */
#define MS_OS_20_FUNC_SUBSET_LEN    (8U + 20U + 132U)          /* 0xA0 */

uint8_t const desc_bos[] = {
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
  TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

uint8_t const *tud_descriptor_bos_cb(void) {
  return desc_bos;
}

/*
 * Microsoft OS 2.0 descriptor set.
 *
 *   10 bytes  Set header
 *    8 bytes  Configuration subset header
 *    8 bytes  Function subset header (first interface = ITF_NUM_VENDOR)
 *   20 bytes  Compatible ID feature -> "WINUSB"
 */
uint8_t const desc_ms_os_20[] = {
  /* Set header */
  10, 0x00,                    /* wLength                                         */
  0x00, 0x00,                  /* wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR */
  0x00, 0x00, 0x03, 0x06,      /* dwWindowsVersion = 0x06030000 (Win 8.1+)        */
  (uint8_t)(MS_OS_20_DESC_LEN & 0xFF), (uint8_t)(MS_OS_20_DESC_LEN >> 8),

  /* Configuration subset header */
  8, 0x00,                     /* wLength                                          */
  0x01, 0x00,                  /* wDescriptorType = SUBSET_HEADER_CONFIGURATION     */
  0x00,                        /* bConfigurationValue: 0 selects the first (only)
                                  configuration. Writing 1 here made Windows
                                  fetch the set and then discard the compatible
                                  ID, leaving the vendor interface at Code 28. */
  0x00,                        /* bReserved                                        */
  (uint8_t)(MS_OS_20_CONFIG_SUBSET_LEN & 0xFF),
  (uint8_t)(MS_OS_20_CONFIG_SUBSET_LEN >> 8),   /* wTotalLength of this subset      */

  /* Function subset header */
  8, 0x00,                     /* wLength                                          */
  0x02, 0x00,                  /* wDescriptorType = SUBSET_HEADER_FUNCTION          */
  ITF_NUM_VENDOR,              /* bFirstInterface                                  */
  0x00,                        /* bReserved                                        */
  (uint8_t)(MS_OS_20_FUNC_SUBSET_LEN & 0xFF),
  (uint8_t)(MS_OS_20_FUNC_SUBSET_LEN >> 8),     /* wSubsetLength of this subset     */

  /* Compatible ID feature */
  20, 0x00,                    /* wLength                                          */
  0x03, 0x00,                  /* wDescriptorType = FEATURE_COMPATBLE_ID            */
  /* 'W','I','N','U','S','B',0,0 */
  'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
  /* Sub-compatible ID, unused */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  /* Registry property feature: DeviceInterfaceGUIDs.
     Without this Windows binds the generic WinUSB driver but exposes no
     device interface, which is what kept the vendor interface at Code 28.
     The payload is UTF-16LE, hence the interleaved 0x00 bytes. */
  132, 0x00,                   /* wLength                                          */
  0x04, 0x00,                  /* wDescriptorType = FEATURE_REG_PROPERTY            */
  0x07, 0x00,                  /* wPropertyDataType = REG_MULTI_SZ                  */
  42, 0x00,                    /* wPropertyNameLength (21 UTF-16 chars incl NUL)    */
  'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
  'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
  'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
  'D', 0x00, 's', 0x00, 0x00, 0x00,
  80, 0x00,                    /* wPropertyDataLength                               */
  /* "{CDB3B5AD-293B-4663-AA36-1AAE46463776}" - the GUID CMSIS-DAP probes use */
  '{', 0x00, 'C', 0x00, 'D', 0x00, 'B', 0x00, '3', 0x00, 'B', 0x00,
  '5', 0x00, 'A', 0x00, 'D', 0x00, '-', 0x00,
  '2', 0x00, '9', 0x00, '3', 0x00, 'B', 0x00, '-', 0x00,
  '4', 0x00, '6', 0x00, '6', 0x00, '3', 0x00, '-', 0x00,
  'A', 0x00, 'A', 0x00, '3', 0x00, '6', 0x00, '-', 0x00,
  '1', 0x00, 'A', 0x00, 'A', 0x00, 'E', 0x00, '4', 0x00, '6', 0x00,
  '4', 0x00, '6', 0x00, '3', 0x00, '7', 0x00, '7', 0x00, '6', 0x00,
  '}', 0x00, 0x00, 0x00, 0x00, 0x00
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect MS OS 2.0 descriptor length");

/*------------------------------------------------------------------*/
/* String descriptors                                               */
/*------------------------------------------------------------------*/

char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },        /* 0: LANGID = 0x0409 (en-US)  */
  DAP_VENDOR_NAME,                     /* 1: Manufacturer             */
  DAP_PRODUCT_NAME,                    /* 2: Product                  */
  NULL,                                /* 3: Serial (filled at runtime) */
  "Liang67 CDC-ACM",                   /* 4: CDC interface            */
  "Liang67 CMSIS-DAP v2",              /* 5: Vendor interface         */
};

static uint16_t _desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t chr_count;

  if (index == STRID_LANGID) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
      return NULL;
    }

    /* Serial number is generated from the STM32 unique device ID */
    const char *str = (index == STRID_SERIAL) ? board_usb_serial_number()
                                              : string_desc_arr[index];
    if (str == NULL) {
      return NULL;
    }

    chr_count = (uint8_t)strlen(str);
    if (chr_count > 32) {
      chr_count = 32;
    }

    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = (uint16_t)str[i];
    }
  }

  _desc_str[0] = (uint16_t)(((uint16_t)TUSB_DESC_STRING << 8) | (uint16_t)(2U * chr_count + 2U));
  return _desc_str;
}

/*------------------------------------------------------------------*/
/* Vendor control requests                                          */
/*------------------------------------------------------------------*/

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
  /* The DATA/ACK stages of the MS OS 2.0 fetch carry no new information: the
     payload was already queued by tud_control_xfer(), so simply accept them. */
  if (stage != CONTROL_STAGE_SETUP) {
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == VENDOR_REQUEST_MICROSOFT) {
      return true;
    }
    return dap_vendor_control_xfer(rhport, stage, request);
  }

  /* Only the SETUP stage of the MS OS 2.0 descriptor fetch is answered here;
     every other vendor request is handled by the DAP command layer. */
  if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
      request->bRequest == VENDOR_REQUEST_MICROSOFT &&
      request->wIndex == MS_OS_20_DESC_WINDEX) {
    return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20,
                            sizeof(desc_ms_os_20));
  }

  return dap_vendor_control_xfer(rhport, stage, request);
}

/*------------------------------------------------------------------*/
/* Serial number from the STM32 96-bit unique ID                    */
/*------------------------------------------------------------------*/

static char _serial_str[3 + 24 + 1];   /* prefix + UID hex + NUL */

const char *board_usb_serial_number(void) {
  static bool ready = false;

  if (!ready) {
    static const char hex[] = "0123456789ABCDEF";

    /* "L67" + the 96-bit UID: 27 characters, and it never changes for a
       given chip.  The firmware release lives in bcdDevice, where hosts
       expect it, so upgrading the firmware does not rename the probe. */
    _serial_str[0] = 'L';
    _serial_str[1] = '6';
    _serial_str[2] = '7';

    /* UID is 96 bits at 0x1FFF7A10; byte order chosen so the string is
       stable and unique (not meant to be human readable). */
    const uint32_t *uid = (const uint32_t *)0x1FFF7A10UL;
    uint32_t idx = 3;

    for (uint32_t word = 0; word < 3; word++) {
      for (int32_t shift = 24; shift >= 0; shift -= 8) {
        uint8_t byte = (uint8_t)(uid[word] >> shift);
        _serial_str[idx++] = hex[byte >> 4];
        _serial_str[idx++] = hex[byte & 0x0F];
      }
    }
    _serial_str[idx] = '\0';
    ready = true;
  }

  return _serial_str;
}
