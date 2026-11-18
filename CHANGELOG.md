# Release notes

## v1.0.0 — 2026-11-18

First public release.

### Firmware

- **CMSIS-DAP v2.1.1** over bulk endpoints, `DAP_PACKET_SIZE = 64`,
  `DAP_PACKET_COUNT = 2`
- **WinUSB** binding through a Microsoft OS 2.0 descriptor — no driver
  installation on Windows 10/11
- **CDC-ACM** virtual COM port bridged to **USART1** (PA9/PA10, AF7), line
  coding follows the host request
- **SWD only** (`DAP_JTAG = 0`); TDI/TDO/nTRST hooks are benign stubs
- SWD on **PA13/PA14**, target reset on **PB0** (open-drain), status LED on
  **PC13** (active low)
- 2 s **boot debug window** so an external probe can still attach and reflash
  the MCU whose own SWD pins are being reused
- Serial number = `L67` + 96-bit UID (27 characters), stable across firmware
  upgrades; release number lives in `bcdDevice = 0x0104`

### Verified

- Enumerates as a composite device: CDC (COM) + vendor interface bound to
  `WINUSB` (`Service = WINUSB`, `ConfigFlags = 0`)
- `DAP_Info` returns vendor `Liang67`, product
  `Liang67 STM32F401CC DAPLink`, protocol `2.1.1`, capabilities `0x31`
  (SWD | Atomic | Timestamp), packet size 64, packet count 2
- `DAP_Connect` → SWD; `SWJ_Clock` accepts up to the configured maximum;
  `SWJ_Pins` reads and drives SWCLK/SWDIO/nRESET correctly
- CDC loopback through a PA9–PA10 jumper returns every byte unchanged
- Flashed and exercised on a WeAct MiniF4 V3.1 (STM32F401CCU6, 25 MHz HSE)

### Assets

- `00-TINYUSB-DAPLINK.hex` — Intel HEX, for J-Link / ST-Link / any SWD tool
- `00-TINYUSB-DAPLINK.bin` — raw binary at `0x08000000`, for DFU
  (`STM32_Programmer_CLI`) and other loaders
- `00-TINYUSB-DAPLINK.axf` — ELF with debug symbols, for Keil / GDB

### Footprint

```
FLASH  32.2 KB / 256 KB  (12.6%)
RAM     8.0 KB /  64 KB  (12.6%)
Stack   2 KB   (raised from the 1 KB default in startup_stm32f401xc.s)
```

### Known limitations

- **PA13/PA14 are shared with the MCU's own SWD port.** After the 2 s boot
  window the firmware owns them, so an external debugger can only attach during
  that window (or by entering the system bootloader via BOOT0). Set
  `DAP_BOOT_DEBUG_DELAY_MS` in `Core/Inc/DAP_config.h` to tune it.
- No JTAG, no SWO, no UART-over-DAP (the CDC port is a plain bridge to USART1).
- Keil MDK only; there is no CMake/GCC build in this repository.
- `DAP_TransferAbort` cannot actually interrupt a running block transfer:
  `DAP_ProcessCommand()` is synchronous, so an abort request is only dequeued
  after the transfer it was meant to stop has already finished. Upstream DAP.c
  does poll the flag in its retry loops; the flag just never gets set in time in
  this single-threaded design.
