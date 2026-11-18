# dap_cmd.ps1 - send arbitrary CMSIS-DAP v2 requests over WinUSB and print responses.
#
# Usage:
#   .\tools\dap_cmd.ps1 -Hex "00 F0"                  # DAP_Info(Capabilities)
#   .\tools\dap_cmd.ps1 -Hex "02 00"                  # DAP_Connect(default)
#   .\tools\dap_cmd.ps1 -Hex "10 80 80 00 00 00 00"   # SWJ_Pins: release nRESET
#   .\tools\dap_cmd.ps1 -Hex "00 F0","02 00"          # several requests in sequence
#
# Each request is zero-padded to one 64-byte DAP packet. The response is
# printed as hex plus the response length byte (response[1] is only the
# payload length for DAP_Info, so both are shown).

param(
  [string[]] $Hex = @("00 F0"),
  [int] $TimeoutMs = 2000
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class DapWinUsbCmd {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string n, uint a, uint s, IntPtr sec, uint d, uint f, IntPtr t);
    [DllImport("winusb.dll", SetLastError=true)]
    static extern bool WinUsb_Initialize(SafeFileHandle f, out IntPtr i);
    [DllImport("winusb.dll", SetLastError=true)]
    static extern bool WinUsb_Free(IntPtr i);
    [StructLayout(LayoutKind.Sequential)]
    public struct PIPE { public int PipeType; public byte PipeId; public ushort MaxPacketSize; public byte Interval; }
    [DllImport("winusb.dll", SetLastError=true)]
    static extern bool WinUsb_QueryPipe(IntPtr i, byte alt, byte idx, ref PIPE p);
    [DllImport("winusb.dll", SetLastError=true)]
    static extern bool WinUsb_WritePipe(IntPtr i, byte id, byte[] b, uint len, out uint w, IntPtr ov);
    [DllImport("winusb.dll", SetLastError=true)]
    static extern bool WinUsb_ReadPipe(IntPtr i, byte id, byte[] b, uint len, out uint r, IntPtr ov);
    [DllImport("winusb.dll", SetLastError=true)]
    static extern bool WinUsb_SetPipePolicy(IntPtr i, byte id, uint policy, uint len, ref uint val);

    public static string Run(string path, byte[][] reqs, uint timeoutMs) {
        var sb = new System.Text.StringBuilder();
        SafeFileHandle h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
        if (h.IsInvalid) { return "CreateFile failed: " + Marshal.GetLastWin32Error(); }
        IntPtr wu;
        if (!WinUsb_Initialize(h, out wu)) {
            int e = Marshal.GetLastWin32Error(); h.Close();
            return "WinUsb_Initialize failed: " + e;
        }

        byte op = 0, ip = 0;
        for (byte k = 0; k < 8; k++) {
            PIPE pi = new PIPE();
            if (!WinUsb_QueryPipe(wu, 0, k, ref pi)) { break; }
            if (pi.PipeType == 2) {
                if ((pi.PipeId & 0x80) != 0) { ip = pi.PipeId; } else { op = pi.PipeId; }
            }
        }
        if (op == 0 || ip == 0) { WinUsb_Free(wu); h.Close(); return "no bulk pipe pair"; }

        uint timeout = timeoutMs;
        WinUsb_SetPipePolicy(wu, ip, 0x03, 4, ref timeout);
        WinUsb_SetPipePolicy(wu, op, 0x03, 4, ref timeout);

        foreach (byte[] rq in reqs) {
            byte[] req = new byte[64];
            Array.Copy(rq, req, Math.Min(rq.Length, 64));
            uint w = 0;
            if (!WinUsb_WritePipe(wu, op, req, 64, out w, IntPtr.Zero)) {
                sb.AppendLine("  TX " + BitConverter.ToString(req, 0, rq.Length) + " -> write failed " + Marshal.GetLastWin32Error());
                continue;
            }
            byte[] rsp = new byte[64];
            uint r = 0;
            if (!WinUsb_ReadPipe(wu, ip, rsp, 64, out r, IntPtr.Zero)) {
                sb.AppendLine("  TX " + BitConverter.ToString(req, 0, rq.Length) + " -> read failed " + Marshal.GetLastWin32Error());
                continue;
            }
            sb.AppendLine(string.Format("  TX {0}  ->  RX[{1}] {2}",
                BitConverter.ToString(req, 0, rq.Length), r, BitConverter.ToString(rsp, 0, (int)Math.Max(r, 8))));
        }

        WinUsb_Free(wu);
        h.Close();
        return sb.ToString();
    }
}
'@

$dev = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
       Where-Object { $_.InstanceId -like '*VID_0D28&PID_0204&MI_02*' } |
       Select-Object -First 1
if (-not $dev) { Write-Output "MI_02 not present"; exit 1 }
Write-Output ("instance: " + $dev.InstanceId)

$path = "\\?\" + $dev.InstanceId.ToLower().Replace('\', '#') + "#{cdb3b5ad-293b-4663-aa36-1aae46463776}"

$reqs = @()
foreach ($h in $Hex) {
    $clean = $h -replace '[^0-9A-Fa-f]', ''
    $bytes = New-Object byte[] ($clean.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($clean.Substring($i * 2, 2), 16)
    }
    $reqs += ,$bytes
}

Write-Output ([DapWinUsbCmd]::Run($path, [byte[][]]$reqs, [uint32]$TimeoutMs))
