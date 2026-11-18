# recover_dfu.ps1 - flash the board through the STM32 system bootloader (DFU).
#
# Use this when the running firmware has taken over PA13/PA14 (the MCU's own
# SWD pins) and J-Link can no longer attach, i.e. the chip is "bricked" from the
# debugger's point of view while still running fine.  The normal build leaves a
# DAP_BOOT_DEBUG_DELAY_MS window after reset, so usually a plain connect works;
# this script is the fallback for when the window is set to 0.
#
# Put the board into the system bootloader first:
#   1. Hold BOOT0 and press/release RESET (or hold BOOT0 while powering up).
#   2. Release BOOT0 about 0.5 s after the reset is released.
# A "STM32 BOOTLOADER" / "STM32 DFU" USB device then appears.
#
# Usage:
#   .\tools\recover_dfu.ps1                          # flash the latest Keil .hex
#   .\tools\recover_dfu.ps1 -Hex tools\recovery_pb13_pb14.hex
#   .\tools\recover_dfu.ps1 -Wait 60                 # wait up to 60 s for DFU
#
# The script waits for the DFU device, then programs and verifies the image.

param(
  [string] $Hex   = 'MDK-ARM\00-TINYUSB-DAPLINK\00-TINYUSB-DAPLINK.hex',
  [int]    $Wait  = 120,
  [switch] $NoReset
)

$ErrorActionPreference = 'Stop'

$cli = 'C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
if (-not (Test-Path $cli)) { throw "STM32_Programmer_CLI not found at $cli" }

$hexPath = (Resolve-Path $Hex -ErrorAction Stop).Path
Write-Host "image   : $hexPath ($((Get-Item $hexPath).Length) bytes)"

function Test-DfuPresent {
  # PID 0xDF11 is the STM32 system bootloader (DFU) on VID 0x0483.
  [bool](Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
         Where-Object { $_.InstanceId -like 'USB\VID_0483&PID_DF11*' })
}

if (-not (Test-DfuPresent)) {
  Write-Host "waiting for the DFU bootloader (up to $Wait s) ..."
  Write-Host "  -> hold BOOT0, pulse RESET, release BOOT0 ~0.5 s later" -ForegroundColor Yellow

  $deadline = (Get-Date).AddSeconds($Wait)
  while ((Get-Date) -lt $deadline) {
    if (Test-DfuPresent) { break }
    Start-Sleep -Milliseconds 250
  }
}

if (-not (Test-DfuPresent)) {
  throw "no STM32 DFU device appeared within $Wait s - the board was not put into bootloader mode"
}

Write-Host "DFU device present" -ForegroundColor Green

# -c port=USB1 selects the DFU transport; the bootloader has no read-out
# protection on these boards, so -ob is not needed.
$args = @('-c', 'port=USB1', '-d', $hexPath, '-v')

if (-not $NoReset) { $args += '-rst' }

Write-Host "running: $cli $($args -join ' ')"
& $cli @args
$rc = $LASTEXITCODE

if ($rc -ne 0) {
  Write-Host "programming FAILED (exit $rc)" -ForegroundColor Red
  Write-Host "if the bootloader rejected the image, check that the board is not read-protected:"
  Write-Host "  $cli -c port=USB1 -ob displ"
  exit $rc
}

Write-Host "programming OK" -ForegroundColor Green

# Let the application come back and confirm the probe enumerates again.
Start-Sleep -Seconds 3
$probe = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
         Where-Object { $_.InstanceId -like '*VID_0D28&PID_0204*' }
if ($probe) {
  Write-Host "probe re-enumerated:" -ForegroundColor Green
  $probe | Select-Object Status, Class, FriendlyName, InstanceId | Format-Table -AutoSize
} else {
  Write-Host "probe did not re-enumerate yet - unplug and replug the USB cable" -ForegroundColor Yellow
}
