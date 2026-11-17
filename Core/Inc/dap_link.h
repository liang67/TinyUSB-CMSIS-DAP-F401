/**
  ******************************************************************************
  * @file    dap_link.h
  * @brief   DAPLink (CMSIS-DAP v2 + CDC-ACM) application interface.
  ******************************************************************************
  */

#ifndef __DAP_LINK_H__
#define __DAP_LINK_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the CMSIS-DAP core, the USB device stack and the UART bridge.
   Call once after HAL_Init()/SystemClock_Config()/MX_*_Init(). */
void dap_link_init(void);

/* Run one iteration of the DAPLink main loop: polls TinyUSB, processes a
   pending CMSIS-DAP packet, bridges CDC <-> USART1 and updates the LED. */
void dap_link_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __DAP_LINK_H__ */
