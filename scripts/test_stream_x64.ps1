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
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_StartRead();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_StopRead();
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_PeekBufferAll([Out] UHF_Tag[] outTags, int maxTags, out int outCount);
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern int UHF_PopBufferAll([Out] UHF_Tag[] outTags, int maxTags, out int outCount);
  [DllImport(Dll, CallingConvention=CallingConvention.StdCall)] public static extern IntPtr UHF_GetLastError();
  public static string LastError() { return Marshal.PtrToStringAnsi(UHF_GetLastError()); }
}
"@

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build-x64/Release"
if (-not (Test-Path $BuildDir)) {
  throw "Build directory not found: $BuildDir"
}

Add-Type -TypeDefinition $code -Language CSharp
Set-Location $BuildDir

Write-Host "Init:" ([Uhf]::UHF_Init())
Write-Host "Open:" ([Uhf]::UHF_Open(0)) "Err:" ([Uhf]::LastError())
Write-Host "EnsureUsbTransport:" ([Uhf]::UHF_EnsureUsbTransport()) "Err:" ([Uhf]::LastError())
Write-Host "StartRead:" ([Uhf]::UHF_StartRead()) "Err:" ([Uhf]::LastError())
Start-Sleep -Milliseconds 3000

$tags = New-Object Uhf+UHF_Tag[] 64
$count = 0
$pop = [Uhf]::UHF_PopBufferAll($tags, $tags.Length, [ref]$count)
Write-Host "PopBufferAll:" $pop "Count:" $count "Err:" ([Uhf]::LastError())
for ($i=0; $i -lt $count; $i++) {
  $t = $tags[$i]
  Write-Host ("Pop Tag[{0}] EPC={1} RSSI={2} Ant={3} Type={4}" -f $i,$t.epc,$t.rssiDbm,$t.antenna,$t.tagType)
}

$count = 0
$peek = [Uhf]::UHF_PeekBufferAll($tags, $tags.Length, [ref]$count)
Write-Host "PeekBufferAll (after pop):" $peek "Count:" $count "Err:" ([Uhf]::LastError())

Write-Host "StopRead:" ([Uhf]::UHF_StopRead()) "Err:" ([Uhf]::LastError())
Write-Host "Close:" ([Uhf]::UHF_Close()) "Err:" ([Uhf]::LastError())
[Uhf]::UHF_Shutdown()
