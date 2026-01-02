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

typedef struct UHF_CalibrationResult {
  int32_t minDetectPowerDbm;
  int32_t recommendedPowerDbm;
  int32_t powerMarginDbm;
  int32_t rssiMinDbm;
  int32_t rssiMaxDbm;
  int32_t rssiAvgDbm;
  int32_t rssiMarginDbm;
  int32_t rssiFilterMinDbm;
  int32_t rssiFilterMaxDbm;
  int32_t sampleCount;
} UHF_CalibrationResult;

#define UHF_ERR_OK 0
#define UHF_ERR_UNKNOWN -1
#define UHF_ERR_INVALID_ARG -2
#define UHF_ERR_VENDOR_MISSING -3
#define UHF_ERR_VENDOR_CALL_FAILED -4
#define UHF_ERR_NOT_SUPPORTED -5
#define UHF_ERR_NOT_OPEN -6
#define UHF_ERR_NOT_CONNECTED -7
#define UHF_ERR_NO_DEVICE -8
#define UHF_ERR_MULTI_TAG -9
#define UHF_ERR_VERIFY_FAILED -10
#define UHF_ERR_ALREADY_READING -11
#define UHF_ERR_NOT_READING -12
#define UHF_ERR_NO_TAG -13
#define UHF_ERR_CALIBRATION_FAILED -14
#define UHF_ERR_CALIBRATION_MISSING -15

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
UHF_API int UHF_CALL UHF_GetTransport(void); // 0=USB, 1=RS232, 2=RJ45, 3=WIFI, 4=Weigand
UHF_API int UHF_CALL UHF_SetTransport(uint8_t transport);
UHF_API int UHF_CALL UHF_SetTransportUsb(void);
UHF_API int UHF_CALL UHF_EnsureUsbTransport(void);
UHF_API int UHF_CALL UHF_GetWorkMode(void); // 0=Answer, 1=Active, 2=Trigger (if supported)
UHF_API int UHF_CALL UHF_CheckSystemConfig(char* outMsg, int outMsgLen);
UHF_API int UHF_CALL UHF_SetWorkModeAnswer(void);
UHF_API int UHF_CALL UHF_SetWorkModeActive(void);
UHF_API int UHF_CALL UHF_SetWorkModeTrigger(void); // may be unsupported

UHF_API int UHF_CALL UHF_StartRead(void);
UHF_API int UHF_CALL UHF_StopRead(void);
UHF_API int UHF_CALL UHF_ClearBuffer(void);

UHF_API int UHF_CALL UHF_PeekBufferAll(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PeekBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferAll(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferAllSafe(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_PopBufferDedupSafe(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_DedupWindowSet(int windowMs);
UHF_API int UHF_CALL UHF_DedupWindowReset(void);
UHF_API int UHF_CALL UHF_DedupKeySet(int mode); // 0 = EPC only, 1 = EPC + antenna
UHF_API int UHF_CALL UHF_PopBufferDedupFiltered(UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_ReadOnce(int timeoutMs, UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_RssiFilterSet(int minDbm, int maxDbm);
UHF_API int UHF_CALL UHF_RssiFilterReset(void);

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
UHF_API int UHF_CALL UHF_WriteEpcSelected(const char* targetEpcHex, const char* newEpcHex,
                                          const char* pwdHex, int forceMulti);
UHF_API int UHF_CALL UHF_WriteTagSelected(const char* targetEpcHex, uint8_t bank, uint8_t wordPtr,
                                          const uint8_t* data, int dataLenBytes,
                                          const char* pwdHex, int forceMulti);
UHF_API int UHF_CALL UHF_SelectEpc(const char* epcHex);
UHF_API int UHF_CALL UHF_ClearSelect(void);
UHF_API int UHF_CALL UHF_CalibrationTagPrepare(const char* desiredEpcHex, int writeNew,
                                               char* outEpcHex, int outEpcLen);
UHF_API int UHF_CALL UHF_CalibrateByTag(const char* targetEpcHex,
                                        int powerMinDbm, int powerMaxDbm, int powerStepDbm,
                                        int readsPerStep, int powerMarginDbm,
                                        int captureMs, int rssiMarginDbm,
                                        int applySettings, UHF_CalibrationResult* outResult);
UHF_API int UHF_CALL UHF_CalibrationApply(const UHF_CalibrationResult* res);
UHF_API int UHF_CALL UHF_CalibrationGetCurrent(UHF_CalibrationResult* outResult);
UHF_API int UHF_CALL UHF_CalibrationSave(const char* path);
UHF_API int UHF_CALL UHF_CalibrationLoad(const char* path, int applySettings, UHF_CalibrationResult* outResult);
UHF_API int UHF_CALL UHF_ReadOnceCalibrated(int timeoutMs, UHF_Tag* outTags, int maxTags, int* outCount);
UHF_API int UHF_CALL UHF_ReadStreamCalibrated(int durationMs, UHF_Tag* outTags, int maxTags, int* outCount);

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
