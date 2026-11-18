# dap_probe.ps1 - talk to the CMSIS-DAP v2 vendor interface over WinUSB.
#
# Usage:
#   .\tools\dap_probe.ps1                 # probe DAP_Info ids
#   .\tools\dap_probe.ps1 -InfoId 0xF0    # one specific info id
#
# The script resolves the MI_02 device interface path from the live PnP
# instance, opens it with WinUSB and issues one DAP_Info request per id.

param(
  [byte[]] $InfoId = @(0x01, 0x02, 0x03, 0x04, 0xF0, 0xFF, 0xFE),
  [int]    $TimeoutMs = 2000
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class DapWinUsb {
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

    public static string Probe(string path, byte[] ids, uint timeoutMs) {
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
            sb.AppendLine(string.Format("  pipe{0} id=0x{1:x2} type={2} maxpkt={3}", k, pi.PipeId, pi.PipeType, pi.MaxPacketSize));
            if (pi.PipeType == 2) {
                if ((pi.PipeId & 0x80) != 0) { ip = pi.PipeId; } else { op = pi.PipeId; }
            }
        }
        if (op == 0 || ip == 0) { WinUsb_Free(wu); h.Close(); return sb.AppendLine("  no bulk pipe pair").ToString(); }
        sb.AppendLine(string.Format("  using OUT=0x{0:x2} IN=0x{1:x2}", op, ip));

        uint timeout = timeoutMs;
        WinUsb_SetPipePolicy(wu, ip, 0x03, 4, ref timeout);   // PIPE_TRANSFER_TIMEOUT

        foreach (byte id in ids) {
            byte[] req = new byte[64];
            req[0] = 0x00;   // DAP_Info
            req[1] = id;
            uint w = 0;
            if (!WinUsb_WritePipe(wu, op, req, 64, out w, IntPtr.Zero)) {
                sb.AppendLine(string.Format("  DAP_Info(0x{0:x2}): write failed {1}", id, Marshal.GetLastWin32Error()));
                continue;
            }
            byte[] rsp = new byte[64];
            uint r = 0;
            if (!WinUsb_ReadPipe(wu, ip, rsp, 64, out r, IntPtr.Zero)) {
                sb.AppendLine(string.Format("  DAP_Info(0x{0:x2}): read failed {1}", id, Marshal.GetLastWin32Error()));
                continue;
            }
            int n = rsp[1];
            string val;
            if (id == 0xF0 || id == 0xFF || id == 0xFE) {
                val = "bytes=" + BitConverter.ToString(rsp, 2, Math.Min(n, 8));
            } else {
                var t = new System.Text.StringBuilder();
                for (int j = 2; j < 2 + n && j < r; j++) { t.Append((char)rsp[j]); }
                val = "'" + t + "'";
            }
            sb.AppendLine(string.Format("  DAP_Info(0x{0:x2}) cmd=0x{1:x2} len={2} {3}", id, rsp[0], n, val));
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
Write-Output ("path    : " + $path)
Write-Output ([DapWinUsb]::Probe($path, $InfoId, [uint32]$TimeoutMs))
