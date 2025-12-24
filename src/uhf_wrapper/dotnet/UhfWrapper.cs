using System;
using System.Runtime.InteropServices;
using System.Text;

namespace UhfWrapper
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct UHF_Tag
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 129)]
        public string Epc;
        public byte EpcLenBytes;
        public int RssiDbm;
        public byte Antenna;
        public byte TagType;
        public byte HasTs;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 6)]
        public byte[] TsRaw;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct UHF_DeviceInfo
    {
        public byte SoftMajor;
        public byte SoftMinor;
        public byte HardMajor;
        public byte HardMinor;
        public byte SoftRaw;
        public byte HardRaw;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string Sn;
    }

    public static class Native
    {
        private const string Dll = "UhfWrapper.dll";
        public const int UHF_WHITELIST_ENTRY_BYTES = 32;

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Init();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void UHF_Shutdown();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr UHF_GetLastError();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        private static extern int UHF_GetLastErrorCode();

        public static string GetLastError()
        {
            var ptr = UHF_GetLastError();
            return Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
        }

        public static int GetLastErrorCode()
        {
            return UHF_GetLastErrorCode();
        }

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_GetUsbCount();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_GetUsbInfoRaw(ushort index, byte[] outBuf, int outBufLen);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Open(ushort index);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Close();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_IsReaderPresent();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_IsOpen();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_IsConnected();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_GetInfo(ref UHF_DeviceInfo info);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_StartRead();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_StopRead();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_ClearBuffer();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_PeekBufferAll([Out] UHF_Tag[] outTags, int maxTags, out int outCount);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_PeekBufferDedup([Out] UHF_Tag[] outTags, int maxTags, out int outCount);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_PopBufferAll([Out] UHF_Tag[] outTags, int maxTags, out int outCount);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_PopBufferDedup([Out] UHF_Tag[] outTags, int maxTags, out int outCount);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_PopBufferAllSafe([Out] UHF_Tag[] outTags, int maxTags, out int outCount);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_PopBufferDedupSafe([Out] UHF_Tag[] outTags, int maxTags, out int outCount);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_GetPowerDbm();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_SetPowerDbm(int dbm);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_GetPowerPct();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_SetPowerPct(int pct);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_RelayOn();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_RelayOff();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Relay2On();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Relay2Off();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Out1On();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Out1Off();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Out2On();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_Out2Off();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_GetFreq(byte[] outFreq2);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_SetFreq(byte freq0, byte freq1);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_ReadTag(byte bank, byte wordPtr, byte wordCount,
                                            string pwdHex, byte[] outData, int outDataLen,
                                            out int outBytesRead);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_WriteTag(byte bank, byte wordPtr,
                                             byte[] data, int dataLenBytes,
                                             string pwdHex);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_WriteEpc(string epcHex, string pwdHex);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_WriteEpcSelected(string targetEpcHex, string newEpcHex, string pwdHex, int forceMulti);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_SelectEpc(string epcHex);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_ClearSelect();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_LockTag(byte lockType, byte lockMem, string pwdHex);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_WhitelistCount(out int count);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_WhitelistGetRaw(ushort index, [Out] byte[] outBuf, int outBufLen, out int outBytes);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_WhitelistGetHex(ushort index, StringBuilder outHex, int outHexLen, out int outBytes);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_WhitelistAddEpc(string epcHex);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        public static extern int UHF_WhitelistRemoveEpc(string epcHex);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_WhitelistClear();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int UHF_ModuleCommand(byte cmd, byte[] payload, int payloadLen,
                                                  [Out] byte[] outBuf, int outBufLen, out int outRespLen);

        // Vendor (partial) 1:1 mapping for common functions. All vendor exports
        // are still available via the native re-export if needed.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int SWHid_GetUsbCount();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_GetUsbInfo(ushort index, byte[] buf);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_OpenDevice(ushort index);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_CloseDevice();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_GetDeviceSystemInfo(byte devAdr, byte[] buf);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_ReadDeviceOneParam(byte devAdr, byte paramAddr, byte[] value);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_SetDeviceOneParam(byte devAdr, byte paramAddr, byte value);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_StartRead(byte devAdr);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_StopRead(byte devAdr);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_ClearTagBuf();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern byte SWHid_GetTagBuf(byte[] buf, out int length, out int tagNum);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_InventoryG2(byte devAdr, byte[] buf, out ushort totalLen, out ushort cardNum);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_WriteEPCG2(byte devAdr, byte[] pwd, byte[] epc, byte epcLen);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_ReadCardG2(byte devAdr, byte[] pwd, byte mem, byte wordPtr, byte readLen, byte[] data);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_WriteCardG2(byte devAdr, byte[] pwd, byte mem, byte wordPtr, byte writeLen, byte[] data);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_RelayOn(byte devAdr);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_RelayOff(byte devAdr);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_SetFreq(byte devAdr, byte[] freq);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern bool SWHid_ReadFreq(byte devAdr, byte[] freq);
    }
}
