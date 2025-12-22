#include "uhf_wrapper.h"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {
#if defined(_WIN32)
using LibHandle = HMODULE;
#else
using LibHandle = void*;
#endif

static LibHandle g_vendor = nullptr;
static int g_is_open = 0;
static char g_last_error[256] = "";

static void set_last_error(const char* msg) {
  if (!msg) {
    g_last_error[0] = '\0';
    return;
  }
#if defined(_MSC_VER)
  strncpy_s(g_last_error, msg, sizeof(g_last_error) - 1);
#else
  strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
  g_last_error[sizeof(g_last_error) - 1] = '\0';
#endif
}

static int ensure_vendor_loaded() {
  if (g_vendor) {
    return 1;
  }
#if defined(_WIN32)
  g_vendor = LoadLibraryA("SWHidApi.dll");
#else
  g_vendor = dlopen("SWHidApi.so", RTLD_LAZY);
#endif
  if (!g_vendor) {
    set_last_error("Failed to load vendor DLL (SWHidApi.dll)");
    return 0;
  }
  set_last_error("");
  return 1;
}

template <typename T>
static T load_fn(const char* name) {
  if (!ensure_vendor_loaded()) {
    return nullptr;
  }
#if defined(_WIN32)
  return reinterpret_cast<T>(GetProcAddress(g_vendor, name));
#else
  return reinterpret_cast<T>(dlsym(g_vendor, name));
#endif
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static int hex_to_bytes(const char* hex, uint8_t* out, int out_cap) {
  if (!hex || !out || out_cap <= 0) return -1;
  int len = 0;
  int hi = -1;
  for (const char* p = hex; *p; ++p) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') continue;
    int v = hex_val(*p);
    if (v < 0) return -1;
    if (hi < 0) {
      hi = v;
    } else {
      if (len >= out_cap) return -1;
      out[len++] = static_cast<uint8_t>((hi << 4) | v);
      hi = -1;
    }
  }
  if (hi >= 0) {
    return -1;
  }
  return len;
}

static void bytes_to_hex(const uint8_t* data, int len, char* out, int out_cap) {
  static const char* kHex = "0123456789ABCDEF";
  if (!out || out_cap <= 0) return;
  int needed = len * 2 + 1;
  if (out_cap < needed) {
    out[0] = '\0';
    return;
  }
  for (int i = 0; i < len; ++i) {
    out[i * 2] = kHex[(data[i] >> 4) & 0xF];
    out[i * 2 + 1] = kHex[data[i] & 0xF];
  }
  out[len * 2] = '\0';
}

static int rssi_to_dbm(int rssi_raw) {
  if (rssi_raw >= 0x63) {
    return -((255 - rssi_raw) / 2);
  }
  if (rssi_raw >= 0x5A) {
    return rssi_raw - 129;
  }
  return rssi_raw - 130;
}

static int parse_tag_buffer(const uint8_t* buf, int total_len, int dedup,
                            UHF_Tag* out_tags, int max_tags, int* out_count) {
  if (!out_count) return 0;
  *out_count = 0;
  if (!buf || total_len <= 0 || !out_tags || max_tags <= 0) {
    return 1;
  }

  int offset = 0;
  int count = 0;
  while (offset < total_len) {
    uint8_t pack_len = buf[offset];
    if (pack_len == 0) {
      offset += 1;
      continue;
    }
    if (offset + 1 + pack_len > total_len) {
      break;
    }

    uint8_t tag_type = buf[offset + 1];
    uint8_t antenna = buf[offset + 2];
    int has_ts = (tag_type & 0x80) != 0;

    int id_len = static_cast<int>(pack_len) - 1 - 2 - (has_ts ? 6 : 0);
    if (id_len < 0) {
      offset += pack_len + 1;
      continue;
    }

    const uint8_t* epc_ptr = buf + offset + 3;
    const uint8_t* rssi_ptr = epc_ptr + id_len;

    UHF_Tag tag{};
    tag.epcLenBytes = static_cast<uint8_t>(id_len);
    bytes_to_hex(epc_ptr, id_len, tag.epc, sizeof(tag.epc));
    tag.rssiDbm = rssi_to_dbm(*rssi_ptr);
    tag.antenna = antenna;
    tag.tagType = tag_type;
    tag.hasTs = static_cast<uint8_t>(has_ts ? 1 : 0);
    if (has_ts) {
      const uint8_t* ts_ptr = rssi_ptr + 1;
      for (int i = 0; i < UHF_TS_LEN; ++i) {
        tag.tsRaw[i] = ts_ptr[i];
      }
    }

    int is_dup = 0;
    if (dedup) {
      for (int i = 0; i < count; ++i) {
        if (strncmp(out_tags[i].epc, tag.epc, sizeof(tag.epc)) == 0) {
          is_dup = 1;
          break;
        }
      }
    }

    if (!is_dup && count < max_tags) {
      out_tags[count++] = tag;
    }

    offset += pack_len + 1;
  }

  *out_count = count;
  return 1;
}

} // namespace

