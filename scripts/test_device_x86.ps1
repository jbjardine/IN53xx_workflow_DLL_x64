$code = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Uhf {
  public const string Dll = "UhfWrapper.dll";
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi, Pack=1)]
  public struct UHF_Tag {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=129)]
    public string epc;
    public byte epcLenBytes;
    public int rssiDbm;
    public byte antenna;
    public byte tagType;
    public byte hasTs;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst=6)]
    public byte[] tsRaw;
  }
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_Init();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern void UHF_Shutdown();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_Open(ushort index);
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_Close();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_EnsureUsbTransport();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_CheckSystemConfig(StringBuilder outMsg, int outMsgLen);
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_SetWorkModeAnswer();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_ReadOnce(int timeoutMs, [Out] UHF_Tag[] outTags, int maxTags, out int outCount);
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern IntPtr UHF_GetLastError();
  public static string LastError() { return Marshal.PtrToStringAnsi(UHF_GetLastError()); }
}
"@

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build-x86/Release"
if (-not (Test-Path $BuildDir)) {
  throw "Build directory not found: $BuildDir"
}

Add-Type -TypeDefinition $code -Language CSharp
Set-Location $BuildDir

Write-Host "Init:" ([Uhf]::UHF_Init())
Write-Host "Open:" ([Uhf]::UHF_Open(0)) "Err:" ([Uhf]::LastError())
Write-Host "EnsureUsbTransport:" ([Uhf]::UHF_EnsureUsbTransport()) "Err:" ([Uhf]::LastError())
$msg = New-Object System.Text.StringBuilder 256
$ok = [Uhf]::UHF_CheckSystemConfig($msg, $msg.Capacity)
Write-Host "ConfigCheck:" $ok "Msg:" $msg.ToString() "Err:" ([Uhf]::LastError())
$tags = New-Object Uhf+UHF_Tag[] 16
$count = 0
$rok = [Uhf]::UHF_ReadOnce(300, $tags, $tags.Length, [ref]$count)
Write-Host "ReadOnce:" $rok "Count:" $count "Err:" ([Uhf]::LastError())
for ($i=0; $i -lt $count; $i++) {
  $t = $tags[$i]
  Write-Host ("Tag[{0}] EPC={1} RSSI={2} Ant={3} Type={4}" -f $i,$t.epc,$t.rssiDbm,$t.antenna,$t.tagType)
}
Write-Host "Close:" ([Uhf]::UHF_Close()) "Err:" ([Uhf]::LastError())
[Uhf]::UHF_Shutdown()
