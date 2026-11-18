[中文说明](README_zh.md)

# TinyUSB-CMSIS-DAP-F401 — CMSIS-DAP v2 probe firmware for STM32F401CC

A **CMSIS-DAP v2 debug probe** for the **STM32F401CCU6**, built on
**[TinyUSB](https://github.com/hathach/tinyusb)** and the **ARM CMSIS-DAP**
sources, with a **CDC-ACM virtual COM port** bridged to **USART1**.

Driverless on Windows 10/11: a Microsoft OS 2.0 descriptor binds the vendor
interface to **WinUSB** automatically, so Keil MDK, pyOCD and OpenOCD work out
of the box with no driver installation.

Tested on the WeAct MiniF4 V3.1 layout (STM32F401CCU6, 25 MHz crystal).

## How it differs from DAPLINK-STM32F401CC

This repo and [DAPLINK-STM32F401CC](https://github.com/liang67/DAPLINK-STM32F401CC)
are **two independent implementations** of the same idea
(CMSIS-DAP v2 + CDC):

| | This repo | DAPLINK-STM32F401CC |
|---|---|---|
| Code base | ARM **CMSIS-DAP** sources (`DAP.c` / `SW_DP.c`) | **ARMmbed/DAPLink** port |
| USB stack | **TinyUSB** 0.18.0 | DAPLink's own stack |
| Build | Keil MDK only (ARMCLANG) | CMake (GCC) **and** Keil |
| SWD pins | **PA13 / PA14** (shared with the MCU's own debug port) | PB13 / PB14 (dedicated) |

## Features

| Function | Implementation |
|---|---|
| Debug probe | CMSIS-DAP **v2.1.1**, `DAP_PACKET_SIZE = 64`, `DAP_PACKET_COUNT = 2` |
| WinUSB | BOS descriptor + Microsoft OS 2.0 descriptor (compatible ID `WINUSB`) |
| Serial port | CDC-ACM ↔ **USART1** (PA9 TX / PA10 RX, AF7) |
| Status LED | single **PC13** LED, active low, shared by heartbeat / connected / error |
| Target reset | **PB0** open-drain, 20 ms low pulse from `DAP_ResetTarget` |

VID/PID reuse mbed's DAPLink assignment: `0x0D28` / `0x0204`, `bcdDevice = 0x0104`.

### SWD only, no JTAG

`DAP_JTAG = 0` — the board exposes no TDI/TDO/nTRST. `DAP_SWJ_Pins` references
those hooks unconditionally, so `DAP_config.h` provides harmless stubs.

## Pin assignment

| Pin | Function | Notes |
|---|---|---|
| **PA13** | SWDIO / TMS | push-pull output (or input with pull-up), drives the target |
| **PA14** | SWCLK / TCK | push-pull output, drives the target |
| **PB0** | nRESET | open-drain output + internal pull-up, low resets the target |
| **PC13** | status LED | active low |
| PA9 | USART1_TX | AF7 |
| PA10 | USART1_RX | AF7 |
| PA11 | USB_OTG_FS_DM | AF10 |
| PA12 | USB_OTG_FS_DP | AF10 |

## ⚠️ PA13/PA14 collide with this MCU's own debug port

**PA13/PA14 *are* this STM32F401's own SWD pins.** As soon as the firmware
drives them as GPIOs to bit-bang the target, an external J-Link/ST-Link can no
longer attach to *this* chip. USB keeps working perfectly — you just cannot
flash the probe any more.

The firmware therefore opens a **boot debug window**:

```c
/* Core/Src/main.c, USER CODE BEGIN SysInit */
#if (DAP_BOOT_DEBUG_DELAY_MS > 0U)
  HAL_Delay(DAP_BOOT_DEBUG_DELAY_MS);
#endif
```

`DAP_BOOT_DEBUG_DELAY_MS` lives in [`Core/Inc/DAP_config.h`](Core/Inc/DAP_config.h)
and defaults to **2000 ms**. It runs after `SystemClock_Config()` and before
`MX_GPIO_Init()`, so **for 2 seconds after every reset** a debugger can still
attach and reflash. After that the firmware owns the pins.

- Can't connect? Press **RESET** on the board and attach immediately.
- Slow probes (J-Link OB, etc.): raise the value to `5000`.
- Production builds may set it to `0` — **but then DFU via BOOT0 is the only
  way back in**.

If you already locked yourself out, use [`tools/recover_dfu.ps1`](tools/recover_dfu.ps1):

```powershell
# 1. Hold BOOT0, tap RESET, release BOOT0 ~0.5 s later
# 2. Run (defaults to the hex in MDK-ARM's output folder)
.\tools\recover_dfu.ps1
```

## Hardware requirements

### Internal D+ pull-up — no external resistor

The STM32F401's OTG_FS has an internal D+ pull-up, enabled by the dwc2 core
clearing `DCTL.SDIS` (see `dcd_connect()`). **Do not** add a 1.5 kΩ pull-up.

Note the F401 exposes no DPPU bit (unlike the F1 series); GCCFG only carries
VBUS-related controls.

### VBUS sensing must be disabled (board specific)

On the STM32F401 the OTG_FS **VBUS sense input is PA9**, and this design uses
PA9 as `USART1_TX`. Left enabled, the core forever believes no host is
attached, never enables the D+ pull-up, and **the host never sees the device**
(this was the first big trap during development).

TinyUSB's dwc2 port only writes `GCCFG.PWRDWN` and never touches `NOVBUSSENS`,
so the firmware does it itself:

```c
/* Core/Src/usb_otg.c */
void usb_otg_disable_vbus_sensing(void)
{
  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_NOVBUSSENS;
  USB_OTG_FS->GCCFG &= ~(USB_OTG_GCCFG_VBUSASEN | USB_OTG_GCCFG_VBUSBSEN);
}
```

Called once from `usb_otg_clock_gpio_init()` **and again after** `tusb_init()`
(the dwc2 core reset clears GCCFG).

## Interfaces and endpoints

| Interface | Class | Endpoints |
|---|---|---|
| 0 | CDC Control | `0x81` IN (notify, 8 bytes) |
| 1 | CDC Data | `0x02` OUT / `0x82` IN |
| 2 | Vendor (WinUSB) | `0x03` OUT / `0x83` IN |

FS on the F401 offers EP0..EP3, which this uses exactly.

## Serial number format

Serial number = **`"L67"` + the 96-bit chip UID in hex**, **27 characters**:

```
L67 003300543038510932313531
└┬┘ └──────────┬──────────┘
 │             └─ UID (0x1FFF7A10, 3 words, high byte first)
 └─ fixed prefix
```

The UID is unique per chip and never changes, so **upgrading the firmware does
not rename the probe**. The release lives in `bcdDevice` (`0x0104`), which is
where hosts expect it.

> Early builds embedded `bcdDevice` in the serial number to force Windows to
> create a fresh device instance and dodge a node poisoned by a bad MS OS 2.0
> descriptor. That is gone now.

## Building

Requires **Keil MDK-ARM 5** with **Arm Compiler 6** (developed with MDK Plus
5.41 / AC6.22) and the `Keil.STM32F4xx_DFP` pack.

```powershell
# Command line
& 'C:\Keil_v5\UV4\UV4.exe' -r 'MDK-ARM\00-TINYUSB-DAPLINK.uvprojx' -j0 -o build.log
```

Or just open `MDK-ARM/00-TINYUSB-DAPLINK.uvprojx` in Keil.

Current footprint (`Optim` level 3 in the `.uvprojx`, which AC6 maps to `-O2`):

```
RW_IRAM1  0x20000000  size 0x00010000  used 8.0 KB  / 64 KB   (12.6%)
ER_IROM1  0x08000000  size 0x00040000  used 32.2 KB / 256 KB  (12.6%)
Stack 0x800 (2 KB), Heap 0x200
```

The stack in `startup_stm32f401xc.s` was raised from the default 1 KB to 2 KB —
the TinyUSB device stack plus HAL callbacks need it during enumeration.

Outputs land in `MDK-ARM/00-TINYUSB-DAPLINK/`:
`00-TINYUSB-DAPLINK.hex` (flashing), `.bin` (DFU/other tools), `.axf` (debugging).

## Flashing

### 1. SWD (recommended)

```powershell
JLink.exe -device STM32F401CC -if SWD -speed 4000 -autoconnect 1 `
          -CommanderScript flash.jlink
# flash.jlink:
#   r
#   h
#   loadfile "MDK-ARM\00-TINYUSB-DAPLINK\00-TINYUSB-DAPLINK.hex"
#   r
#   g
#   q
```

**Connect within 2 seconds of a reset** (see the PA13/PA14 note above).

### 2. DFU (recovery when this firmware locked you out)

```powershell
.\tools\recover_dfu.ps1
```

The script waits for `USB\VID_0483&PID_DF11` (the STM32 system bootloader), then
programs and verifies through `STM32_Programmer_CLI`.

## Verification tooling

Three PowerShell scripts in `tools/` that talk to the running probe directly
(no Keil or pyOCD needed):

```powershell
# DAP_Info: vendor / product / serial / firmware version / capabilities / packet size
.\tools\dap_probe.ps1

# Send arbitrary CMSIS-DAP requests and print the responses
.\tools\dap_cmd.ps1 -Hex "00 F0", "02 00", "10 80 80"

# DFU recovery
.\tools\recover_dfu.ps1
```

`dap_cmd.ps1` opens the MI_02 interface through WinUSB (GUID
`{CDB3B5AD-293B-4663-AA36-1AAE46463776}`), OUT pipe `0x03`, IN pipe `0x83`, and
zero-pads each request to one 64-byte DAP packet.

### Sample output

```
TX 00-F0      -> RX[4]  00-02-31              # DAP_Info(Capabilities) = SWD|Atomic|Timestamp
TX 00-01      -> RX[10] 00-08-"Liang67"       # vendor name
TX 00-02      -> RX[30] 00-1C-"Liang67 STM32F401CC DAPLink"
TX 00-03      -> RX[30] 00-1C-"L6700..."      # serial, 27 characters
TX 00-04      -> RX[8]  00-06-"2.1.1"         # DAP protocol version
TX 02-00      -> RX[2]  02-01                 # DAP_Connect → SWD
TX 10-83-83   -> RX[2]  10-A3                 # SWJ_Pins: SWCLK/SWDIO both high
TX 10-00-83   -> RX[2]  10-20                 # SWJ_Pins: SWCLK/SWDIO both low
TX 10-80-80   -> RX[2]  10-A1                 # SWJ_Pins: release nRESET
```

## Implementation notes and known traps

All of these were hit for real during development — worth knowing before
touching the code.

### 1. Don't implement `tud_vendor_rx_cb()`

TinyUSB declares that callback `TU_ATTR_WEAK`, **so your definition is weak
too**. armlink sees no strong reference and drops the whole function:

```
Removing dap_link.o(.text.tud_vendor_rx_cb), (132 bytes).
```

Every vendor OUT packet is then **silently discarded** — USB enumerates, CDC
works, only DAP goes deaf. This project polls `tud_vendor_available()` inside
`dap_process_packet()` instead. The CDC bridge always polled
(`tud_cdc_available()`), which is why it was unaffected.

### 2. MS OS 2.0 `bConfigurationValue` must be 0

If the Configuration subset header says `bConfigurationValue = 1`, Windows
fetches the whole descriptor set and then **throws away** the compatible ID.
The symptom is the vendor interface reporting **Code 28
(CM_PROB_FAILED_INSTALL)**, `ConfigFlags = 64`, and `CompatibleIDs` missing
`USB\MS_COMP_WINUSB`.

### 3. `DeviceInterfaceGUIDs` must be `REG_MULTI_SZ`

`wPropertyDataType` has to be `0x0007`, not `0x0001`. The GUID used here is the
one CMSIS-DAP probes conventionally publish:
`{CDB3B5AD-293B-4663-AA36-1AAE46463776}`.

### 4. A poisoned device node never heals

Windows asks for the OS descriptor only on the **first** enumeration and caches
the verdict in
`HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<VID><PID><bcdDevice>`.
Once that attempt fails (`osvc=0,0`), WinUSB will never bind — no driver
reinstall fixes it. The only cure is changing the **serial number or
bcdDevice** so Windows creates a brand-new instance.

### 5. The CDC bridge does not require DTR

`tud_cdc_connected()` demands the host raise DTR, which many serial terminals
never do. This project uses `tud_cdc_ready()`, so data flows as soon as you
open the port.

### 6. 8 data bits + parity needs `UART_WORDLENGTH_9B`

STM32 counts the parity bit in the frame length. `tud_cdc_line_coding_cb()`
selects `Parity == UART_PARITY_NONE ? 8B : 9B` accordingly.

## Repository layout

```
Core/Inc/
  DAP_config.h          board config: pins, clock, capabilities, delay, LED hooks
  dap_link.h            DAP link + serial bridge interface
  tusb_config.h         TinyUSB configuration (CDC + Vendor)
  usb_descriptors.h     descriptor and string indices
Core/Src/
  dap_link.c            DAP packet handling, CDC↔USART1 bridge, LED state machine
  usb_descriptors.c     device/config/BOS/MS OS 2.0/string descriptors
  main.c                CubeMX skeleton + boot debug window
  usb_otg.c             OTG_FS clock, GPIO, VBUS sensing disable
  gpio.c                pin initialisation
  usart.c               USART1 115200 8N1
  stm32f4xx_it.c        interrupts (OTG_FS goes to tud_int_handler)
MDK-ARM/                Keil project
Middlewares/Third_Party/
  tinyusb/              TinyUSB 0.18.0 (trimmed, src/ only)
  cmsis-dap/            ARM CMSIS_5 5.9.0 DAP sources
tools/                  verification and recovery scripts
```

## Third-party components

| Component | Version | License |
|---|---|---|
| [TinyUSB](https://github.com/hathach/tinyusb) | 0.18.0 | MIT |
| [CMSIS-DAP](https://github.com/ARM-software/CMSIS_5) | 5.9.0 | Apache-2.0 |
| [STM32F4 HAL](https://github.com/STMicroelectronics/stm32f4xx_hal_driver) | CubeMX F4 1.8.x | BSD-3-Clause |
| [CMSIS](https://github.com/ARM-software/CMSIS_5) | 5.x | Apache-2.0 |

## References

- [CMSIS-DAP specification](https://arm-software.github.io/CMSIS_5/DAP/html/index.html)
- [Microsoft OS 2.0 descriptors](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification)
- [TinyUSB documentation](https://docs.tinyusb.org/)

## License

Project code is released under the **Apache License 2.0**, see [LICENSE](LICENSE).
Third-party components keep their own licenses (table above).