extern "C" {

UHF_API int UHF_CALL UHF_Init(void) {
  return ensure_vendor_loaded();
}

UHF_API void UHF_CALL UHF_Shutdown(void) {
  if (g_vendor) {
#if defined(_WIN32)
    FreeLibrary(g_vendor);
#else
    dlclose(g_vendor);
#endif
    g_vendor = nullptr;
  }
  g_is_open = 0;
}

UHF_API const char* UHF_CALL UHF_GetLastError(void) {
  return g_last_error;
}

UHF_API int UHF_CALL UHF_GetUsbCount(void) {
  using Fn = int (UHF_CALL*)();
  auto fn = load_fn<Fn>("SWHid_GetUsbCount");
  if (!fn) {
    set_last_error("Missing SWHid_GetUsbCount");
    return -1;
  }
  return fn();
}

UHF_API int UHF_CALL UHF_GetUsbInfoRaw(uint16_t index, uint8_t* outBuf, int outBufLen) {
  using Fn = int (UHF_CALL*)(uint16_t, char*);
  auto fn = load_fn<Fn>("SWHid_GetUsbInfo");
  if (!fn) {
    set_last_error("Missing SWHid_GetUsbInfo");
    return 0;
  }
  if (!outBuf || outBufLen <= 0) {
    set_last_error("Invalid buffer");
    return 0;
  }
  return fn(index, reinterpret_cast<char*>(outBuf)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Open(uint16_t index) {
  using Fn = int (UHF_CALL*)(uint16_t);
  auto fn = load_fn<Fn>("SWHid_OpenDevice");
  if (!fn) {
    set_last_error("Missing SWHid_OpenDevice");
    return 0;
  }
  int ok = fn(index) ? 1 : 0;
  g_is_open = ok;
  return ok;
}

UHF_API int UHF_CALL UHF_Close(void) {
  using Fn = int (UHF_CALL*)();
  auto fn = load_fn<Fn>("SWHid_CloseDevice");
  if (!fn) {
    set_last_error("Missing SWHid_CloseDevice");
    return 0;
  }
  int ok = fn() ? 1 : 0;
  g_is_open = 0;
  return ok;
}

UHF_API int UHF_CALL UHF_IsReaderPresent(void) {
  int count = UHF_GetUsbCount();
  return count > 0 ? 1 : 0;
}

UHF_API int UHF_CALL UHF_IsOpen(void) {
  return g_is_open ? 1 : 0;
}

UHF_API int UHF_CALL UHF_IsConnected(void) {
  return (UHF_IsReaderPresent() && UHF_IsOpen()) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_GetInfo(UHF_DeviceInfo* outInfo) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_GetDeviceSystemInfo");
  if (!fn) {
    set_last_error("Missing SWHid_GetDeviceSystemInfo");
    return 0;
  }
  if (!outInfo) {
    set_last_error("Invalid outInfo");
    return 0;
  }
  uint8_t buf[16] = {0};
  if (!fn(0xFF, buf)) {
    set_last_error("SWHid_GetDeviceSystemInfo failed");
    return 0;
  }
  outInfo->softRaw = buf[0];
  outInfo->hardRaw = buf[1];
  outInfo->softMajor = static_cast<uint8_t>(buf[0] >> 4);
  outInfo->softMinor = static_cast<uint8_t>(buf[0] & 0x0F);
  outInfo->hardMajor = static_cast<uint8_t>(buf[1] >> 4);
  outInfo->hardMinor = static_cast<uint8_t>(buf[1] & 0x0F);

  char sn_hex[32] = {0};
  bytes_to_hex(buf + 2, 7, sn_hex, sizeof(sn_hex));
#if defined(_MSC_VER)
  strncpy_s(outInfo->sn, sn_hex, sizeof(outInfo->sn) - 1);
#else
  strncpy(outInfo->sn, sn_hex, sizeof(outInfo->sn) - 1);
  outInfo->sn[sizeof(outInfo->sn) - 1] = '\0';
#endif
  return 1;
}

UHF_API int UHF_CALL UHF_StartRead(void) {
  using Fn = int (UHF_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_StartRead");
  if (!fn) {
    set_last_error("Missing SWHid_StartRead");
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_StopRead(void) {
  using Fn = int (UHF_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_StopRead");
  if (!fn) {
    set_last_error("Missing SWHid_StopRead");
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_ClearBuffer(void) {
  using Fn = int (UHF_CALL*)();
  auto fn = load_fn<Fn>("SWHid_ClearTagBuf");
  if (!fn) {
    set_last_error("Missing SWHid_ClearTagBuf");
    return 0;
  }
  return fn() ? 1 : 0;
}

static int get_tag_buf(uint8_t* buf, int buf_len, int* out_len, int* out_count) {
  using Fn = uint8_t (UHF_CALL*)(uint8_t*, int*, int*);
  auto fn = load_fn<Fn>("SWHid_GetTagBuf");
  if (!fn) {
    set_last_error("Missing SWHid_GetTagBuf");
    return 0;
  }
  if (!buf || buf_len <= 0 || !out_len || !out_count) {
    set_last_error("Invalid buffer");
    return 0;
  }
  uint8_t ret = fn(buf, out_len, out_count);
  return ret != 0 ? 1 : 0;
}

static int peek_or_pop(int dedup, int do_clear, int safe_cycle,
                       UHF_Tag* outTags, int maxTags, int* outCount) {
  uint8_t buf[65536];
  int total_len = 0;
  int tag_num = 0;

  if (safe_cycle) {
    UHF_StopRead();
  }

  int ok = get_tag_buf(buf, sizeof(buf), &total_len, &tag_num);
  if (!ok) {
    if (outCount) *outCount = 0;
    if (safe_cycle) {
      UHF_StartRead();
    }
    return 0;
  }

  if (total_len <= 0 || tag_num <= 0) {
    if (outCount) *outCount = 0;
    if (do_clear) {
      UHF_ClearBuffer();
    }
    if (safe_cycle) {
      UHF_StartRead();
    }
    return 1;
  }

  ok = parse_tag_buffer(buf, total_len, dedup, outTags, maxTags, outCount);
  if (do_clear) {
    UHF_ClearBuffer();
  }
  if (safe_cycle) {
    UHF_StartRead();
  }
  return ok;
}

UHF_API int UHF_CALL UHF_PeekBufferAll(UHF_Tag* outTags, int maxTags, int* outCount) {
  return peek_or_pop(0, 0, 0, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_PeekBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount) {
  return peek_or_pop(1, 0, 0, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_PopBufferAll(UHF_Tag* outTags, int maxTags, int* outCount) {
  return peek_or_pop(0, 1, 0, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_PopBufferDedup(UHF_Tag* outTags, int maxTags, int* outCount) {
  return peek_or_pop(1, 1, 0, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_PopBufferAllSafe(UHF_Tag* outTags, int maxTags, int* outCount) {
  return peek_or_pop(0, 1, 1, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_PopBufferDedupSafe(UHF_Tag* outTags, int maxTags, int* outCount) {
  return peek_or_pop(1, 1, 1, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_GetPowerDbm(void) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadDeviceOneParam");
  if (!fn) {
    set_last_error("Missing SWHid_ReadDeviceOneParam");
    return -1;
  }
  uint8_t val = 0;
  if (!fn(0xFF, 0x05, &val)) {
    set_last_error("SWHid_ReadDeviceOneParam failed");
    return -1;
  }
  return static_cast<int>(val);
}

UHF_API int UHF_CALL UHF_SetPowerDbm(int dbm) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t, uint8_t);
  auto fn = load_fn<Fn>("SWHid_SetDeviceOneParam");
  if (!fn) {
    set_last_error("Missing SWHid_SetDeviceOneParam");
    return 0;
  }
  if (dbm < 0 || dbm > 26) {
    set_last_error("dbm out of range (0..26)");
    return 0;
  }
  return fn(0xFF, 0x05, static_cast<uint8_t>(dbm)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_GetPowerPct(void) {
  int dbm = UHF_GetPowerDbm();
  if (dbm < 0) return -1;
  int pct = static_cast<int>((dbm * 100.0) / 26.0 + 0.5);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

UHF_API int UHF_CALL UHF_SetPowerPct(int pct) {
  if (pct < 0 || pct > 100) {
    set_last_error("pct out of range (0..100)");
    return 0;
  }
  int dbm = static_cast<int>((pct * 26.0) / 100.0 + 0.5);
  return UHF_SetPowerDbm(dbm);
}

UHF_API int UHF_CALL UHF_RelayOn(void) {
  using Fn = int (UHF_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_RelayOn");
  if (!fn) {
    set_last_error("Missing SWHid_RelayOn");
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_RelayOff(void) {
  using Fn = int (UHF_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_RelayOff");
  if (!fn) {
    set_last_error("Missing SWHid_RelayOff");
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_GetFreq(uint8_t* outFreq2) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadFreq");
  if (!fn) {
    set_last_error("Missing SWHid_ReadFreq");
    return 0;
  }
  if (!outFreq2) {
    set_last_error("Invalid output buffer");
    return 0;
  }
  return fn(0xFF, outFreq2) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_SetFreq(uint8_t freq0, uint8_t freq1) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_SetFreq");
  if (!fn) {
    set_last_error("Missing SWHid_SetFreq");
    return 0;
  }
  uint8_t buf[2] = {freq0, freq1};
  return fn(0xFF, buf) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_ReadTag(uint8_t bank, uint8_t wordPtr, uint8_t wordCount,
                                 const char* pwdHex, uint8_t* outData, int outDataLen,
                                 int* outBytesRead) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t*, uint8_t, uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadCardG2");
  if (!fn) {
    set_last_error("Missing SWHid_ReadCardG2");
    return 0;
  }
  if (!outData || outDataLen <= 0 || !outBytesRead) {
    set_last_error("Invalid output buffer");
    return 0;
  }
  uint8_t pwd[4] = {0};
  if (pwdHex && pwdHex[0]) {
    if (hex_to_bytes(pwdHex, pwd, sizeof(pwd)) != 4) {
      set_last_error("Invalid password hex (need 8 hex chars)");
      return 0;
    }
  }
  int bytes_needed = static_cast<int>(wordCount) * 2;
  if (outDataLen < bytes_needed) {
    set_last_error("Output buffer too small");
    return 0;
  }
  int ok = fn(0xFF, pwd, bank, wordPtr, wordCount, outData) ? 1 : 0;
  if (ok) {
    *outBytesRead = bytes_needed;
  } else {
    *outBytesRead = 0;
  }
  return ok;
}

UHF_API int UHF_CALL UHF_WriteTag(uint8_t bank, uint8_t wordPtr,
                                  const uint8_t* data, int dataLenBytes,
                                  const char* pwdHex) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t*, uint8_t, uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_WriteCardG2");
  if (!fn) {
    set_last_error("Missing SWHid_WriteCardG2");
    return 0;
  }
  if (!data || dataLenBytes <= 0 || (dataLenBytes % 2) != 0) {
    set_last_error("Invalid data length (must be even, bytes)");
    return 0;
  }
  uint8_t pwd[4] = {0};
  if (pwdHex && pwdHex[0]) {
    if (hex_to_bytes(pwdHex, pwd, sizeof(pwd)) != 4) {
      set_last_error("Invalid password hex (need 8 hex chars)");
      return 0;
    }
  }
  uint8_t wordCount = static_cast<uint8_t>(dataLenBytes / 2);
  return fn(0xFF, pwd, bank, wordPtr, wordCount, const_cast<uint8_t*>(data)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_WriteEpc(const char* epcHex, const char* pwdHex) {
  using Fn = int (UHF_CALL*)(uint8_t, uint8_t*, uint8_t*, uint8_t);
  auto fn = load_fn<Fn>("SWHid_WriteEPCG2");
  if (!fn) {
    set_last_error("Missing SWHid_WriteEPCG2");
    return 0;
  }
  if (!epcHex || !epcHex[0]) {
    set_last_error("Invalid EPC hex");
    return 0;
  }
  uint8_t epc[UHF_MAX_EPC_BYTES] = {0};
  int epc_len = hex_to_bytes(epcHex, epc, sizeof(epc));
  if (epc_len <= 0) {
    set_last_error("Invalid EPC hex");
    return 0;
  }
  uint8_t pwd[4] = {0};
  if (pwdHex && pwdHex[0]) {
    if (hex_to_bytes(pwdHex, pwd, sizeof(pwd)) != 4) {
      set_last_error("Invalid password hex (need 8 hex chars)");
      return 0;
    }
  }
  return fn(0xFF, pwd, epc, static_cast<uint8_t>(epc_len)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_LockTag(uint8_t lockType, uint8_t lockMem, const char* pwdHex) {
  (void)lockType;
  (void)lockMem;
  (void)pwdHex;
  set_last_error("UHF_LockTag unsupported: vendor signature unknown. Use SWHid_LockCardG2 export directly.");
  return 0;
}

} // extern "C"
