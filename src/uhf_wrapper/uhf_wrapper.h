#ifndef UHF_WRAPPER_H_
#define UHF_WRAPPER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #if defined(UHF_WRAPPER_EXPORTS)
    #define UHF_API __declspec(dllexport)
  #else
    #define UHF_API __declspec(dllimport)
  #endif
  #define UHF_CALL __stdcall
#else
  #define UHF_API
  #define UHF_CALL
#endif

#define UHF_MAX_EPC_BYTES 64
#define UHF_MAX_EPC_HEX   (UHF_MAX_EPC_BYTES * 2)
#define UHF_TS_LEN        6
#define UHF_WHITELIST_ENTRY_BYTES 32

#if defined(_MSC_VER)
  #pragma pack(push, 1)
#endif

typedef struct UHF_Tag {
  char epc[UHF_MAX_EPC_HEX + 1];
  uint8_t epcLenBytes;
  int rssiDbm;
  uint8_t antenna;
  uint8_t tagType;
  uint8_t hasTs;
  uint8_t tsRaw[UHF_TS_LEN];
} UHF_Tag;

typedef struct UHF_DeviceInfo {
  uint8_t softMajor;
  uint8_t softMinor;
  uint8_t hardMajor;
  uint8_t hardMinor;
  uint8_t softRaw;
  uint8_t hardRaw;
  char sn[32];
} UHF_DeviceInfo;

#define UHF_ERR_OK 0
#define UHF_ERR_UNKNOWN -1
#define UHF_ERR_INVALID_ARG -2
#define UHF_ERR_VENDOR_MISSING -3
#define UHF_ERR_VENDOR_CALL_FAILED -4
#define UHF_ERR_NOT_SUPPORTED -5
#define UHF_ERR_NOT_OPEN -6
#define UHF_ERR_NOT_CONNECTED -7
#define UHF_ERR_NO_DEVICE -8

#if defined(_MSC_VER)
  #pragma pack(pop)
#endif

UHF_API int UHF_CALL UHF_Init(void);
UHF_API void UHF_CALL UHF_Shutdown(void);
UHF_API const char* UHF_CALL UHF_GetLastError(void);
UHF_API int UHF_CALL UHF_GetLastErrorCode(void);

UHF_API int UHF_CALL UHF_GetUsbCount(void);
UHF_API int UHF_CALL UHF_GetUsbInfoRaw(uint16_t index, uint8_t* outBuf, int outBufLen);

UHF_API int UHF_CALL UHF_Open(uint16_t index);
UHF_API int UHF_CALL UHF_Close(void);
UHF_API int UHF_CALL UHF_IsReaderPresent(void);
UHF_API int UHF_CALL UHF_IsOpen(void);
UHF_API int UHF_CALL UHF_IsConnected(void);

UHF_API int UHF_CALL UHF_GetInfo(UHF_DeviceInfo* outInfo);

UHF_API int UHF_CALL UHF_StartRead(void);
UHF_API int UHF_CALL UHF_StopRead(void);
UHF_API int UHF_CALL UHF_ClearBuffer(void);

UHF_API int UHF_CALL UHF_PeekBufferAll(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PeekBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferAll(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferAllSafe(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferDedupSafe(UHF_Tag* outTags, int maxTags, int* outCount);

UHF_API int UHF_CALL UHF_GetPowerDbm(void);
UHF_API int UHF_CALL UHF_SetPowerDbm(int dbm);
UHF_API int UHF_CALL UHF_GetPowerPct(void);
UHF_API int UHF_CALL UHF_SetPowerPct(int pct);

UHF_API int UHF_CALL UHF_RelayOn(void);
UHF_API int UHF_CALL UHF_RelayOff(void);
UHF_API int UHF_CALL UHF_Relay2On(void);
UHF_API int UHF_CALL UHF_Relay2Off(void);
UHF_API int UHF_CALL UHF_Out1On(void);
UHF_API int UHF_CALL UHF_Out1Off(void);
UHF_API int UHF_CALL UHF_Out2On(void);
UHF_API int UHF_CALL UHF_Out2Off(void);

UHF_API int UHF_CALL UHF_GetFreq(uint8_t* outFreq2);
UHF_API int UHF_CALL UHF_SetFreq(uint8_t freq0, uint8_t freq1);

UHF_API int UHF_CALL UHF_ReadTag(uint8_t bank, uint8_t wordPtr, uint8_t wordCount,
                                 const char* pwdHex, uint8_t* outData, int outDataLen,
                                 int* outBytesRead);
UHF_API int UHF_CALL UHF_WriteTag(uint8_t bank, uint8_t wordPtr,
                                  const uint8_t* data, int dataLenBytes,
                                  const char* pwdHex);
UHF_API int UHF_CALL UHF_WriteEpc(const char* epcHex, const char* pwdHex);
UHF_API int UHF_CALL UHF_SelectEpc(const char* epcHex);
UHF_API int UHF_CALL UHF_ClearSelect(void);

// NOTE: lockType/lockMem are combined into a single lockCfg byte when possible.
UHF_API int UHF_CALL UHF_LockTag(uint8_t lockType, uint8_t lockMem, const char* pwdHex);

UHF_API int UHF_CALL UHF_WhitelistCount(int* outCount);
UHF_API int UHF_CALL UHF_WhitelistGetRaw(uint16_t index, uint8_t* outBuf, int outBufLen, int* outBytes);
UHF_API int UHF_CALL UHF_WhitelistGetHex(uint16_t index, char* outHex, int outHexLen, int* outBytes);
UHF_API int UHF_CALL UHF_WhitelistAddEpc(const char* epcHex);
UHF_API int UHF_CALL UHF_WhitelistRemoveEpc(const char* epcHex);
UHF_API int UHF_CALL UHF_WhitelistClear(void);

UHF_API int UHF_CALL UHF_ModuleCommand(uint8_t cmd, const uint8_t* payload, int payloadLen,
                                       uint8_t* outBuf, int outBufLen, int* outRespLen);

#ifdef __cplusplus
}
#endif

#endif
