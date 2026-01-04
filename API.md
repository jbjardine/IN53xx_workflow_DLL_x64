# UHF Wrapper API (DLL)

This document describes the public, stable `UHF_*` API exposed by `UhfWrapper.dll`
for IN53xx / Impinj R2000 UHF readers (Windows x86/x64, HID mode).

For exact declarations, see `src/uhf_wrapper/uhf_wrapper.h`.

## Linking / Calling convention

- Windows DLL, `__stdcall`
- Import macro: `UHF_API` (dllexport/dllimport)
- Include:

```c
#include "uhf_wrapper.h"
```

## Error handling

Most functions return `int`:
- `0` = success
- `<0` = error (see codes below)

Use:
- `UHF_GetLastError()` for a short human-readable string
- `UHF_GetLastErrorCode()` for the last error code

Common error codes:

```
UHF_ERR_OK (0)
UHF_ERR_UNKNOWN (-1)
UHF_ERR_INVALID_ARG (-2)
UHF_ERR_VENDOR_MISSING (-3)
UHF_ERR_VENDOR_CALL_FAILED (-4)
UHF_ERR_NOT_SUPPORTED (-5)
UHF_ERR_NOT_OPEN (-6)
UHF_ERR_NOT_CONNECTED (-7)
UHF_ERR_NO_DEVICE (-8)
UHF_ERR_MULTI_TAG (-9)
UHF_ERR_VERIFY_FAILED (-10)
UHF_ERR_ALREADY_READING (-11)
UHF_ERR_NOT_READING (-12)
UHF_ERR_NO_TAG (-13)
UHF_ERR_CALIBRATION_FAILED (-14)
UHF_ERR_CALIBRATION_MISSING (-15)
```

## Types

### `UHF_Tag`
```c
typedef struct UHF_Tag {
  char epc[UHF_MAX_EPC_HEX + 1];
  uint8_t epcLenBytes;
  int rssiDbm;
  uint8_t antenna;
  uint8_t tagType;
  uint8_t hasTs;
  uint8_t tsRaw[UHF_TS_LEN];
} UHF_Tag;
```

### `UHF_DeviceInfo`
```c
typedef struct UHF_DeviceInfo {
  uint8_t softMajor;
  uint8_t softMinor;
  uint8_t hardMajor;
  uint8_t hardMinor;
  uint8_t softRaw;
  uint8_t hardRaw;
  char sn[32];
} UHF_DeviceInfo;
```

### `UHF_Status`
```c
typedef struct UHF_Status {
  int32_t present;
  int32_t open;
  int32_t connected;
  int32_t powerDbm;
  int32_t powerPct;
  int32_t transport;
  int32_t workMode;
  uint8_t freq0;
  uint8_t freq1;
  int32_t hasFreq;
  int32_t rssiFilterEnabled;
  int32_t rssiFilterMinDbm;
  int32_t rssiFilterMaxDbm;
  int32_t dedupWindowMs;
  int32_t dedupKeyMode;
} UHF_Status;
```

### `UHF_CalibrationResult`
```c
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
```

## Constants

```
UHF_MAX_EPC_BYTES
UHF_MAX_EPC_HEX
UHF_TS_LEN
UHF_WHITELIST_ENTRY_BYTES
```

## Function Reference

### Init / lifecycle
```c
int UHF_Init(void);
void UHF_Shutdown(void);
const char* UHF_GetLastError(void);
int UHF_GetLastErrorCode(void);
```

### Device / connection
```c
int UHF_GetUsbCount(void);
int UHF_GetUsbInfoRaw(uint16_t index, uint8_t* outBuf, int outBufLen);
int UHF_Open(uint16_t index);
int UHF_Close(void);
int UHF_IsReaderPresent(void);
int UHF_IsOpen(void);
int UHF_IsConnected(void);
int UHF_GetStatus(UHF_Status* outStatus);
```

`UHF_GetStatus` is a convenience call that aggregates connection state,
transport/workmode, power, frequency, and wrapper-side filters (dedup/RSSI).

### Info / transport / work mode
```c
int UHF_GetInfo(UHF_DeviceInfo* outInfo);
int UHF_GetTransport(void);
int UHF_SetTransport(uint8_t transport);
int UHF_SetTransportUsb(void);
int UHF_EnsureUsbTransport(void);
int UHF_GetWorkMode(void);
int UHF_CheckSystemConfig(char* outMsg, int outMsgLen);
int UHF_SetWorkModeAnswer(void);
int UHF_SetWorkModeActive(void);
int UHF_SetWorkModeTrigger(void);
```

