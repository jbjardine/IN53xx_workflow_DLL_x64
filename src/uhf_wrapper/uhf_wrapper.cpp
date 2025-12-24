#include "uhf_wrapper.h"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <unistd.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32) && defined(_M_IX86)
#define VENDOR_CALL __cdecl
#else
#define VENDOR_CALL __stdcall
#endif

namespace {
#if defined(_WIN32)
using LibHandle = HMODULE;
#else
using LibHandle = void*;
#endif

static LibHandle g_vendor = nullptr;
static int g_is_open = 0;
static char g_last_error[256] = "";
static int g_last_error_code = UHF_ERR_OK;
static int g_dedup_window_ms = 0;
static int g_dedup_key_mode = 0; // 0 = EPC only, 1 = EPC + antenna
static std::unordered_map<std::string, int64_t> g_dedup_cache;
static std::mutex g_dedup_mutex;

static const uint8_t kMaskSelectTemplate[] = {
  0x53, 0x57, 0x00, 0xCD, 0xFF, 0x2F, 0xC3, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xE2,
  0x80, 0x68, 0x94, 0x00, 0x00, 0x40, 0x31, 0x9F, 0x66, 0x00, 0x03, 0x0C, 0x00, 0x03, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x01, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x54,
  0x0C,
};

// Special param payload (without initial flag). See SDK NewVersionChange.txt.
#pragma pack(push, 1)
struct SpecialParamV2 {
  uint8_t bPassword;
  uint8_t arrPass[4];
  uint8_t bMask;
  uint8_t bMaskStartAddr;
  uint8_t arrMask[12];
  uint8_t bMaskLength;
  uint8_t bRelay;
  uint8_t bRelayTime;
  uint8_t bCardCache;
  uint8_t bProtocol;
  uint8_t bProtocolSel;
  uint8_t bCondition;
  uint8_t bTemp;
  uint8_t arrParam1[64];
};
#pragma pack(pop)

template <typename T>
static T load_fn(const char* name);
static int hex_to_bytes(const char* hex, uint8_t* out, int out_cap);

static void set_last_error(const char* msg, int code) {
  if (!msg || msg[0] == '\0') {
    g_last_error[0] = '\0';
    g_last_error_code = UHF_ERR_OK;
    return;
  }
  g_last_error_code = code;
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
  char dll_path[512] = {0};
  DWORD n = GetEnvironmentVariableA("UHF_VENDOR_DLL", dll_path, sizeof(dll_path));
  if (n > 0 && n < sizeof(dll_path)) {
    g_vendor = LoadLibraryA(dll_path);
  } else {
    g_vendor = LoadLibraryA("SWHidApi.dll");
  }
#else
  g_vendor = dlopen("SWHidApi.so", RTLD_LAZY);
#endif
  if (!g_vendor) {
    set_last_error("Failed to load vendor DLL (SWHidApi.dll)", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  set_last_error("", UHF_ERR_OK);
  return 1;
}

static void sleep_ms(int ms) {
  if (ms <= 0) return;
#if defined(_WIN32)
  Sleep(static_cast<DWORD>(ms));
#else
  usleep(static_cast<useconds_t>(ms * 1000));
#endif
}

static int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static std::string build_dedup_key(const UHF_Tag& tag) {
  std::string key = tag.epc;
  if (g_dedup_key_mode == 1) {
    key.push_back('#');
    key += std::to_string(tag.antenna);
  }
  return key;
}

static void dedup_cache_reset_locked() {
  g_dedup_cache.clear();
}

static void finalize_checksum(uint8_t* buf, int len) {
  if (!buf || len <= 0) return;
  buf[len - 1] = 0;
  uint8_t sum = 0;
  for (int i = 0; i < len - 1; ++i) {
    sum = static_cast<uint8_t>(sum + buf[i]);
  }
  buf[len - 1] = static_cast<uint8_t>(0 - sum);
}

static int send_buffer(const uint8_t* buf, int len) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint16_t);
  auto fn = load_fn<Fn>("SWHid_SendBuffer");
  if (!fn) {
    set_last_error("Missing SWHid_SendBuffer", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!buf || len <= 0 || len > 0xFFFF) {
    set_last_error("Invalid buffer length", UHF_ERR_INVALID_ARG);
    return 0;
  }
  return fn(0xFF, const_cast<uint8_t*>(buf), static_cast<uint16_t>(len)) ? 1 : 0;
}

static int read_special_param(uint8_t* out_buf, uint8_t out_len) {
  if (!out_buf || out_len == 0) return 0;
#if defined(_WIN64)
  using FnRead = int (VENDOR_CALL*)(void*, uint8_t*, uint8_t*, uint8_t);
  auto fn = load_fn<FnRead>("SWHid_ReadDeviceSpecialParam");
  if (!fn) return 0;
  uint8_t dev = 0;
  return fn(nullptr, &dev, out_buf, out_len) ? 1 : 0;
#else
  using FnRead = int (VENDOR_CALL*)(uint8_t*, uint8_t*, uint8_t);
  auto fn = load_fn<FnRead>("SWHid_ReadDeviceSpecialParam");
  if (!fn) return 0;
  uint8_t dev = 0;
  return fn(&dev, out_buf, out_len) ? 1 : 0;
#endif
}

static int write_special_param(const uint8_t* buf, uint8_t len) {
  if (!buf || len == 0) return 0;
#if defined(_WIN64)
  using FnSet = int (VENDOR_CALL*)(void*, uint8_t, const void*, uint8_t);
  auto fn = load_fn<FnSet>("SWHid_SetDeviceSpecialParam");
  if (!fn) return 0;
  return fn(nullptr, 0xFF, buf, len) ? 1 : 0;
#else
  using FnSet = int (VENDOR_CALL*)(uint8_t, const void*, uint8_t);
  auto fn = load_fn<FnSet>("SWHid_SetDeviceSpecialParam");
  if (!fn) return 0;
  return fn(0xFF, buf, len) ? 1 : 0;
#endif
}

static int send_module_cmd(uint8_t cmd, const uint8_t* payload, int payload_len,
                           uint8_t* out_buf, int out_buf_len, int* out_resp_len) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint16_t, uint8_t*, uint16_t*);
  auto fn = load_fn<Fn>("SWHid_CommunicateWithModule");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_CommunicateWithModule missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  if (payload_len < 0 || payload_len > 0xFFFF) {
    set_last_error("Invalid payload length", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (payload_len > 0 && !payload) {
    set_last_error("Invalid payload buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t tmp_resp[1024];
  uint16_t resp_len = 0;
  uint8_t* resp_buf = out_buf;
  int resp_cap = out_buf_len;
  if (!resp_buf || resp_cap <= 0) {
    resp_buf = tmp_resp;
    resp_cap = static_cast<int>(sizeof(tmp_resp));
  }
  if (resp_cap > 0xFFFF) {
    resp_cap = 0xFFFF;
  }
  uint16_t cap16 = static_cast<uint16_t>(resp_cap);
  int ok = fn(cmd,
              const_cast<uint8_t*>(payload ? payload : reinterpret_cast<const uint8_t*>("")),
              static_cast<uint16_t>(payload_len),
              resp_buf,
              &resp_len);
  if (!ok) {
    set_last_error("SWHid_CommunicateWithModule failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (out_resp_len) {
    *out_resp_len = static_cast<int>(resp_len);
  }
  if (out_buf && out_buf_len > 0 && out_buf != resp_buf) {
    int to_copy = static_cast<int>(resp_len);
    if (to_copy > out_buf_len) to_copy = out_buf_len;
    if (to_copy > 0) {
      memcpy(out_buf, resp_buf, static_cast<size_t>(to_copy));
    }
  }
  return 1;
}

static int build_mask_frame(const uint8_t* epc, int epc_len, int enable, uint8_t* out, int out_len) {
  const int tmpl_len = static_cast<int>(sizeof(kMaskSelectTemplate));
  if (!out || out_len < tmpl_len) {
    return 0;
  }
  memcpy(out, kMaskSelectTemplate, tmpl_len);
  // Enable flag (observed in ReaderSoft log).
  out[13] = enable ? 0x01 : 0x00;
  // bPassword stays OFF for mask-only selection (ReaderSoft log shows 0x00).
  out[8] = 0x00;
  out[9] = 0x00;
  out[10] = 0x00;
  out[11] = 0x00;
  out[12] = 0x00;
  // bCondition (crypt mode): try Pass | Mask for access ops.
  out[33] = enable ? 0x01 : 0x00;
  // Start(Hex) observed as 0x00 in log.
  out[14] = 0x00;

  if (enable) {
    if (!epc || epc_len <= 0 || epc_len > 12) {
      return 0;
    }
    memcpy(out + 15, epc, epc_len);
    if (epc_len < 12) {
      memset(out + 15 + epc_len, 0x00, 12 - epc_len);
    }
    out[27] = static_cast<uint8_t>(epc_len);
  } else {
    memset(out + 15, 0x00, 12);
    out[27] = 0x00;
  }

  finalize_checksum(out, tmpl_len);
  return 1;
}

static int set_mask_special_param(const uint8_t* epc, int epc_len, int enable) {
  SpecialParamV2 p{};
  if (!read_special_param(reinterpret_cast<uint8_t*>(&p), static_cast<uint8_t>(sizeof(p)))) {
    memset(&p, 0, sizeof(p));
  }
  p.bPassword = 0x00;
  memset(p.arrPass, 0x00, sizeof(p.arrPass));
  p.bMask = enable ? 0x01 : 0x00;
  p.bMaskStartAddr = 0x00;
  if (enable) {
    if (!epc || epc_len <= 0 || epc_len > 12) {
      return 0;
    }
    memcpy(p.arrMask, epc, epc_len);
    if (epc_len < 12) {
      memset(p.arrMask + epc_len, 0x00, 12 - epc_len);
    }
    p.bMaskLength = static_cast<uint8_t>(epc_len);
    p.bCondition = 0x02; // Pass & Mask (strict)
  } else {
    memset(p.arrMask, 0x00, 12);
    p.bMaskLength = 0x00;
    p.bCondition = 0x00;
  }
  return write_special_param(reinterpret_cast<const uint8_t*>(&p), static_cast<uint8_t>(sizeof(p)));
}

static int count_tags_once(int timeout_ms, int* out_count) {
  if (!out_count) return 0;
  *out_count = 0;
  if (!UHF_StartRead()) {
    return 0;
  }
  if (timeout_ms < 0) timeout_ms = 0;
  sleep_ms(timeout_ms);
  UHF_StopRead();
  UHF_Tag tags[256];
  int count = 0;
  if (!UHF_PopBufferDedup(tags, 256, &count)) {
    return 0;
  }
  *out_count = count;
  return 1;
}

static int epc_hex_equal(const char* a, const char* b) {
  if (!a || !b) return 0;
  const char* pa = a;
  const char* pb = b;
  for (;;) {
    while (*pa == ' ' || *pa == '\t' || *pa == '\n' || *pa == '\r') ++pa;
    while (*pb == ' ' || *pb == '\t' || *pb == '\n' || *pb == '\r') ++pb;
    if (*pa == '\0' || *pb == '\0') {
      return (*pa == '\0' && *pb == '\0') ? 1 : 0;
    }
    if (tolower(static_cast<unsigned char>(*pa)) != tolower(static_cast<unsigned char>(*pb))) {
      return 0;
    }
    ++pa;
    ++pb;
  }
}

static int inventory_has_epc(const char* epcHex, int timeout_ms) {
  if (!epcHex || !epcHex[0]) return 0;
  if (timeout_ms < 0) timeout_ms = 0;
  if (!UHF_StartRead()) {
    return 0;
  }
  int elapsed = 0;
  int found = 0;
  int interval = 50;
  if (timeout_ms > 0 && interval > timeout_ms) interval = timeout_ms;
  while (timeout_ms == 0 || elapsed < timeout_ms) {
    sleep_ms(interval);
    elapsed += interval;
    UHF_Tag tags[256];
    int count = 0;
    if (!UHF_PopBufferDedup(tags, 256, &count)) {
      continue;
    }
    for (int i = 0; i < count; ++i) {
      if (epc_hex_equal(tags[i].epc, epcHex)) {
        found = 1;
        break;
      }
    }
    if (found) break;
    if (timeout_ms == 0) break;
  }
  UHF_StopRead();
  return found;
}

static int verify_tag_read(uint8_t bank, uint8_t wordPtr, const uint8_t* expected,
                           int expected_len, const char* pwdHex) {
  if (!expected || expected_len <= 0) return 0;
  uint8_t buf[512];
  int bytesRead = 0;
  uint8_t wordCount = static_cast<uint8_t>(expected_len / 2);
  if ((expected_len % 2) != 0) {
    return 0;
  }
  int ok = UHF_ReadTag(bank, wordPtr, wordCount, pwdHex, buf, sizeof(buf), &bytesRead);
  if (!ok || bytesRead < expected_len) {
    return 0;
  }
  return memcmp(buf, expected, expected_len) == 0 ? 1 : 0;
}

static int write_tag_raw(uint8_t bank, uint8_t wordPtr,
                         const uint8_t* data, int dataLenBytes,
                         const char* pwdHex) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t, uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_WriteCardG2");
  if (!fn) {
    set_last_error("Missing SWHid_WriteCardG2", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!data || dataLenBytes <= 0 || (dataLenBytes % 2) != 0) {
    set_last_error("Invalid data length (must be even, bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t pwd[4] = {0};
  if (pwdHex && pwdHex[0]) {
    if (hex_to_bytes(pwdHex, pwd, sizeof(pwd)) != 4) {
      set_last_error("Invalid password hex (need 8 hex chars)", UHF_ERR_INVALID_ARG);
      return 0;
    }
  }
  uint8_t wordCount = static_cast<uint8_t>(dataLenBytes / 2);
  int ok = fn(0xFF, pwd, bank, wordPtr, wordCount, const_cast<uint8_t*>(data)) ? 1 : 0;
  if (!ok) {
    set_last_error("Write tag failed (WriteCardG2)", UHF_ERR_VENDOR_CALL_FAILED);
  }
  return ok;
}

static int write_epc_raw(const char* epcHex, const char* pwdHex,
                         uint8_t* out_epc, int* out_epc_len) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t*, uint8_t);
  using FnWrite = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t, uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_WriteEPCG2");
  if (!fn) {
    set_last_error("Missing SWHid_WriteEPCG2", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!epcHex || !epcHex[0]) {
    set_last_error("Invalid EPC hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t epc[UHF_MAX_EPC_BYTES] = {0};
  int epc_len = hex_to_bytes(epcHex, epc, sizeof(epc));
  if (epc_len <= 0 || (epc_len % 2) != 0) {
    set_last_error("Invalid EPC length (must be even bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t pwd[4] = {0};
  if (pwdHex && pwdHex[0]) {
    if (hex_to_bytes(pwdHex, pwd, sizeof(pwd)) != 4) {
      set_last_error("Invalid password hex (need 8 hex chars)", UHF_ERR_INVALID_ARG);
      return 0;
    }
  }
  // ReaderSoft expects EPC length in 16-bit words.
  uint8_t word_len = static_cast<uint8_t>(epc_len / 2);
  int ok = fn(0xFF, pwd, epc, word_len) ? 1 : 0;
  if (!ok) {
    sleep_ms(40);
    ok = fn(0xFF, pwd, epc, word_len) ? 1 : 0;
  }
  if (!ok && (epc_len % 2) == 0) {
    // Fallback: write EPC memory directly (bank EPC, wordPtr=2).
    auto fn_write = load_fn<FnWrite>("SWHid_WriteCardG2");
    if (fn_write && word_len > 0) {
      ok = fn_write(0xFF, pwd, 1, 2, word_len, epc) ? 1 : 0;
    }
  }
  if (!ok) {
    set_last_error("EPC write failed (WriteEPCG2/WriteCardG2)", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (out_epc && out_epc_len) {
    memcpy(out_epc, epc, static_cast<size_t>(epc_len));
    *out_epc_len = epc_len;
  }
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
    if (hi < 0 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
      ++p;
      continue;
    }
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

UHF_API int UHF_CALL UHF_GetLastErrorCode(void) {
  return g_last_error_code;
}

UHF_API int UHF_CALL UHF_GetUsbCount(void) {
  using Fn = int (VENDOR_CALL*)();
  auto fn = load_fn<Fn>("SWHid_GetUsbCount");
  if (!fn) {
    set_last_error("Missing SWHid_GetUsbCount", UHF_ERR_VENDOR_MISSING);
    return -1;
  }
  return fn();
}

UHF_API int UHF_CALL UHF_GetUsbInfoRaw(uint16_t index, uint8_t* outBuf, int outBufLen) {
  using Fn = int (VENDOR_CALL*)(uint16_t, char*);
  auto fn = load_fn<Fn>("SWHid_GetUsbInfo");
  if (!fn) {
    set_last_error("Missing SWHid_GetUsbInfo", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!outBuf || outBufLen <= 0) {
    set_last_error("Invalid buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  return fn(index, reinterpret_cast<char*>(outBuf)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Open(uint16_t index) {
  using FnOpen = int (VENDOR_CALL*)(uint16_t);
  using FnCount = int (VENDOR_CALL*)();
  using FnInfo = int (VENDOR_CALL*)(uint16_t, char*);
  auto fn_open = load_fn<FnOpen>("SWHid_OpenDevice");
  if (!fn_open) {
    set_last_error("Missing SWHid_OpenDevice", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  // Some vendor DLLs require GetUsbCount/GetUsbInfo before OpenDevice.
  auto fn_count = load_fn<FnCount>("SWHid_GetUsbCount");
  if (fn_count) {
    int count = fn_count();
    if (count <= 0) {
      set_last_error("No USB devices found", UHF_ERR_NO_DEVICE);
      return 0;
    }
    if (index >= static_cast<uint16_t>(count)) {
      set_last_error("USB index out of range", UHF_ERR_INVALID_ARG);
      return 0;
    }
  }
  auto fn_info = load_fn<FnInfo>("SWHid_GetUsbInfo");
  if (fn_info) {
    char buf[512] = {0};
    fn_info(index, buf);
  }
  int ok = fn_open(index) ? 1 : 0;
  g_is_open = ok;
  if (!ok) {
    set_last_error("SWHid_OpenDevice failed", UHF_ERR_VENDOR_CALL_FAILED);
  }
  return ok;
}

UHF_API int UHF_CALL UHF_Close(void) {
  using Fn = int (VENDOR_CALL*)();
  auto fn = load_fn<Fn>("SWHid_CloseDevice");
  if (!fn) {
    set_last_error("Missing SWHid_CloseDevice", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  int ok = fn() ? 1 : 0;
  g_is_open = 0;
  if (!ok) {
    set_last_error("SWHid_CloseDevice failed", UHF_ERR_VENDOR_CALL_FAILED);
  }
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
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_GetDeviceSystemInfo");
  if (!fn) {
    set_last_error("Missing SWHid_GetDeviceSystemInfo", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!outInfo) {
    set_last_error("Invalid outInfo", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t buf[16] = {0};
  if (!fn(0xFF, buf)) {
    set_last_error("SWHid_GetDeviceSystemInfo failed", UHF_ERR_VENDOR_CALL_FAILED);
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
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_StartRead");
  if (!fn) {
    set_last_error("Missing SWHid_StartRead", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_StopRead(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_StopRead");
  if (!fn) {
    set_last_error("Missing SWHid_StopRead", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_ClearBuffer(void) {
  using Fn = int (VENDOR_CALL*)();
  auto fn = load_fn<Fn>("SWHid_ClearTagBuf");
  if (!fn) {
    set_last_error("Missing SWHid_ClearTagBuf", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  int ok = fn() ? 1 : 0;
  if (ok) {
    std::lock_guard<std::mutex> lock(g_dedup_mutex);
    dedup_cache_reset_locked();
  }
  return ok;
}

static int get_tag_buf(uint8_t* buf, int buf_len, int* out_len, int* out_count) {
  using Fn = uint8_t (VENDOR_CALL*)(uint8_t*, int*, int*);
  auto fn = load_fn<Fn>("SWHid_GetTagBuf");
  if (!fn) {
    set_last_error("Missing SWHid_GetTagBuf", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!buf || buf_len <= 0 || !out_len || !out_count) {
    set_last_error("Invalid buffer", UHF_ERR_INVALID_ARG);
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

UHF_API int UHF_CALL UHF_DedupWindowSet(int windowMs) {
  if (windowMs < 0) {
    set_last_error("windowMs must be >= 0", UHF_ERR_INVALID_ARG);
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  g_dedup_window_ms = windowMs;
  if (windowMs == 0) {
    dedup_cache_reset_locked();
  }
  return 1;
}

UHF_API int UHF_CALL UHF_DedupWindowReset(void) {
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  dedup_cache_reset_locked();
  return 1;
}

UHF_API int UHF_CALL UHF_DedupKeySet(int mode) {
  if (mode != 0 && mode != 1) {
    set_last_error("mode must be 0 (EPC) or 1 (EPC+antenna)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  g_dedup_key_mode = mode;
  dedup_cache_reset_locked();
  return 1;
}

UHF_API int UHF_CALL UHF_PopBufferDedupFiltered(UHF_Tag* outTags, int maxTags, int* outCount) {
  if (!outTags || maxTags <= 0 || !outCount) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int window_ms = 0;
  {
    std::lock_guard<std::mutex> lock(g_dedup_mutex);
    window_ms = g_dedup_window_ms;
  }
  if (window_ms <= 0) {
    return UHF_PopBufferDedup(outTags, maxTags, outCount);
  }
  UHF_Tag tmp[256];
  int cap = maxTags;
  if (cap > static_cast<int>(sizeof(tmp) / sizeof(tmp[0]))) {
    cap = static_cast<int>(sizeof(tmp) / sizeof(tmp[0]));
  }
  int count = 0;
  if (!UHF_PopBufferDedup(tmp, cap, &count)) {
    return 0;
  }
  int64_t now = now_ms();
  int out = 0;
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  for (int i = 0; i < count && out < maxTags; ++i) {
    std::string key = build_dedup_key(tmp[i]);
    auto it = g_dedup_cache.find(key);
    if (it != g_dedup_cache.end()) {
      int64_t age = now - it->second;
      if (age < window_ms) {
        continue;
      }
    }
    outTags[out++] = tmp[i];
    g_dedup_cache[key] = now;
  }
  // Purge expired entries to keep cache bounded.
  for (auto it = g_dedup_cache.begin(); it != g_dedup_cache.end(); ) {
    if ((now - it->second) >= window_ms) {
      it = g_dedup_cache.erase(it);
    } else {
      ++it;
    }
  }
  *outCount = out;
  return 1;
}

UHF_API int UHF_CALL UHF_ReadOnce(int timeoutMs, UHF_Tag* outTags, int maxTags, int* outCount) {
  if (!outTags || maxTags <= 0 || !outCount) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (timeoutMs < 0) timeoutMs = 0;
  if (!UHF_ClearBuffer()) {
    set_last_error("ClearBuffer failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (!UHF_StartRead()) {
    set_last_error("StartRead failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  sleep_ms(timeoutMs);
  UHF_StopRead();
  return UHF_PopBufferDedupFiltered(outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_GetPowerDbm(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadDeviceOneParam");
  if (!fn) {
    set_last_error("Missing SWHid_ReadDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return -1;
  }
  uint8_t val = 0;
  if (!fn(0xFF, 0x05, &val)) {
    set_last_error("SWHid_ReadDeviceOneParam failed", UHF_ERR_VENDOR_CALL_FAILED);
    return -1;
  }
  return static_cast<int>(val);
}

UHF_API int UHF_CALL UHF_SetPowerDbm(int dbm) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t);
  auto fn = load_fn<Fn>("SWHid_SetDeviceOneParam");
  if (!fn) {
    set_last_error("Missing SWHid_SetDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (dbm < 0 || dbm > 26) {
    set_last_error("dbm out of range (0..26)", UHF_ERR_INVALID_ARG);
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
    set_last_error("pct out of range (0..100)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int dbm = static_cast<int>((pct * 26.0) / 100.0 + 0.5);
  return UHF_SetPowerDbm(dbm);
}

UHF_API int UHF_CALL UHF_RelayOn(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_RelayOn");
  if (!fn) {
    set_last_error("Missing SWHid_RelayOn", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_RelayOff(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_RelayOff");
  if (!fn) {
    set_last_error("Missing SWHid_RelayOff", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Relay2On(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_Relay2On");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_Relay2On missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Relay2Off(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_Relay2Off");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_Relay2Off missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Out1On(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_Out1On");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_Out1On missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Out1Off(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_Out1Off");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_Out1Off missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Out2On(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_Out2On");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_Out2On missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_Out2Off(void) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_Out2Off");
  if (!fn) {
    set_last_error("Not supported by vendor DLL (SWHid_Out2Off missing)", UHF_ERR_NOT_SUPPORTED);
    return 0;
  }
  return fn(0xFF) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_GetFreq(uint8_t* outFreq2) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadFreq");
  if (!fn) {
    set_last_error("Missing SWHid_ReadFreq", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!outFreq2) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  return fn(0xFF, outFreq2) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_SetFreq(uint8_t freq0, uint8_t freq1) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_SetFreq");
  if (!fn) {
    set_last_error("Missing SWHid_SetFreq", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  uint8_t buf[2] = {freq0, freq1};
  return fn(0xFF, buf) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_ReadTag(uint8_t bank, uint8_t wordPtr, uint8_t wordCount,
                                 const char* pwdHex, uint8_t* outData, int outDataLen,
                                 int* outBytesRead) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t, uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadCardG2");
  if (!fn) {
    set_last_error("Missing SWHid_ReadCardG2", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!outData || outDataLen <= 0 || !outBytesRead) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t pwd[4] = {0};
  if (pwdHex && pwdHex[0]) {
    if (hex_to_bytes(pwdHex, pwd, sizeof(pwd)) != 4) {
      set_last_error("Invalid password hex (need 8 hex chars)", UHF_ERR_INVALID_ARG);
      return 0;
    }
  }
  int bytes_needed = static_cast<int>(wordCount) * 2;
  if (outDataLen < bytes_needed) {
    set_last_error("Output buffer too small", UHF_ERR_INVALID_ARG);
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
  if (!data || dataLenBytes <= 0 || (dataLenBytes % 2) != 0) {
    set_last_error("Invalid data length (must be even, bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int count = 0;
  if (!count_tags_once(200, &count)) {
    set_last_error("Failed to read tags for safety check", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (count > 1) {
    set_last_error("Multiple tags detected; use selected write", UHF_ERR_MULTI_TAG);
    return 0;
  }
  if (!write_tag_raw(bank, wordPtr, data, dataLenBytes, pwdHex)) {
    return 0;
  }
  if (!verify_tag_read(bank, wordPtr, data, dataLenBytes, pwdHex)) {
    set_last_error("Write verify failed", UHF_ERR_VERIFY_FAILED);
    return 0;
  }
  return 1;
}

UHF_API int UHF_CALL UHF_WriteEpc(const char* epcHex, const char* pwdHex) {
  int count = 0;
  if (!count_tags_once(200, &count)) {
    set_last_error("Failed to read tags for safety check", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (count > 1) {
    set_last_error("Multiple tags detected; use selected write", UHF_ERR_MULTI_TAG);
    return 0;
  }
  uint8_t epc[UHF_MAX_EPC_BYTES] = {0};
  int epc_len = 0;
  if (!write_epc_raw(epcHex, pwdHex, epc, &epc_len)) {
    return 0;
  }
  if (!verify_tag_read(1, 2, epc, epc_len, pwdHex)) {
    set_last_error("EPC verify failed", UHF_ERR_VERIFY_FAILED);
    return 0;
  }
  return 1;
}

UHF_API int UHF_CALL UHF_WriteEpcSelected(const char* targetEpcHex, const char* newEpcHex,
                                          const char* pwdHex, int forceMulti) {
  if (!targetEpcHex || !targetEpcHex[0] || !newEpcHex || !newEpcHex[0]) {
    set_last_error("Invalid target/new EPC hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!forceMulti) {
    int count = 0;
    if (!count_tags_once(200, &count)) {
      set_last_error("Failed to read tags for safety check", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    if (count > 1) {
      set_last_error("Multiple tags detected; use force to override", UHF_ERR_MULTI_TAG);
      return 0;
    }
  }
  int sel_ok = UHF_SelectEpc(targetEpcHex);
  if (!sel_ok) {
    set_last_error("Select EPC failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  uint8_t epc[UHF_MAX_EPC_BYTES] = {0};
  int epc_len = 0;
  int ok = write_epc_raw(newEpcHex, pwdHex, epc, &epc_len);
  int verify_ok = 0;
  UHF_ClearSelect();
  if (ok) {
    if (UHF_SelectEpc(newEpcHex)) {
      verify_ok = verify_tag_read(1, 2, epc, epc_len, pwdHex);
    }
    UHF_ClearSelect();
    if (!verify_ok) {
      verify_ok = inventory_has_epc(newEpcHex, 200);
    }
  }
  if (ok && !verify_ok) {
    set_last_error("EPC verify failed", UHF_ERR_VERIFY_FAILED);
    ok = 0;
  }
  return ok;
}

UHF_API int UHF_CALL UHF_WriteTagSelected(const char* targetEpcHex, uint8_t bank, uint8_t wordPtr,
                                          const uint8_t* data, int dataLenBytes,
                                          const char* pwdHex, int forceMulti) {
  if (!targetEpcHex || !targetEpcHex[0]) {
    set_last_error("Invalid target EPC hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!data || dataLenBytes <= 0 || (dataLenBytes % 2) != 0) {
    set_last_error("Invalid data length (must be even, bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!forceMulti) {
    int count = 0;
    if (!count_tags_once(200, &count)) {
      set_last_error("Failed to read tags for safety check", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    if (count > 1) {
      set_last_error("Multiple tags detected; use force to override", UHF_ERR_MULTI_TAG);
      return 0;
    }
  }
  int sel_ok = UHF_SelectEpc(targetEpcHex);
  if (!sel_ok) {
    set_last_error("Select EPC failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  int ok = write_tag_raw(bank, wordPtr, data, dataLenBytes, pwdHex);
  if (ok && !verify_tag_read(bank, wordPtr, data, dataLenBytes, pwdHex)) {
    set_last_error("Write verify failed", UHF_ERR_VERIFY_FAILED);
    ok = 0;
  }
  UHF_ClearSelect();
  return ok;
}

UHF_API int UHF_CALL UHF_SelectEpc(const char* epcHex) {
  if (!epcHex || !epcHex[0]) {
    set_last_error("Invalid EPC hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t epc[12] = {0};
  int epc_len = hex_to_bytes(epcHex, epc, sizeof(epc));
  if (epc_len <= 0 || epc_len > 12) {
    set_last_error("Select EPC requires 1..12 bytes (24 hex chars for 96-bit EPC)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int sp_ok = set_mask_special_param(epc, epc_len, 1);
  uint8_t frame[sizeof(kMaskSelectTemplate)] = {0};
  if (!build_mask_frame(epc, epc_len, 1, frame, sizeof(frame))) {
    set_last_error("Failed to build mask frame", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int frame_ok = send_buffer(frame, static_cast<int>(sizeof(frame)));
  if (sp_ok || frame_ok) {
    return 1;
  }
  set_last_error("Mask select failed", UHF_ERR_VENDOR_CALL_FAILED);
  return 0;
}

UHF_API int UHF_CALL UHF_ClearSelect(void) {
  int sp_ok = set_mask_special_param(nullptr, 0, 0);
  uint8_t frame[sizeof(kMaskSelectTemplate)] = {0};
  if (!build_mask_frame(nullptr, 0, 0, frame, sizeof(frame))) {
    set_last_error("Failed to build clear mask frame", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int frame_ok = send_buffer(frame, static_cast<int>(sizeof(frame)));
  if (sp_ok || frame_ok) {
    return 1;
  }
  set_last_error("Mask clear failed", UHF_ERR_VENDOR_CALL_FAILED);
  return 0;
}

static int parse_pwd_hex(const char* pwdHex, uint8_t* out4) {
  if (!out4) return 0;
  if (!pwdHex || !pwdHex[0]) {
    memset(out4, 0, 4);
    return 1;
  }
  uint8_t tmp[4] = {0};
  int len = hex_to_bytes(pwdHex, tmp, 4);
  if (len != 4) {
    return 0;
  }
  memcpy(out4, tmp, 4);
  return 1;
}

UHF_API int UHF_CALL UHF_LockTag(uint8_t lockType, uint8_t lockMem, const char* pwdHex) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t);
  auto fn = load_fn<Fn>("SWHid_LockCardG2");
  if (!fn) {
    set_last_error("Missing SWHid_LockCardG2", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (lockType > 0x0F || lockMem > 0x0F) {
    set_last_error("lockType/lockMem out of range (0..15)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t pwd[4];
  if (!parse_pwd_hex(pwdHex, pwd)) {
    set_last_error("Invalid password hex (expected 4 bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t lockCfg = static_cast<uint8_t>((lockMem << 4) | (lockType & 0x0F));
  return fn(0xFF, pwd, lockCfg) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_WhitelistCount(int* outCount) {
  if (!outCount) {
    set_last_error("Invalid output pointer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  using Fn = int (VENDOR_CALL*)(uint8_t, uint16_t*);
  auto fn = load_fn<Fn>("SWHid_ReadWhiteListCnt");
  if (fn) {
    uint16_t cnt = 0;
    if (!fn(0xFF, &cnt)) {
      set_last_error("SWHid_ReadWhiteListCnt failed", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    *outCount = static_cast<int>(cnt);
    return 1;
  }
  // Fallback for SDKs that don't export ReadWhiteListCnt: enumerate until failure.
  using FnRead = int (VENDOR_CALL*)(uint8_t, uint16_t, int*);
  auto read_fn = load_fn<FnRead>("SWHid_ReadWhiteList");
  if (!read_fn) {
    set_last_error("Missing SWHid_ReadWhiteList", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  int count = 0;
  int tmp[8];
  for (int i = 0; i < 4096; ++i) {
    if (!read_fn(0xFF, static_cast<uint16_t>(i), tmp)) {
      set_last_error("", UHF_ERR_OK);
      break;
    }
    ++count;
  }
  *outCount = count;
  return 1;
}

UHF_API int UHF_CALL UHF_WhitelistGetRaw(uint16_t index, uint8_t* outBuf, int outBufLen, int* outBytes) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint16_t, int*);
  auto fn = load_fn<Fn>("SWHid_ReadWhiteList");
  if (!fn) {
    set_last_error("Missing SWHid_ReadWhiteList", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!outBuf || outBufLen < UHF_WHITELIST_ENTRY_BYTES) {
    set_last_error("Output buffer too small (need 32 bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int tmp[8];
  if (!fn(0xFF, index, tmp)) {
    set_last_error("SWHid_ReadWhiteList failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  memcpy(outBuf, tmp, UHF_WHITELIST_ENTRY_BYTES);
  if (outBytes) {
    *outBytes = UHF_WHITELIST_ENTRY_BYTES;
  }
  return 1;
}

UHF_API int UHF_CALL UHF_WhitelistGetHex(uint16_t index, char* outHex, int outHexLen, int* outBytes) {
  uint8_t raw[UHF_WHITELIST_ENTRY_BYTES];
  int bytes = 0;
  if (!UHF_WhitelistGetRaw(index, raw, sizeof(raw), &bytes)) {
    return 0;
  }
  if (!outHex || outHexLen <= 0) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (outHexLen < bytes * 2 + 1) {
    set_last_error("Output buffer too small for hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  bytes_to_hex(raw, bytes, outHex, outHexLen);
  if (outBytes) {
    *outBytes = bytes;
  }
  return 1;
}

UHF_API int UHF_CALL UHF_WhitelistAddEpc(const char* epcHex) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t);
  auto fn = load_fn<Fn>("SWHid_WriteWhiteList");
  if (!fn) {
    set_last_error("Missing SWHid_WriteWhiteList", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!epcHex || !epcHex[0]) {
    set_last_error("Invalid EPC hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t buf[UHF_WHITELIST_ENTRY_BYTES];
  memset(buf, 0, sizeof(buf));
  int len = hex_to_bytes(epcHex, buf, sizeof(buf));
  if (len <= 0 || len > UHF_WHITELIST_ENTRY_BYTES) {
    set_last_error("EPC too long (max 32 bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  return fn(0xFF, buf, static_cast<uint8_t>(len)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_WhitelistRemoveEpc(const char* epcHex) {
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t);
  auto fn = load_fn<Fn>("SWHid_DelOneWhiteList");
  if (!fn) {
    fn = load_fn<Fn>("SWHid_DeleteOneWhiteList");
  }
  if (!fn) {
    set_last_error("Missing SWHid_DelOneWhiteList/SWHid_DeleteOneWhiteList", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!epcHex || !epcHex[0]) {
    set_last_error("Invalid EPC hex", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t buf[UHF_WHITELIST_ENTRY_BYTES];
  memset(buf, 0, sizeof(buf));
  int len = hex_to_bytes(epcHex, buf, sizeof(buf));
  if (len <= 0 || len > UHF_WHITELIST_ENTRY_BYTES) {
    set_last_error("EPC too long (max 32 bytes)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  return fn(0xFF, buf, static_cast<uint8_t>(len)) ? 1 : 0;
}

UHF_API int UHF_CALL UHF_WhitelistClear(void) {
  using Fn = int (VENDOR_CALL*)();
  auto fn = load_fn<Fn>("SWHid_DelAllWhiteList");
  if (!fn) {
    fn = load_fn<Fn>("SWHid_DeleteAllWhiteList");
  }
  if (!fn) {
    set_last_error("Missing SWHid_DelAllWhiteList/SWHid_DeleteAllWhiteList", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  return fn() ? 1 : 0;
}

UHF_API int UHF_CALL UHF_ModuleCommand(uint8_t cmd, const uint8_t* payload, int payloadLen,
                                       uint8_t* outBuf, int outBufLen, int* outRespLen) {
  return send_module_cmd(cmd, payload, payloadLen, outBuf, outBufLen, outRespLen);
}

} // extern "C"