### Read / buffer
```c
int UHF_StartRead(void);
int UHF_StopRead(void);
int UHF_ClearBuffer(void);

int UHF_PeekBufferAll(UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_PeekBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_PopBufferAll(UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_PopBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_PopBufferAllSafe(UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_PopBufferDedupSafe(UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_PopBufferDedupFiltered(UHF_Tag* outTags, int maxTags, int* outCount);

int UHF_DedupWindowSet(int windowMs);
int UHF_DedupWindowReset(void);
int UHF_DedupKeySet(int mode); // 0=EPC only, 1=EPC+antenna

int UHF_ReadOnce(int timeoutMs, UHF_Tag* outTags, int maxTags, int* outCount);
```

### RSSI filter (software)
```c
int UHF_RssiFilterSet(int minDbm, int maxDbm);
int UHF_RssiFilterReset(void);
```

### Power
```c
int UHF_GetPowerDbm(void);
int UHF_SetPowerDbm(int dbm);
int UHF_GetPowerPct(void);
int UHF_SetPowerPct(int pct);
```

### Select / read / write / lock
```c
int UHF_SelectEpc(const char* epcHex);
int UHF_ClearSelect(void);

int UHF_ReadTag(uint8_t bank, uint8_t wordPtr, uint8_t wordCount,
                const char* pwdHex, uint8_t* outData, int outDataLen,
                int* outBytesRead);

int UHF_WriteTag(uint8_t bank, uint8_t wordPtr,
                 const uint8_t* data, int dataLenBytes,
                 const char* pwdHex);

int UHF_WriteEpc(const char* epcHex, const char* pwdHex);

int UHF_WriteEpcSelected(const char* targetEpcHex, const char* newEpcHex,
                         const char* pwdHex, int forceMulti);

int UHF_WriteTagSelected(const char* targetEpcHex, uint8_t bank, uint8_t wordPtr,
                         const uint8_t* data, int dataLenBytes,
                         const char* pwdHex, int forceMulti);

int UHF_LockTag(uint8_t lockType, uint8_t lockMem, const char* pwdHex);
```

### Calibration
```c
int UHF_CalibrationTagPrepare(const char* desiredEpcHex, int writeNew,
                              char* outEpcHex, int outEpcLen);

int UHF_CalibrateByTag(const char* targetEpcHex,
                       int powerMinDbm, int powerMaxDbm, int powerStepDbm,
                       int readsPerStep, int powerMarginDbm,
                       int captureMs, int rssiMarginDbm,
                       int applySettings, UHF_CalibrationResult* outResult);

int UHF_CalibrationApply(const UHF_CalibrationResult* res);
int UHF_CalibrationGetCurrent(UHF_CalibrationResult* outResult);
int UHF_CalibrationSave(const char* path);
int UHF_CalibrationLoad(const char* path, int applySettings, UHF_CalibrationResult* outResult);

int UHF_ReadOnceCalibrated(int timeoutMs, UHF_Tag* outTags, int maxTags, int* outCount);
int UHF_ReadStreamCalibrated(int durationMs, UHF_Tag* outTags, int maxTags, int* outCount);
```

### Frequency
```c
int UHF_GetFreq(uint8_t* outFreq2);
int UHF_SetFreq(uint8_t freq0, uint8_t freq1);
```

### I/O (relay / outputs)
```c
int UHF_RelayOn(void);
int UHF_RelayOff(void);
int UHF_Relay2On(void);
int UHF_Relay2Off(void);
int UHF_Out1On(void);
int UHF_Out1Off(void);
int UHF_Out2On(void);
int UHF_Out2Off(void);
```

### Whitelist
```c
int UHF_WhitelistCount(int* outCount);
int UHF_WhitelistGetRaw(uint16_t index, uint8_t* outBuf, int outBufLen, int* outBytes);
int UHF_WhitelistGetHex(uint16_t index, char* outHex, int outHexLen, int* outBytes);
int UHF_WhitelistAddEpc(const char* epcHex);
int UHF_WhitelistRemoveEpc(const char* epcHex);
int UHF_WhitelistClear(void);
```

### Raw module command
```c
int UHF_ModuleCommand(uint8_t cmd, const uint8_t* payload, int payloadLen,
                      uint8_t* outBuf, int outBufLen, int* outRespLen);
```

## Vendor 1:1 (re-export)

All vendor `SWHid_*` functions are re-exported with identical names and
signatures. You can link to `UhfWrapper.dll` and call the vendor API without
loading `SWHidApi.dll` directly.

See:
- `src/uhf_wrapper/UhfWrapper.def`
- `src/uhf_wrapper/uhf_wrapper.h`
