#include "uhf_wrapper.h"

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
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
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
  #if defined(_WIN64)
    #define VENDOR_CALL
  #else
    #define VENDOR_CALL __cdecl
  #endif
#else
  #define VENDOR_CALL
#endif

static int check_system_config_internal(char* outMsg, int outMsgLen, int keep_open);
static int ensure_usb_config_ready(void);
static void close_device_silent(void);

namespace {
#if defined(_WIN32)
using LibHandle = HMODULE;
#else
using LibHandle = void*;
#endif

static LibHandle g_vendor = nullptr;
static int g_is_open = 0;
static int g_is_reading = 0;
static char g_last_error[256] = "";
static int g_last_error_code = UHF_ERR_OK;
static int g_dedup_window_ms = 0;
static int g_dedup_key_mode = 0; // 0 = EPC only, 1 = EPC + antenna
static int g_rssi_min_dbm = std::numeric_limits<int>::min();
static int g_rssi_max_dbm = std::numeric_limits<int>::max();
static int g_has_calibration = 0;
static UHF_CalibrationResult g_calibration = {};
static std::unordered_map<std::string, int64_t> g_dedup_cache;
static std::mutex g_dedup_mutex;
static const uint8_t kParamWorkMode = 0x02;
static const uint8_t kWorkModeAnswer = 0x00;
static const uint8_t kWorkModeActive = 0x01;
static const uint8_t kWorkModeTrigger = 0x02;
// Device parameters (protocol doc V1.9): 01 Transport, 02 WorkMode.
static const uint8_t kParamTransport = 0x01;
static const uint8_t kTransportUsb = 0x00;
static const uint8_t kTransportRs232 = 0x01;
static const uint8_t kTransportRj45 = 0x02;
static const uint8_t kTransportWifi = 0x03;
static const uint8_t kTransportWeigand = 0x04;
static const uint8_t kDeviceParamHeader = 0xC3;
static const uint8_t kDeviceParamBlockLen = 32;

static int device_param_index(uint8_t param_addr, const uint8_t* buf, int len) {
  if (param_addr == 0) return 0;
  int base = 0;
  if (buf && len > 1 && buf[0] == kDeviceParamHeader && buf[1] == 0x55) {
    base = 2; // Some firmwares prefix DevType (0xC3) + default switch (0x55).
  } else if (buf && len > 0 && buf[0] == 0x55) {
    base = 1; // Some firmwares include an initial 0x55 before bTransport.
  }
  int idx = base + static_cast<int>(param_addr) - 1;
  if (idx < base) idx = base;
  if (len > 0 && idx >= len) return -1;
  return idx;
}

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
  uint8_t bInitialFlag;
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
static void bytes_to_hex(const uint8_t* data, int len, char* out, int out_cap);
static void finalize_checksum(uint8_t* buf, int len);
static int send_buffer(const uint8_t* buf, int len);
static int parse_tag_buffer(const uint8_t* buf, int total_len, int dedup,
                            UHF_Tag* out_tags, int max_tags, int* out_count);
static int start_read_vendor(int set_error);
static int stop_read_vendor(int set_error);
static int ensure_work_mode(uint8_t mode, const char* mode_name);

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

#if defined(_WIN64)
static const char* get_x86_helper_path() {
  static std::string path;
  static int inited = 0;
  if (inited) return path.empty() ? nullptr : path.c_str();
  inited = 1;
  char buf[1024] = {0};
  DWORD n = GetEnvironmentVariableA("UHF_X86_HELPER", buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf)) {
    path = buf;
  }
  return path.empty() ? nullptr : path.c_str();
}

static int parse_json_string_field(const std::string& line, const char* key, std::string& out) {
  std::string pat = std::string("\"") + key + "\":\"";
  size_t pos = line.find(pat);
  if (pos == std::string::npos) return 0;
  pos += pat.size();
  size_t end = line.find('\"', pos);
  if (end == std::string::npos || end < pos) return 0;
  out = line.substr(pos, end - pos);
  return 1;
}

static int parse_json_int_field(const std::string& line, const char* key, int* out) {
  if (!out) return 0;
  std::string pat = std::string("\"") + key + "\":";
  size_t pos = line.find(pat);
  if (pos == std::string::npos) return 0;
  pos += pat.size();
  const char* start = line.c_str() + pos;
  char* end = nullptr;
  long val = strtol(start, &end, 10);
  if (end == start) return 0;
  *out = static_cast<int>(val);
  return 1;
}

static int read_once_via_helper(int timeout_ms, UHF_Tag* out_tags, int max_tags, int* out_count) {
  if (!out_count || !out_tags || max_tags <= 0) return 0;
  *out_count = 0;
  const char* helper = get_x86_helper_path();
  if (!helper || !helper[0]) return 0;

  std::string cmd = "\"";
  cmd += helper;
  cmd += "\" --json --timeout ";
  cmd += std::to_string(timeout_ms);
  cmd += " read-once";

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    set_last_error("Failed to create helper pipe", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');

  BOOL created = CreateProcessA(nullptr,
                                cmd_buf.data(),
                                nullptr,
                                nullptr,
                                TRUE,
                                CREATE_NO_WINDOW,
                                nullptr,
                                nullptr,
                                &si,
                                &pi);
  CloseHandle(write_pipe);
  if (!created) {
    CloseHandle(read_pipe);
    set_last_error("Failed to launch x86 helper", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }

  DWORD wait_ms = (timeout_ms > 0 ? static_cast<DWORD>(timeout_ms + 2000) : 2000);
  WaitForSingleObject(pi.hProcess, wait_ms);

  std::string output;
  char buf[4096];
  DWORD read = 0;
  while (ReadFile(read_pipe, buf, sizeof(buf), &read, nullptr) && read > 0) {
    output.append(buf, buf + read);
  }

  CloseHandle(read_pipe);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  int count = 0;
  size_t start = 0;
  while (start < output.size()) {
    size_t end = output.find_first_of("\r\n", start);
    if (end == std::string::npos) end = output.size();
    std::string line = output.substr(start, end - start);
    start = end + 1;
    if (line.empty()) continue;
    if (line.front() != '{') continue;
    UHF_Tag tag{};
    std::string epc;
    if (!parse_json_string_field(line, "epc", epc)) continue;
    int len = 0;
    int rssi = 0;
    int ant = 0;
    int type = 0;
    parse_json_int_field(line, "len", &len);
    parse_json_int_field(line, "rssi", &rssi);
    parse_json_int_field(line, "ant", &ant);
    parse_json_int_field(line, "tagType", &type);

    if (len <= 0) {
      len = static_cast<int>(epc.size() / 2);
    }

    tag.epcLenBytes = static_cast<uint8_t>(len);
    tag.rssiDbm = rssi;
    tag.antenna = static_cast<uint8_t>(ant);
    tag.tagType = static_cast<uint8_t>(type);
    tag.hasTs = 0;
#if defined(_MSC_VER)
    strncpy_s(tag.epc, epc.c_str(), sizeof(tag.epc) - 1);
#else
    strncpy(tag.epc, epc.c_str(), sizeof(tag.epc) - 1);
    tag.epc[sizeof(tag.epc) - 1] = '\0';
#endif

    if (count < max_tags) {
      out_tags[count++] = tag;
    }
  }

  *out_count = count;
  return 1;
}
#endif

static int start_read_vendor(int set_error) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_StartRead");
  if (!fn) {
    if (set_error) set_last_error("Missing SWHid_StartRead", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  int ok = fn(0xFF) ? 1 : 0;
  if (!ok) {
    // Some firmware/transport stacks can leave RF in a transient state.
    // Best effort: stop and retry once.
    (void)stop_read_vendor(0);
    sleep_ms(30);
    ok = fn(0xFF) ? 1 : 0;
  }
  if (!ok && set_error) {
    set_last_error("SWHid_StartRead failed", UHF_ERR_VENDOR_CALL_FAILED);
  }
  return ok;
}

static int stop_read_vendor(int set_error) {
  using Fn = int (VENDOR_CALL*)(uint8_t);
  auto fn = load_fn<Fn>("SWHid_StopRead");
  if (!fn) {
    if (set_error) set_last_error("Missing SWHid_StopRead", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  int ok = fn(0xFF) ? 1 : 0;
  if (!ok && set_error) {
    set_last_error("SWHid_StopRead failed", UHF_ERR_VENDOR_CALL_FAILED);
  }
  return ok;
}

static int ensure_work_mode(uint8_t mode, const char* mode_name) {
  if (!ensure_usb_config_ready()) {
    return 0;
  }
  using FnRead = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*);
  using FnSet = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t);
  auto fn_set = load_fn<FnSet>("SWHid_SetDeviceOneParam");
  if (!fn_set) {
    set_last_error("Missing SWHid_SetDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (g_is_reading && mode != kWorkModeActive) {
    set_last_error("Cannot switch work mode while reading", UHF_ERR_ALREADY_READING);
    return 0;
  }
  auto fn_read = load_fn<FnRead>("SWHid_ReadDeviceOneParam");
  if (fn_read) {
    uint8_t cur = 0xFF;
    if (fn_read(0xFF, kParamWorkMode, &cur)) {
      if (cur == mode) {
        return 1;
      }
    }
  }
  // If direct read failed, try block read to detect current mode.
  using FnReadBlock = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t*, uint8_t);
  auto fn_read_block = load_fn<FnReadBlock>("SWHid_ReadDeviceParam");
  if (fn_read_block) {
    uint8_t buf[kDeviceParamBlockLen] = {};
    uint8_t hdr = 0;
    if (fn_read_block(0xFF, &hdr, buf, kDeviceParamBlockLen)) {
      int idx = device_param_index(kParamWorkMode, buf, kDeviceParamBlockLen);
      if (idx >= 0 && idx < kDeviceParamBlockLen) {
        if (buf[idx] == mode) {
          return 1;
        }
      }
    }
  }
  if (!fn_set(0xFF, kParamWorkMode, mode)) {
    // Some firmware requires RF to be stopped before mode change.
    stop_read_vendor(0);
    if (fn_set(0xFF, kParamWorkMode, mode)) {
      return 1;
    }
    // Fallback to full param block (needed when reader is not in USB mode).
    using FnSetBlock = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*, uint8_t);
    auto fn_set_block = load_fn<FnSetBlock>("SWHid_SetDeviceParam");
    if (fn_read_block && fn_set_block) {
      uint8_t buf[kDeviceParamBlockLen] = {};
      uint8_t hdr = kDeviceParamHeader;
      if (fn_read_block(0xFF, &hdr, buf, kDeviceParamBlockLen)) {
        int idx = device_param_index(kParamWorkMode, buf, kDeviceParamBlockLen);
        if (idx >= 0 && idx < kDeviceParamBlockLen) {
          buf[idx] = mode;
          if (fn_set_block(0xFF, hdr ? hdr : kDeviceParamHeader, buf, kDeviceParamBlockLen)) {
            return 1;
          }
        }
      }
    }
    if (mode_name) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Failed to set %s", mode_name);
      set_last_error(msg, UHF_ERR_VENDOR_CALL_FAILED);
    } else {
      set_last_error("Failed to set work mode", UHF_ERR_VENDOR_CALL_FAILED);
    }
    return 0;
  }
  return 1;
}

static int read_device_param_block(uint8_t param_addr, uint8_t* out_val) {
  if (!out_val) {
    set_last_error("Invalid device param buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  using FnReadBlock = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t*, uint8_t);
  auto fn_read_block = load_fn<FnReadBlock>("SWHid_ReadDeviceParam");
  if (!fn_read_block) {
    return 0;
  }
  uint8_t buf[kDeviceParamBlockLen] = {};
  uint8_t hdr = 0;
  if (!fn_read_block(0xFF, &hdr, buf, kDeviceParamBlockLen)) {
    return 0;
  }
  int idx = device_param_index(param_addr, buf, kDeviceParamBlockLen);
  if (idx < 0 || idx >= kDeviceParamBlockLen) {
    return 0;
  }
  *out_val = buf[idx];
  set_last_error("", UHF_ERR_OK);
  return 1;
}

static int write_device_param_block(uint8_t param_addr, uint8_t val) {
  using FnReadBlock = int (VENDOR_CALL*)(uint8_t, uint8_t*, uint8_t*, uint8_t);
  using FnSetBlock = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*, uint8_t);
  auto fn_read_block = load_fn<FnReadBlock>("SWHid_ReadDeviceParam");
  auto fn_set_block = load_fn<FnSetBlock>("SWHid_SetDeviceParam");
  if (!fn_read_block || !fn_set_block) {
    return 0;
  }
  uint8_t buf[kDeviceParamBlockLen] = {};
  uint8_t hdr = kDeviceParamHeader;
  if (!fn_read_block(0xFF, &hdr, buf, kDeviceParamBlockLen)) {
    return 0;
  }
  int idx = device_param_index(param_addr, buf, kDeviceParamBlockLen);
  if (idx < 0 || idx >= kDeviceParamBlockLen) {
    return 0;
  }
  buf[idx] = val;
  if (!fn_set_block(0xFF, hdr ? hdr : kDeviceParamHeader, buf, kDeviceParamBlockLen)) {
    return 0;
  }
  set_last_error("", UHF_ERR_OK);
  return 1;
}

static int read_transport_param(uint8_t* out_val) {
  if (!out_val) {
    set_last_error("Invalid transport buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  using FnRead = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*);
  auto fn_read = load_fn<FnRead>("SWHid_ReadDeviceOneParam");
  if (!fn_read) {
    set_last_error("Missing SWHid_ReadDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!fn_read(0xFF, kParamTransport, out_val)) {
    // Fallback to full device param block (needed when reader is not in USB mode).
    uint8_t tmp = 0;
    if (read_device_param_block(kParamTransport, &tmp)) {
      *out_val = tmp;
      return 1;
    }
    set_last_error("SWHid_ReadDeviceOneParam failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  return 1;
}

static int read_param_with_block(uint8_t param, uint8_t* out_val) {
  if (!out_val) {
    set_last_error("Invalid param buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  using FnRead = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*);
  auto fn_read = load_fn<FnRead>("SWHid_ReadDeviceOneParam");
  if (!fn_read) {
    set_last_error("Missing SWHid_ReadDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!fn_read(0xFF, param, out_val)) {
    uint8_t tmp = 0;
    if (read_device_param_block(param, &tmp)) {
      *out_val = tmp;
      return 1;
    }
    set_last_error("SWHid_ReadDeviceOneParam failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  return 1;
}
static int set_transport_param(uint8_t val) {
  // Prefer full device param block to match vendor soft behavior (persists config).
  if (write_device_param_block(kParamTransport, val)) {
    return 1;
  }
  using FnSet = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t);
  auto fn_set = load_fn<FnSet>("SWHid_SetDeviceOneParam");
  if (fn_set) {
    if (fn_set(0xFF, kParamTransport, val)) {
      set_last_error("", UHF_ERR_OK);
      return 1;
    }
  }
  uint8_t frame[9] = {0x53, 0x57, 0x00, 0x05, 0xFF, 0x24, kParamTransport, val, 0x00};
  finalize_checksum(frame, static_cast<int>(sizeof(frame)));
  if (send_buffer(frame, static_cast<int>(sizeof(frame)))) {
    set_last_error("", UHF_ERR_OK);
    return 1;
  }
  uint8_t hid_buf[11] = {0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  memcpy(hid_buf + 2, frame, sizeof(frame));
  if (send_buffer(hid_buf, static_cast<int>(sizeof(hid_buf)))) {
    set_last_error("", UHF_ERR_OK);
    return 1;
  }
  set_last_error("Failed to set transport", UHF_ERR_VENDOR_CALL_FAILED);
  return 0;
}

static int ensure_transport_usb_opened(int allow_fix) {
  uint8_t transport = 0xFF;
  int read_ok = read_transport_param(&transport);
  if (read_ok) {
    if (transport == kTransportUsb) {
      set_last_error("", UHF_ERR_OK);
      return 1;
    }
    if (!allow_fix) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Transport is not USB (value %u)", transport);
      set_last_error(msg, UHF_ERR_VERIFY_FAILED);
      return 0;
    }
  } else if (!allow_fix) {
    return 0;
  } else {
    // Transport read not supported; try forcing USB anyway.
    if (set_transport_param(kTransportUsb)) {
      transport = 0xFF;
      if (read_transport_param(&transport)) {
        if (transport != kTransportUsb) {
          char msg[128];
          snprintf(msg, sizeof(msg),
                   "Transport set to USB but readback is %u", transport);
          set_last_error(msg, UHF_ERR_VERIFY_FAILED);
          return 0;
        }
      }
    }
    // If we still can't read, assume USB and continue.
    set_last_error("", UHF_ERR_OK);
    return 1;
  }

  if (!set_transport_param(kTransportUsb)) {
    set_last_error("Failed to set transport to USB", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }

  transport = 0xFF;
  if (read_transport_param(&transport)) {
    if (transport != kTransportUsb) {
      char msg[128];
      snprintf(msg, sizeof(msg),
               "Transport set to USB but readback is %u", transport);
      set_last_error(msg, UHF_ERR_VERIFY_FAILED);
      return 0;
    }
  }
  set_last_error("", UHF_ERR_OK);
  return 1;
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


static int inventory_g2_raw(uint8_t* buf, int buf_len, int* out_len, int* out_count) {
  using Fn = uint8_t (VENDOR_CALL*)(uint8_t, uint8_t*, uint16_t*, uint16_t*);
  auto fn = load_fn<Fn>("SWHid_InventoryG2");
  if (!fn) {
    set_last_error("Missing SWHid_InventoryG2", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  if (!buf || buf_len <= 0 || !out_len || !out_count) {
    set_last_error("Invalid inventory buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint16_t total_len = 0;
  uint16_t card_num = 0;
  uint8_t ok = fn(0xFF, buf, &total_len, &card_num);
  *out_len = static_cast<int>(total_len);
  *out_count = static_cast<int>(card_num);
  if (ok != 0) {
    return 1;
  }
  if (!UHF_IsReaderPresent()) {
    set_last_error("Reader not present", UHF_ERR_NO_DEVICE);
    return 0;
  }
  return 1;
}

static int inventory_g2_once_no_mode(int timeout_ms, UHF_Tag* out_tags, int max_tags, int* out_count);

static int inventory_g2_once(int timeout_ms, UHF_Tag* out_tags, int max_tags, int* out_count) {
  if (!out_tags || max_tags <= 0 || !out_count) return 0;
  *out_count = 0;
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
    // Some vendor builds don't expose WorkMode via OneParam; still try inventory.
    int ok = inventory_g2_once_no_mode(timeout_ms, out_tags, max_tags, out_count);
    if (ok) {
      set_last_error("", UHF_ERR_OK);
    }
    return ok;
  }
  if (timeout_ms < 0) timeout_ms = 0;
  const int interval_ms = 50;
  int64_t start = now_ms();
  uint8_t buf[65536];
  for (;;) {
    int total_len = 0;
    int tag_num = 0;
    if (!inventory_g2_raw(buf, sizeof(buf), &total_len, &tag_num)) {
      return 0;
    }
    if (total_len > 0) {
      if (!parse_tag_buffer(buf, total_len, 1, out_tags, max_tags, out_count)) {
        return 0;
      }
      if (*out_count > 0) {
        return 1;
      }
    }
    if (timeout_ms == 0) {
      break;
    }
    if ((now_ms() - start) >= timeout_ms) {
      break;
    }
    sleep_ms(interval_ms);
  }
  return 1;
}

static int inventory_g2_once_no_mode(int timeout_ms, UHF_Tag* out_tags, int max_tags, int* out_count) {
  if (!out_tags || max_tags <= 0 || !out_count) return 0;
  *out_count = 0;
  if (timeout_ms < 0) timeout_ms = 0;
  const int interval_ms = 50;
  int64_t start = now_ms();
  uint8_t buf[65536];
  for (;;) {
    int total_len = 0;
    int tag_num = 0;
    if (!inventory_g2_raw(buf, sizeof(buf), &total_len, &tag_num)) {
      return 0;
    }
    if (total_len > 0) {
      if (!parse_tag_buffer(buf, total_len, 1, out_tags, max_tags, out_count)) {
        return 0;
      }
      if (*out_count > 0) {
        return 1;
      }
    }
    if (timeout_ms == 0) {
      break;
    }
    if ((now_ms() - start) >= timeout_ms) {
      break;
    }
    sleep_ms(interval_ms);
  }
  return 1;
}

static int read_special_param(uint8_t* out_buf, uint8_t out_len) {
  if (!out_buf || out_len == 0) return 0;
  if (!ensure_usb_config_ready()) return 0;
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
  if (!ensure_usb_config_ready()) return 0;
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
  if (!ensure_usb_config_ready()) {
    return 0;
  }
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
  p.bInitialFlag = 0x55;
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
  int mode = UHF_GetWorkMode();
  if (mode == kWorkModeAnswer || mode < 0) {
    // Active-mode buffer clear can fail in AnswerMode; ignore.
    (void)UHF_ClearBuffer();
    UHF_Tag tmp[64];
    int tmp_count = 0;
    if (inventory_g2_once(timeout_ms, tmp, 64, &tmp_count)) {
      *out_count = tmp_count;
      if (tmp_count > 0) return 1;
      // Fall through to Active-mode buffer fallback if Answer mode yields 0 tags.
    }
    // Fallback: use Active-mode buffered read to avoid Answer-mode firmware quirks.
    int restore_answer = (mode == kWorkModeAnswer);
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
    if (restore_answer) {
      (void)UHF_SetWorkModeAnswer();
    }
    return 1;
  } else {
    if (!UHF_StartRead()) {
      return 0;
    }
    if (timeout_ms < 0) timeout_ms = 0;
    sleep_ms(timeout_ms);
    UHF_StopRead();
  }
  UHF_Tag tags[256];
  int count = 0;
  if (!UHF_PopBufferDedup(tags, 256, &count)) {
    return 0;
  }
  *out_count = count;
  return 1;
}

static int read_tags_active_window(int duration_ms, UHF_Tag* out_tags, int max_tags, int* out_count) {
  if (!out_tags || max_tags <= 0 || !out_count) return 0;
  *out_count = 0;
  if (duration_ms < 0) duration_ms = 0;
  // Force Active mode for buffered reads.
  (void)UHF_SetWorkModeActive();
  (void)UHF_ClearBuffer();
  if (!UHF_StartRead()) {
    return 0;
  }
  sleep_ms(duration_ms);
  UHF_StopRead();
  return UHF_PopBufferAll(out_tags, max_tags, out_count);
}

static int read_tags_once(int timeout_ms, UHF_Tag* out_tags, int max_tags, int* out_count) {
  if (!out_tags || max_tags <= 0 || !out_count) {
    return 0;
  }
  *out_count = 0;
  int mode = UHF_GetWorkMode();
  // ClearTagBuf is for ActiveMode; in AnswerMode it may fail, ignore.
  (void)UHF_ClearBuffer();
  if (mode == kWorkModeAnswer || mode < 0) {
    int ok = inventory_g2_once(timeout_ms, out_tags, max_tags, out_count);
    if (ok && *out_count > 0) {
      return 1;
    }
    // Fallback: Answer-mode inventory can return 0 tags on some firmwares.
    int restore_answer = (mode == kWorkModeAnswer);
    int active_ms = timeout_ms;
    if (active_ms < 500) active_ms = 500;
    int ok_active = read_tags_active_window(active_ms, out_tags, max_tags, out_count);
    if (restore_answer) {
      (void)UHF_SetWorkModeAnswer();
    }
    return ok_active;
  }
  if (!UHF_StartRead()) {
    return 0;
  }
  if (timeout_ms < 0) timeout_ms = 0;
  sleep_ms(timeout_ms);
  UHF_StopRead();
  return UHF_PopBufferDedup(out_tags, max_tags, out_count);
}

static int generate_random_epc_hex(char* out_hex, int out_len, int epc_bytes) {
  if (!out_hex || out_len <= 0 || epc_bytes <= 0 || epc_bytes > UHF_MAX_EPC_BYTES) {
    return 0;
  }
  if (out_len < (epc_bytes * 2 + 1)) {
    return 0;
  }
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> dist(0, 255);
  uint8_t buf[UHF_MAX_EPC_BYTES] = {0};
  for (int i = 0; i < epc_bytes; ++i) {
    buf[i] = static_cast<uint8_t>(dist(rng));
  }
  bytes_to_hex(buf, epc_bytes, out_hex, out_len);
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
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
    return 0;
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
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
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

static int extract_single_epc(const UHF_Tag* tags, int count, char* out_hex, int out_len) {
  if (!out_hex || out_len <= 0) return 0;
  if (!tags || count <= 0) return 0;
  const char* base = tags[0].epc;
  for (int i = 1; i < count; ++i) {
    if (!epc_hex_equal(tags[i].epc, base)) {
      return -1;
    }
  }
#if defined(_MSC_VER)
  strncpy_s(out_hex, out_len, base, _TRUNCATE);
#else
  strncpy(out_hex, base, static_cast<size_t>(out_len - 1));
  out_hex[out_len - 1] = '\0';
#endif
  return 1;
}

static int calibration_verify_unique_epc(const char* target_hex, int tid_reads) {
  if (!target_hex || !target_hex[0]) return 1;
  if (tid_reads <= 0) tid_reads = 1;
  // If multiple EPCs are visible, accept and rely on software filtering.
  {
    UHF_Tag tags[16];
    int count = 0;
    if (read_tags_once(200, tags, 16, &count) && count > 1) {
      set_last_error("", UHF_ERR_OK);
      return 1;
    }
  }
  uint8_t target_bytes[UHF_MAX_EPC_BYTES] = {0};
  int target_len = hex_to_bytes(target_hex, target_bytes, sizeof(target_bytes));
  if (target_len <= 0 || (target_len % 2) != 0) {
    return 1;
  }
  if (!UHF_SelectEpc(target_hex)) {
    // Can't verify uniqueness; proceed without blocking calibration.
    set_last_error("", UHF_ERR_OK);
    return 1;
  }
  // Verify the reader actually honors the select mask; if not, skip strict check.
  {
    UHF_Tag tags[16];
    int count = 0;
    if (read_tags_once(200, tags, 16, &count)) {
      int only_target = 1;
      for (int i = 0; i < count; ++i) {
        if (!epc_hex_equal(tags[i].epc, target_hex)) {
          only_target = 0;
          break;
        }
      }
      if (!only_target) {
        set_last_error("", UHF_ERR_OK);
        UHF_ClearSelect();
        return 1;
      }
    }
  }
  // Verify ReadTag honors the select by reading EPC memory multiple times.
  {
    int word_count = target_len / 2;
    if (word_count <= 0) {
      set_last_error("", UHF_ERR_OK);
      UHF_ClearSelect();
      return 1;
    }
    for (int i = 0; i < 3; ++i) {
      uint8_t epc_mem[UHF_MAX_EPC_BYTES] = {0};
      int bytes_read = 0;
      if (!UHF_ReadTag(1, 2, static_cast<uint8_t>(word_count), nullptr,
                       epc_mem, sizeof(epc_mem), &bytes_read) ||
          bytes_read < target_len ||
          memcmp(epc_mem, target_bytes, static_cast<size_t>(target_len)) != 0) {
        set_last_error("", UHF_ERR_OK);
        UHF_ClearSelect();
        return 1;
      }
    }
  }
  uint8_t tid_vals[3][32] = {{0}};
  int tid_lens[3] = {0};
  int tid_counts[3] = {0};
  int tid_unique = 0;
  for (int i = 0; i < tid_reads; ++i) {
    uint8_t tid[32] = {0};
    int bytes_read = 0;
    if (!UHF_ReadTag(2, 0, 6, nullptr, tid, sizeof(tid), &bytes_read) || bytes_read <= 0) {
      // TID not readable; skip strict duplicate detection.
      set_last_error("", UHF_ERR_OK);
      UHF_ClearSelect();
      return 1;
    }
    if (bytes_read > static_cast<int>(sizeof(tid_vals[0]))) {
      set_last_error("", UHF_ERR_OK);
      UHF_ClearSelect();
      return 1;
    }
    int matched = 0;
    for (int j = 0; j < tid_unique; ++j) {
      if (bytes_read == tid_lens[j] &&
          memcmp(tid_vals[j], tid, static_cast<size_t>(bytes_read)) == 0) {
        tid_counts[j]++;
        matched = 1;
        break;
      }
    }
    if (!matched) {
      if (tid_unique >= 3) {
        // Too many variants; assume unreliable reads and skip.
        set_last_error("", UHF_ERR_OK);
        UHF_ClearSelect();
        return 1;
      }
      memcpy(tid_vals[tid_unique], tid, static_cast<size_t>(bytes_read));
      tid_lens[tid_unique] = bytes_read;
      tid_counts[tid_unique] = 1;
      tid_unique++;
    }
  }
  for (int j = 0; j < tid_unique; ++j) {
    if (tid_lens[j] <= 0) {
      // Inconsistent TID length; skip strict duplicate detection.
      set_last_error("", UHF_ERR_OK);
      UHF_ClearSelect();
      return 1;
    }
  }
  int distinct_with_repeats = 0;
  for (int j = 0; j < tid_unique; ++j) {
    if (tid_counts[j] >= 2) {
      distinct_with_repeats++;
    }
  }
  if (distinct_with_repeats >= 2) {
    set_last_error("Multiple tags share the calibration EPC (different TID)", UHF_ERR_MULTI_TAG);
    UHF_ClearSelect();
    return 0;
  }
  UHF_ClearSelect();
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
  auto parse_packed_records = [&](const uint8_t* data, int data_len) {
    if (!data || data_len <= 0) return;
    int rec_off = 0;
    while (rec_off < data_len) {
      uint8_t pack_len = data[rec_off];
      if (pack_len == 0) {
        rec_off += 1;
        continue;
      }
      if (rec_off + 1 + pack_len > data_len) {
        break;
      }

      uint8_t tag_type = data[rec_off + 1];
      uint8_t antenna = data[rec_off + 2];
      int has_ts = (tag_type & 0x80) != 0;
      int id_len = static_cast<int>(pack_len) - 1 - 2 - (has_ts ? 6 : 0);

      if (id_len <= 0 || id_len > UHF_MAX_EPC_BYTES) {
        rec_off += pack_len + 1;
        continue;
      }

      const uint8_t* epc_ptr = data + rec_off + 3;
      const uint8_t* rssi_ptr = epc_ptr + id_len;
      const uint8_t* frame_end = data + rec_off + 1 + pack_len;
      if (rssi_ptr >= frame_end) {
        rec_off += pack_len + 1;
        continue;
      }
      if (has_ts && (rssi_ptr + 1 + UHF_TS_LEN > frame_end)) {
        rec_off += pack_len + 1;
        continue;
      }

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

      rec_off += pack_len + 1;
    }
  };

  // Reader packets can come as:
  // - packed tag records only (active buffer APIs), or
  // - "CT" framed responses (Inventory/ActiveData module responses).
  if (total_len >= 4 && buf[0] == 0x43 && buf[1] == 0x54) {
    while (offset + 4 <= total_len) {
      if (buf[offset] != 0x43 || buf[offset + 1] != 0x54) {
        offset += 1;
        continue;
      }
      int payload_len = (static_cast<int>(buf[offset + 2]) << 8) |
                        static_cast<int>(buf[offset + 3]);
      if (payload_len <= 0 || offset + 4 + payload_len > total_len) {
        break;
      }

      const uint8_t* payload = buf + offset + 4;
      // CMD_INVENTORY_TAG response: status, cmd(0x01), result, tagCount(2), records..., checksum
      if (payload_len >= 6 && payload[1] == 0x01) {
        parse_packed_records(payload + 5, payload_len - 5);
      }
      // CMD_ACTIVE_DATA response: status, cmd(0x45), result, devSn(7), tagNum, records..., checksum
      else if (payload_len >= 12 && payload[1] == 0x45) {
        parse_packed_records(payload + 11, payload_len - 11);
      }

      offset += 4 + payload_len;
    }
  } else {
    parse_packed_records(buf, total_len);
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
  g_is_reading = 0;
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
  g_is_reading = 0;
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
  // Vendor close often returns 0 even when the handle is actually released.
  // Their official tool doesn't check this return, so we treat close as best-effort.
  (void)fn();
  g_is_open = 0;
  g_is_reading = 0;
  set_last_error("", UHF_ERR_OK);
  return 1;
}

UHF_API int UHF_CALL UHF_IsReaderPresent(void) {
  // Some vendor stacks disturb the opened handle when querying USB list.
  // If we already have an open handle, treat presence as true.
  if (UHF_IsOpen()) {
    return 1;
  }
  int count = UHF_GetUsbCount();
  return count > 0 ? 1 : 0;
}

UHF_API int UHF_CALL UHF_IsOpen(void) {
  return g_is_open ? 1 : 0;
}

UHF_API int UHF_CALL UHF_IsConnected(void) {
  // Avoid USB re-enumeration side effects while handle is open.
  if (UHF_IsOpen()) {
    return 1;
  }
  return UHF_IsReaderPresent();
}

UHF_API int UHF_CALL UHF_GetInfo(UHF_DeviceInfo* outInfo) {
  if (!ensure_usb_config_ready()) {
    return 0;
  }
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

UHF_API int UHF_CALL UHF_GetStatus(UHF_Status* outStatus) {
  if (!outStatus) {
    set_last_error("Invalid outStatus", UHF_ERR_INVALID_ARG);
    return 0;
  }
  memset(outStatus, 0, sizeof(*outStatus));
  outStatus->open = UHF_IsOpen();
  outStatus->present = outStatus->open ? 1 : UHF_IsReaderPresent();
  outStatus->connected = outStatus->open ? 1 : UHF_IsConnected();
  outStatus->powerDbm = -1;
  outStatus->powerPct = -1;
  outStatus->transport = -1;
  outStatus->workMode = -1;
  outStatus->hasFreq = 0;
  outStatus->rssiFilterEnabled = 0;
  outStatus->rssiFilterMinDbm = std::numeric_limits<int>::min();
  outStatus->rssiFilterMaxDbm = std::numeric_limits<int>::max();
  outStatus->dedupWindowMs = 0;
  outStatus->dedupKeyMode = 0;

  {
    std::lock_guard<std::mutex> lock(g_dedup_mutex);
    outStatus->dedupWindowMs = g_dedup_window_ms;
    outStatus->dedupKeyMode = g_dedup_key_mode;
    outStatus->rssiFilterMinDbm = g_rssi_min_dbm;
    outStatus->rssiFilterMaxDbm = g_rssi_max_dbm;
    outStatus->rssiFilterEnabled =
        (g_rssi_min_dbm != std::numeric_limits<int>::min()) ||
        (g_rssi_max_dbm != std::numeric_limits<int>::max());
  }

  if (!outStatus->present) {
    set_last_error("", UHF_ERR_OK);
    return 1;
  }

  int opened_here = 0;
  if (!UHF_IsOpen()) {
    if (!UHF_Open(0)) {
      set_last_error("Open failed for status", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    opened_here = 1;
  }

  uint8_t transport = 0xFF;
  if (read_transport_param(&transport)) {
    outStatus->transport = static_cast<int>(transport);
  }
  if (outStatus->transport < 0) {
    int t = UHF_GetTransport();
    if (t >= 0) outStatus->transport = t;
  }

  uint8_t workmode = 0xFF;
  if (read_param_with_block(kParamWorkMode, &workmode)) {
    outStatus->workMode = static_cast<int>(workmode);
  }
  if (outStatus->workMode < 0) {
    int w = UHF_GetWorkMode();
    if (w >= 0) outStatus->workMode = w;
  }

  uint8_t power = 0;
  if (read_param_with_block(0x05, &power)) {
    outStatus->powerDbm = static_cast<int>(power);
    int pct = static_cast<int>((outStatus->powerDbm * 100.0) / 26.0 + 0.5);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    outStatus->powerPct = pct;
  }
  if (outStatus->powerDbm < 0) {
    int p = UHF_GetPowerDbm();
    if (p >= 0) {
      outStatus->powerDbm = p;
      outStatus->powerPct = UHF_GetPowerPct();
    }
  }

  uint8_t freq[2] = {0};
  if (UHF_GetFreq(freq)) {
    outStatus->freq0 = freq[0];
    outStatus->freq1 = freq[1];
    outStatus->hasFreq = 1;
  }

  if (opened_here) close_device_silent();
  set_last_error("", UHF_ERR_OK);
  return 1;
}

UHF_API int UHF_CALL UHF_GetTransport(void) {
  int opened_here = 0;
  if (!UHF_IsOpen()) {
    int usb_count = UHF_GetUsbCount();
    if (usb_count <= 0) {
      set_last_error("No USB device found", UHF_ERR_NO_DEVICE);
      return -1;
    }
    if (!UHF_Open(0)) {
      set_last_error("Open failed for transport read", UHF_ERR_VENDOR_CALL_FAILED);
      return -1;
    }
    opened_here = 1;
  }
  uint8_t transport = 0xFF;
  if (!read_transport_param(&transport)) {
    if (opened_here) close_device_silent();
    return -1;
  }
  if (opened_here) close_device_silent();
  return static_cast<int>(transport);
}

UHF_API int UHF_CALL UHF_SetTransport(uint8_t transport) {
  int usb_count = UHF_GetUsbCount();
  if (usb_count <= 0) {
    set_last_error("No USB device found", UHF_ERR_NO_DEVICE);
    return 0;
  }
  if (!UHF_IsOpen()) {
    if (!UHF_Open(0)) {
      set_last_error("Open failed for transport set", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
  }
  if (!set_transport_param(transport)) {
    return 0;
  }
  set_last_error("", UHF_ERR_OK);
  return 1;
}

UHF_API int UHF_CALL UHF_SetTransportUsb(void) {
  return UHF_SetTransport(kTransportUsb);
}

UHF_API int UHF_CALL UHF_EnsureUsbTransport(void) {
  int already_open = UHF_IsOpen();
  if (!already_open) {
    int usb_count = UHF_GetUsbCount();
    if (usb_count <= 0) {
      set_last_error("No USB device found", UHF_ERR_NO_DEVICE);
      return 0;
    }
    if (!UHF_Open(0)) {
      set_last_error("Open failed for transport check", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
  }
  return ensure_transport_usb_opened(1);
}

UHF_API int UHF_CALL UHF_GetWorkMode(void) {
  if (!ensure_usb_config_ready()) {
    return -1;
  }
  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadDeviceOneParam");
  if (!fn) {
    set_last_error("Missing SWHid_ReadDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return -1;
  }
  uint8_t val = 0xFF;
  if (!fn(0xFF, kParamWorkMode, &val)) {
    set_last_error("SWHid_ReadDeviceOneParam failed", UHF_ERR_VENDOR_CALL_FAILED);
    return -1;
  }
  return static_cast<int>(val);
}

UHF_API int UHF_CALL UHF_CheckSystemConfig(char* outMsg, int outMsgLen) {
  return check_system_config_internal(outMsg, outMsgLen, 0);
}

UHF_API int UHF_CALL UHF_SetWorkModeAnswer(void) {
  return ensure_work_mode(kWorkModeAnswer, "AnswerMode");
}

UHF_API int UHF_CALL UHF_SetWorkModeActive(void) {
  return ensure_work_mode(kWorkModeActive, "ActiveMode");
}

UHF_API int UHF_CALL UHF_SetWorkModeTrigger(void) {
  set_last_error("Trigger mode not supported by vendor software", UHF_ERR_NOT_SUPPORTED);
  return 0;
}

UHF_API int UHF_CALL UHF_StartRead(void) {
  if (g_is_reading) {
    set_last_error("Read already started", UHF_ERR_ALREADY_READING);
    return 0;
  }
  if (!ensure_work_mode(kWorkModeActive, "ActiveMode")) {
    // If workmode can't be set (device in special mode), still try to start.
    int ok_fallback = start_read_vendor(0);
    if (ok_fallback) {
      g_is_reading = 1;
      set_last_error("", UHF_ERR_OK);
      return 1;
    }
    return 0;
  }
  int ok = start_read_vendor(1);
  if (ok) g_is_reading = 1;
  return ok;
}

UHF_API int UHF_CALL UHF_StopRead(void) {
  if (!g_is_reading) {
    set_last_error("Read not started", UHF_ERR_NOT_READING);
    return 0;
  }
  int ok = stop_read_vendor(1);
  if (ok) g_is_reading = 0;
  return ok;
}

UHF_API int UHF_CALL UHF_ClearBuffer(void) {
  if (!ensure_usb_config_ready()) {
    return 0;
  }
  using Fn = int (VENDOR_CALL*)();
  auto fn = load_fn<Fn>("SWHid_ClearTagBuf");
  if (!fn) {
    set_last_error("Missing SWHid_ClearTagBuf", UHF_ERR_VENDOR_MISSING);
    return 0;
  }
  int ok = fn() ? 1 : 0;
  // Vendor DLL sometimes returns 0 even when buffer is cleared. Treat as best-effort.
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  dedup_cache_reset_locked();
  set_last_error("", UHF_ERR_OK);
  return 1;
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
  *out_len = 0;
  *out_count = 0;
  uint8_t ret = fn(buf, out_len, out_count);
  if (ret != 0) {
    return 1;
  }
  if (!UHF_IsReaderPresent()) {
    set_last_error("Reader not present", UHF_ERR_NO_DEVICE);
    return 0;
  }
  return 1;
}

static int peek_or_pop(int dedup, int do_clear, int safe_cycle,
                       UHF_Tag* outTags, int maxTags, int* outCount) {
  uint8_t buf[65536];
  int total_len = 0;
  int tag_num = 0;
  int did_stop = 0;
  int was_reading = g_is_reading;

  if (safe_cycle) {
    if (was_reading) {
      if (stop_read_vendor(0)) {
        g_is_reading = 0;
        did_stop = 1;
      }
    }
  }

  int ok = get_tag_buf(buf, sizeof(buf), &total_len, &tag_num);
  if (!ok) {
    if (outCount) *outCount = 0;
    if (safe_cycle) {
      if (was_reading && did_stop) {
        if (start_read_vendor(0)) g_is_reading = 1;
      }
    }
    return 0;
  }

  if (total_len <= 0) {
    if (outCount) *outCount = 0;
    if (do_clear) {
      UHF_ClearBuffer();
    }
    if (safe_cycle) {
      if (was_reading && did_stop) {
        if (start_read_vendor(0)) g_is_reading = 1;
      }
    }
    return 1;
  }

  ok = parse_tag_buffer(buf, total_len, dedup, outTags, maxTags, outCount);
  if (do_clear) {
    UHF_ClearBuffer();
  }
  if (safe_cycle) {
    if (was_reading && did_stop) {
      if (start_read_vendor(0)) g_is_reading = 1;
    }
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
  int rssi_min = std::numeric_limits<int>::min();
  int rssi_max = std::numeric_limits<int>::max();
  {
    std::lock_guard<std::mutex> lock(g_dedup_mutex);
    window_ms = g_dedup_window_ms;
    rssi_min = g_rssi_min_dbm;
    rssi_max = g_rssi_max_dbm;
  }
  int filter_rssi = (rssi_min != std::numeric_limits<int>::min()) ||
                    (rssi_max != std::numeric_limits<int>::max());
  if (window_ms <= 0 && !filter_rssi) {
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
  if (window_ms <= 0) {
    int out = 0;
    for (int i = 0; i < count && out < maxTags; ++i) {
      if (tmp[i].rssiDbm < rssi_min || tmp[i].rssiDbm > rssi_max) {
        continue;
      }
      outTags[out++] = tmp[i];
    }
    *outCount = out;
    return 1;
  }
  int64_t now = now_ms();
  int out = 0;
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  for (int i = 0; i < count && out < maxTags; ++i) {
    if (tmp[i].rssiDbm < rssi_min || tmp[i].rssiDbm > rssi_max) {
      continue;
    }
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
#if defined(_WIN64)
  if (!UHF_IsOpen()) {
    if (read_once_via_helper(timeoutMs, outTags, maxTags, outCount)) {
      set_last_error("", UHF_ERR_OK);
      return 1;
    }
  }
#endif
  if (timeoutMs < 0) timeoutMs = 0;
  int mode = UHF_GetWorkMode();
  if (mode == kWorkModeAnswer || mode < 0) {
    int ok = inventory_g2_once(timeoutMs, outTags, maxTags, outCount);
    if (ok && *outCount > 0) {
      return 1;
    }
  }
  if (!UHF_ClearBuffer()) {
    set_last_error("ClearBuffer failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (!UHF_StartRead()) {
    // Fallback to Answer-mode inventory when buffered read is not available.
    set_last_error("", UHF_ERR_OK);
    int ok = inventory_g2_once(timeoutMs, outTags, maxTags, outCount);
    if (!ok) {
      set_last_error("", UHF_ERR_OK);
      return inventory_g2_once_no_mode(timeoutMs, outTags, maxTags, outCount);
    }
    return ok;
  }
  sleep_ms(timeoutMs);
  UHF_StopRead();
  return UHF_PopBufferDedupFiltered(outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_RssiFilterSet(int minDbm, int maxDbm) {
  if (minDbm > maxDbm) {
    set_last_error("minDbm must be <= maxDbm", UHF_ERR_INVALID_ARG);
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  g_rssi_min_dbm = minDbm;
  g_rssi_max_dbm = maxDbm;
  return 1;
}

UHF_API int UHF_CALL UHF_RssiFilterReset(void) {
  std::lock_guard<std::mutex> lock(g_dedup_mutex);
  g_rssi_min_dbm = std::numeric_limits<int>::min();
  g_rssi_max_dbm = std::numeric_limits<int>::max();
  return 1;
}

UHF_API int UHF_CALL UHF_GetPowerDbm(void) {
  if (!ensure_usb_config_ready()) {
    return -1;
  }
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
  if (!ensure_usb_config_ready()) {
    return 0;
  }
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
  if (!fn(0xFF, 0x05, static_cast<uint8_t>(dbm))) {
    // Some firmwares only accept power changes via full device param block.
    if (write_device_param_block(0x05, static_cast<uint8_t>(dbm))) {
      set_last_error("", UHF_ERR_OK);
      return 1;
    }
    set_last_error("SWHid_SetDeviceOneParam failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  return 1;
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
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
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
  if (count <= 0) {
    set_last_error("No tag detected; place a single tag", UHF_ERR_NO_TAG);
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
    if (count <= 0) {
      set_last_error("No tag detected; place a single tag", UHF_ERR_NO_TAG);
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
    if (count <= 0) {
      set_last_error("No tag detected; place a single tag", UHF_ERR_NO_TAG);
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
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
    return 0;
  }
  uint8_t epc[12] = {0};
  int epc_len = hex_to_bytes(epcHex, epc, sizeof(epc));
  if (epc_len <= 0 || epc_len > 12) {
    set_last_error("Select EPC requires 1..12 bytes (24 hex chars for 96-bit EPC)", UHF_ERR_INVALID_ARG);
    return 0;
  }
  uint8_t frame[sizeof(kMaskSelectTemplate)] = {0};
  if (!build_mask_frame(epc, epc_len, 1, frame, sizeof(frame))) {
    set_last_error("Failed to build mask frame", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int frame_ok = send_buffer(frame, static_cast<int>(sizeof(frame)));
  if (frame_ok) {
    return 1;
  }
  set_last_error("Mask select failed", UHF_ERR_VENDOR_CALL_FAILED);
  return 0;
}

UHF_API int UHF_CALL UHF_ClearSelect(void) {
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
    return 0;
  }
  uint8_t frame[sizeof(kMaskSelectTemplate)] = {0};
  if (!build_mask_frame(nullptr, 0, 0, frame, sizeof(frame))) {
    set_last_error("Failed to build clear mask frame", UHF_ERR_INVALID_ARG);
    return 0;
  }
  int frame_ok = send_buffer(frame, static_cast<int>(sizeof(frame)));
  if (frame_ok) {
    return 1;
  }
  set_last_error("Mask clear failed", UHF_ERR_VENDOR_CALL_FAILED);
  return 0;
}

UHF_API int UHF_CALL UHF_CalibrationTagPrepare(const char* desiredEpcHex, int writeNew,
                                               char* outEpcHex, int outEpcLen) {
  if (outEpcHex && outEpcLen <= 0) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  // Ensure firmware is not left streaming from a previous session.
  stop_read_vendor(0);
  g_is_reading = 0;
  char epcHex[UHF_MAX_EPC_HEX + 1] = {0};
  if (writeNew) {
    UHF_Tag tags[4];
    int count = 0;
    if (!read_tags_once(200, tags, 4, &count)) {
      set_last_error("Read tags failed", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    if (count <= 0) {
      set_last_error("No tag detected", UHF_ERR_NO_TAG);
      return 0;
    }
    if (count > 1) {
      int one = extract_single_epc(tags, count, epcHex, sizeof(epcHex));
      if (one < 0) {
        set_last_error("Multiple tags detected; remove extras", UHF_ERR_MULTI_TAG);
        return 0;
      }
    }
    if (desiredEpcHex && desiredEpcHex[0]) {
      uint8_t tmp[UHF_MAX_EPC_BYTES] = {0};
      int len = hex_to_bytes(desiredEpcHex, tmp, sizeof(tmp));
      if (len <= 0 || (len % 2) != 0) {
        set_last_error("Invalid EPC hex", UHF_ERR_INVALID_ARG);
        return 0;
      }
#if defined(_MSC_VER)
      strncpy_s(epcHex, desiredEpcHex, sizeof(epcHex) - 1);
#else
      strncpy(epcHex, desiredEpcHex, sizeof(epcHex) - 1);
      epcHex[sizeof(epcHex) - 1] = '\0';
#endif
    } else {
      if (!generate_random_epc_hex(epcHex, sizeof(epcHex), 12)) {
        set_last_error("Failed to generate EPC", UHF_ERR_UNKNOWN);
        return 0;
      }
    }
    if (outEpcHex) {
      size_t len = strlen(epcHex);
      if (outEpcLen <= static_cast<int>(len)) {
        set_last_error("Output buffer too small", UHF_ERR_INVALID_ARG);
        return 0;
      }
    }
    if (!UHF_WriteEpc(epcHex, "")) {
      return 0;
    }
  } else {
    UHF_Tag tags[8];
    int count = 0;
    if (!read_tags_once(200, tags, 8, &count)) {
      set_last_error("Read tags failed", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    if (count <= 0) {
      set_last_error("No tag detected", UHF_ERR_NO_TAG);
      return 0;
    }
    if (count > 1) {
      int one = extract_single_epc(tags, count, epcHex, sizeof(epcHex));
      if (one < 0) {
        set_last_error("Multiple tags detected; remove extras or write new EPC", UHF_ERR_MULTI_TAG);
        return 0;
      }
    } else {
#if defined(_MSC_VER)
      strncpy_s(epcHex, tags[0].epc, sizeof(epcHex) - 1);
#else
      strncpy(epcHex, tags[0].epc, sizeof(epcHex) - 1);
      epcHex[sizeof(epcHex) - 1] = '\0';
#endif
    }
  }
  if (outEpcHex) {
    size_t len = strlen(epcHex);
    if (outEpcLen <= static_cast<int>(len)) {
      set_last_error("Output buffer too small", UHF_ERR_INVALID_ARG);
      return 0;
    }
#if defined(_MSC_VER)
    strncpy_s(outEpcHex, outEpcLen, epcHex, _TRUNCATE);
#else
    strncpy(outEpcHex, epcHex, static_cast<size_t>(outEpcLen - 1));
    outEpcHex[outEpcLen - 1] = '\0';
#endif
  }
  return 1;
}

UHF_API int UHF_CALL UHF_CalibrateByTag(const char* targetEpcHex,
                                        int powerMinDbm, int powerMaxDbm, int powerStepDbm,
                                        int readsPerStep, int powerMarginDbm,
                                        int captureMs, int rssiMarginDbm,
                                        int applySettings, UHF_CalibrationResult* outResult) {
  if (!outResult) {
    set_last_error("Invalid output pointer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  memset(outResult, 0, sizeof(*outResult));
  // Ensure firmware is not left streaming from a previous session.
  stop_read_vendor(0);
  g_is_reading = 0;
  if (powerStepDbm <= 0) {
    set_last_error("Invalid power step", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (powerMinDbm < 0) powerMinDbm = 0;
  if (powerMaxDbm > 26) powerMaxDbm = 26;
  if (powerMinDbm > powerMaxDbm) {
    set_last_error("Invalid power range/step", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (readsPerStep <= 0 || captureMs <= 0 || powerMarginDbm < 0 || rssiMarginDbm < 0) {
    set_last_error("Invalid calibration parameters", UHF_ERR_INVALID_ARG);
    return 0;
  }

  char targetHex[UHF_MAX_EPC_HEX + 1] = {0};
  if (!targetEpcHex || !targetEpcHex[0]) {
    UHF_Tag tags[4];
    int count = 0;
    if (!read_tags_active_window(500, tags, 4, &count)) {
      set_last_error("Read tags failed", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    if (count <= 0) {
      set_last_error("No tag detected for calibration", UHF_ERR_NO_TAG);
      return 0;
    }
    if (count > 1) {
      int one = extract_single_epc(tags, count, targetHex, sizeof(targetHex));
      if (one < 0) {
        set_last_error("Multiple tags detected; provide target EPC", UHF_ERR_MULTI_TAG);
        return 0;
      }
    } else {
#if defined(_MSC_VER)
      strncpy_s(targetHex, tags[0].epc, sizeof(targetHex) - 1);
#else
      strncpy(targetHex, tags[0].epc, sizeof(targetHex) - 1);
      targetHex[sizeof(targetHex) - 1] = '\0';
#endif
    }
  } else {
    uint8_t tmp[UHF_MAX_EPC_BYTES] = {0};
    int len = hex_to_bytes(targetEpcHex, tmp, sizeof(tmp));
    if (len <= 0 || (len % 2) != 0) {
      set_last_error("Invalid target EPC hex", UHF_ERR_INVALID_ARG);
      return 0;
    }
#if defined(_MSC_VER)
    strncpy_s(targetHex, targetEpcHex, sizeof(targetHex) - 1);
#else
    strncpy(targetHex, targetEpcHex, sizeof(targetHex) - 1);
    targetHex[sizeof(targetHex) - 1] = '\0';
#endif
  }

  if (!calibration_verify_unique_epc(targetHex, 5)) {
    return 0;
  }

  if (!UHF_EnsureUsbTransport()) {
    set_last_error("Failed to ensure USB transport for calibration", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }

  // Ensure no lingering mask selection before calibration.
  UHF_ClearSelect();

  int prevPower = UHF_GetPowerDbm();
  int selection_enabled = 0;
  // NOTE: We intentionally avoid hardware mask selection during calibration.
  // Some firmwares reject power changes while a mask is active. We filter
  // by EPC in software during reads instead.

  auto set_power_cal = [&](int value) -> int {
    // Ensure RF is stopped before power changes.
    stop_read_vendor(0);
    g_is_reading = 0;
    // Some firmwares only accept power changes in Active mode.
    (void)UHF_SetWorkModeAnswer();
    if (UHF_SetPowerDbm(value)) {
      return 1;
    }
    (void)UHF_SetWorkModeActive();
    if (UHF_SetPowerDbm(value)) {
      return 1;
    }
    // Some firmwares refuse power changes while mask select is active.
    UHF_ClearSelect();
    selection_enabled = 0;
    if (UHF_SetPowerDbm(value)) {
      return 1;
    }
    (void)UHF_SetWorkModeActive();
    if (UHF_SetPowerDbm(value)) {
      return 1;
    }
    char msg[256];
    const char* err = UHF_GetLastError();
    if (err && err[0]) {
      snprintf(msg, sizeof(msg),
               "SetPowerDbm failed during calibration (dbm=%d): %s", value, err);
    } else {
      snprintf(msg, sizeof(msg),
               "SetPowerDbm failed during calibration (dbm=%d)", value);
    }
    set_last_error(msg, UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  };

  int multi_env = 0;
  {
    UHF_Tag env_tags[16];
    int env_count = 0;
    if (set_power_cal(powerMaxDbm)) {
      if (read_tags_active_window(500, env_tags, 16, &env_count) && env_count > 1) {
        multi_env = 1;
      }
    }
  }
  int minHits = readsPerStep;
  if (multi_env && readsPerStep >= 2) {
    minHits = readsPerStep - 1;
  }
  if (minHits < 1) minHits = 1;
  int foundPower = -1;
  const int per_read_ms = 300;
  int per_read_ms_cal = per_read_ms;
  if (per_read_ms_cal < 500) per_read_ms_cal = 500;
  for (int p = powerMaxDbm; p >= powerMinDbm; p -= powerStepDbm) {
    if (!set_power_cal(p)) {
      if (selection_enabled) UHF_ClearSelect();
      if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
      return 0;
    }
    sleep_ms(50); // allow RF power to settle
    int hits = 0;
    for (int i = 0; i < readsPerStep; ++i) {
      UHF_Tag tags[32];
      int count = 0;
      if (!read_tags_active_window(per_read_ms_cal, tags, 32, &count)) {
        continue;
      }
      for (int t = 0; t < count; ++t) {
        if (!targetHex[0] || epc_hex_equal(tags[t].epc, targetHex)) {
          hits++;
          break;
        }
      }
    }
    if (hits >= minHits) {
      foundPower = p;
      // Keep going lower to find the minimum reliable power.
      continue;
    }
    if (foundPower >= 0) {
      break;
    }
  }

  if (foundPower < 0) {
    set_last_error("Calibration tag not detected in power range", UHF_ERR_CALIBRATION_FAILED);
    if (selection_enabled) UHF_ClearSelect();
    if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
    return 0;
  }

  int recommendedPower = foundPower + powerMarginDbm;
  if (recommendedPower > powerMaxDbm) recommendedPower = powerMaxDbm;
  if (recommendedPower < powerMinDbm) recommendedPower = powerMinDbm;
  if (!set_power_cal(recommendedPower)) {
    if (selection_enabled) UHF_ClearSelect();
    if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
    return 0;
  }

  if (!UHF_ClearBuffer()) {
    set_last_error("ClearBuffer failed", UHF_ERR_VENDOR_CALL_FAILED);
    if (selection_enabled) UHF_ClearSelect();
    if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
    return 0;
  }
  if (!UHF_StartRead()) {
    set_last_error("StartRead failed", UHF_ERR_VENDOR_CALL_FAILED);
    if (selection_enabled) UHF_ClearSelect();
    if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
    return 0;
  }
  sleep_ms(captureMs);
  UHF_StopRead();

  UHF_Tag tags[512];
  int count = 0;
  if (!UHF_PopBufferAll(tags, 512, &count)) {
    set_last_error("PopBufferAll failed", UHF_ERR_VENDOR_CALL_FAILED);
    if (selection_enabled) UHF_ClearSelect();
    if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
    return 0;
  }

  int sampleCount = 0;
  int rssiMin = 0;
  int rssiMax = 0;
  int64_t rssiSum = 0;
  for (int i = 0; i < count; ++i) {
    if (targetHex[0] && !epc_hex_equal(tags[i].epc, targetHex)) {
      continue;
    }
    if (sampleCount == 0) {
      rssiMin = tags[i].rssiDbm;
      rssiMax = tags[i].rssiDbm;
    } else {
      if (tags[i].rssiDbm < rssiMin) rssiMin = tags[i].rssiDbm;
      if (tags[i].rssiDbm > rssiMax) rssiMax = tags[i].rssiDbm;
    }
    rssiSum += tags[i].rssiDbm;
    sampleCount++;
  }
  if (sampleCount <= 0) {
    set_last_error("No RSSI samples captured", UHF_ERR_CALIBRATION_FAILED);
    if (selection_enabled) UHF_ClearSelect();
    if (prevPower >= 0) UHF_SetPowerDbm(prevPower);
    return 0;
  }
  int rssiAvg = static_cast<int>(rssiSum / sampleCount);
  int rssiFilterMin = rssiMin - rssiMarginDbm;
  int rssiFilterMax = rssiMax + rssiMarginDbm;

  if (applySettings) {
    UHF_RssiFilterSet(rssiFilterMin, rssiFilterMax);
  } else if (prevPower >= 0) {
    UHF_SetPowerDbm(prevPower);
  }

  if (selection_enabled) {
    UHF_ClearSelect();
  }

  outResult->minDetectPowerDbm = foundPower;
  outResult->recommendedPowerDbm = recommendedPower;
  outResult->powerMarginDbm = powerMarginDbm;
  outResult->rssiMinDbm = rssiMin;
  outResult->rssiMaxDbm = rssiMax;
  outResult->rssiAvgDbm = rssiAvg;
  outResult->rssiMarginDbm = rssiMarginDbm;
  outResult->rssiFilterMinDbm = rssiFilterMin;
  outResult->rssiFilterMaxDbm = rssiFilterMax;
  outResult->sampleCount = sampleCount;
  g_calibration = *outResult;
  g_has_calibration = 1;
  return 1;
}

UHF_API int UHF_CALL UHF_CalibrationApply(const UHF_CalibrationResult* res) {
  if (!res) {
    set_last_error("Invalid calibration pointer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (res->rssiFilterMinDbm > res->rssiFilterMaxDbm) {
    set_last_error("Invalid RSSI filter range", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!UHF_SetPowerDbm(res->recommendedPowerDbm)) {
    set_last_error("SetPowerDbm failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (!UHF_RssiFilterSet(res->rssiFilterMinDbm, res->rssiFilterMaxDbm)) {
    set_last_error("RssiFilterSet failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  g_calibration = *res;
  g_has_calibration = 1;
  return 1;
}

UHF_API int UHF_CALL UHF_CalibrationGetCurrent(UHF_CalibrationResult* outResult) {
  if (!outResult) {
    set_last_error("Invalid output pointer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!g_has_calibration) {
    set_last_error("Calibration not set", UHF_ERR_CALIBRATION_MISSING);
    return 0;
  }
  *outResult = g_calibration;
  return 1;
}

UHF_API int UHF_CALL UHF_CalibrationSave(const char* path) {
  if (!path || !path[0]) {
    set_last_error("Invalid file path", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!g_has_calibration) {
    set_last_error("Calibration not set", UHF_ERR_CALIBRATION_MISSING);
    return 0;
  }
  FILE* f = fopen(path, "wb");
  if (!f) {
    set_last_error("Failed to open calibration file for write", UHF_ERR_UNKNOWN);
    return 0;
  }
  fprintf(f, "# UHF_CALIBRATION_V1\n");
  fprintf(f, "minDetectPowerDbm=%d\n", g_calibration.minDetectPowerDbm);
  fprintf(f, "recommendedPowerDbm=%d\n", g_calibration.recommendedPowerDbm);
  fprintf(f, "powerMarginDbm=%d\n", g_calibration.powerMarginDbm);
  fprintf(f, "rssiMinDbm=%d\n", g_calibration.rssiMinDbm);
  fprintf(f, "rssiMaxDbm=%d\n", g_calibration.rssiMaxDbm);
  fprintf(f, "rssiAvgDbm=%d\n", g_calibration.rssiAvgDbm);
  fprintf(f, "rssiMarginDbm=%d\n", g_calibration.rssiMarginDbm);
  fprintf(f, "rssiFilterMinDbm=%d\n", g_calibration.rssiFilterMinDbm);
  fprintf(f, "rssiFilterMaxDbm=%d\n", g_calibration.rssiFilterMaxDbm);
  fprintf(f, "sampleCount=%d\n", g_calibration.sampleCount);
  fclose(f);
  return 1;
}

UHF_API int UHF_CALL UHF_CalibrationLoad(const char* path, int applySettings,
                                         UHF_CalibrationResult* outResult) {
  if (!path || !path[0]) {
    set_last_error("Invalid file path", UHF_ERR_INVALID_ARG);
    return 0;
  }
  FILE* f = fopen(path, "rb");
  if (!f) {
    set_last_error("Failed to open calibration file", UHF_ERR_UNKNOWN);
    return 0;
  }
  UHF_CalibrationResult res{};
  int got_rec = 0;
  int got_min = 0;
  int got_max = 0;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    char key[64] = {0};
    char val[64] = {0};
    if (sscanf(line, "%63[^=]=%63s", key, val) != 2) {
      continue;
    }
    int v = atoi(val);
    if (strcmp(key, "minDetectPowerDbm") == 0) res.minDetectPowerDbm = v;
    else if (strcmp(key, "recommendedPowerDbm") == 0) { res.recommendedPowerDbm = v; got_rec = 1; }
    else if (strcmp(key, "powerMarginDbm") == 0) res.powerMarginDbm = v;
    else if (strcmp(key, "rssiMinDbm") == 0) res.rssiMinDbm = v;
    else if (strcmp(key, "rssiMaxDbm") == 0) res.rssiMaxDbm = v;
    else if (strcmp(key, "rssiAvgDbm") == 0) res.rssiAvgDbm = v;
    else if (strcmp(key, "rssiMarginDbm") == 0) res.rssiMarginDbm = v;
    else if (strcmp(key, "rssiFilterMinDbm") == 0) { res.rssiFilterMinDbm = v; got_min = 1; }
    else if (strcmp(key, "rssiFilterMaxDbm") == 0) { res.rssiFilterMaxDbm = v; got_max = 1; }
    else if (strcmp(key, "sampleCount") == 0) res.sampleCount = v;
  }
  fclose(f);
  if (!got_rec || !got_min || !got_max) {
    set_last_error("Invalid calibration file", UHF_ERR_CALIBRATION_FAILED);
    return 0;
  }
  g_calibration = res;
  g_has_calibration = 1;
  if (applySettings) {
    if (!UHF_CalibrationApply(&res)) {
      return 0;
    }
  }
  if (outResult) {
    *outResult = res;
  }
  return 1;
}

UHF_API int UHF_CALL UHF_ReadOnceCalibrated(int timeoutMs, UHF_Tag* outTags, int maxTags, int* outCount) {
  if (!g_has_calibration) {
    set_last_error("Calibration not set", UHF_ERR_CALIBRATION_MISSING);
    return 0;
  }
  if (!UHF_CalibrationApply(&g_calibration)) {
    return 0;
  }
  return UHF_ReadOnce(timeoutMs, outTags, maxTags, outCount);
}

UHF_API int UHF_CALL UHF_ReadStreamCalibrated(int durationMs, UHF_Tag* outTags, int maxTags, int* outCount) {
  if (!outTags || maxTags <= 0 || !outCount) {
    set_last_error("Invalid output buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  if (!g_has_calibration) {
    set_last_error("Calibration not set", UHF_ERR_CALIBRATION_MISSING);
    return 0;
  }
  if (durationMs < 0) durationMs = 0;
  if (!UHF_CalibrationApply(&g_calibration)) {
    return 0;
  }
  if (!UHF_ClearBuffer()) {
    set_last_error("ClearBuffer failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  if (!UHF_StartRead()) {
    set_last_error("StartRead failed", UHF_ERR_VENDOR_CALL_FAILED);
    return 0;
  }
  sleep_ms(durationMs);
  UHF_StopRead();
  return UHF_PopBufferDedupFiltered(outTags, maxTags, outCount);
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
  if (!ensure_work_mode(kWorkModeAnswer, "AnswerMode")) {
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

static const char* transport_name(uint8_t val) {
  switch (val) {
    case kTransportUsb: return "USB/PC";
    case kTransportRs232: return "RS232/RS485";
    case kTransportRj45: return "RJ45";
    case kTransportWifi: return "WIFI";
    case kTransportWeigand: return "Weigand";
    default: return "Unknown";
  }
}

static const char* workmode_name(uint8_t val) {
  switch (val) {
    case kWorkModeAnswer: return "Answer";
    case kWorkModeActive: return "Active";
    case kWorkModeTrigger: return "Trigger";
    default: return "Unknown";
  }
}

static void close_device_silent(void) {
  using Fn = int (VENDOR_CALL*)();
  auto fn = load_fn<Fn>("SWHid_CloseDevice");
  if (fn) {
    (void)fn();
  }
  g_is_open = 0;
  g_is_reading = 0;
}

static int check_system_config_internal(char* outMsg, int outMsgLen, int keep_open) {
  if (!outMsg || outMsgLen <= 0) {
    set_last_error("Invalid buffer", UHF_ERR_INVALID_ARG);
    return 0;
  }
  outMsg[0] = '\0';

  int usb_count = 0;
  int already_open = UHF_IsOpen();
  if (!already_open) {
    usb_count = UHF_GetUsbCount();
    if (usb_count <= 0) {
      snprintf(outMsg, outMsgLen, "USB devices: %d (none)", usb_count);
      set_last_error("No USB device found", UHF_ERR_NO_DEVICE);
      return 0;
    }
  } else {
    usb_count = 1;
  }

  int opened_here = 0;
  if (!already_open) {
    if (!UHF_Open(0)) {
      const char* err = UHF_GetLastError();
      snprintf(outMsg,
               outMsgLen,
               "USB devices: %d; transport=unknown (open failed%s%s)",
               usb_count,
               (err && err[0]) ? ": " : "",
               (err && err[0]) ? err : "");
      set_last_error("Open failed for config check", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    opened_here = 1;
  }

  using Fn = int (VENDOR_CALL*)(uint8_t, uint8_t, uint8_t*);
  auto fn = load_fn<Fn>("SWHid_ReadDeviceOneParam");
  if (!fn) {
    snprintf(outMsg, outMsgLen, "USB devices: %d; transport=unknown (missing param reader)", usb_count);
    if (opened_here) close_device_silent();
    set_last_error("Missing SWHid_ReadDeviceOneParam", UHF_ERR_VENDOR_MISSING);
    return 0;
  }

  uint8_t transport = 0xFF;
  if (!fn(0xFF, kParamTransport, &transport)) {
    snprintf(outMsg, outMsgLen, "USB devices: %d; transport=read failed; workmode=unknown", usb_count);
    if (opened_here && !keep_open) close_device_silent();
    // Some firmware builds don't support ReadDeviceOneParam; treat as warning.
    set_last_error("", UHF_ERR_OK);
    return 1;
  }

  uint8_t workmode = 0xFF;
  if (!fn(0xFF, kParamWorkMode, &workmode)) {
    workmode = 0xFF;
  }

  snprintf(outMsg,
           outMsgLen,
           "USB devices: %d; transport=%u (%s); workmode=%u (%s)",
           usb_count,
           static_cast<unsigned>(transport),
           transport_name(transport),
           static_cast<unsigned>(workmode),
           workmode_name(workmode));

  if (transport != kTransportUsb) {
    if (opened_here) close_device_silent();
    set_last_error("Transport is not USB (set interface to USB)", UHF_ERR_VERIFY_FAILED);
    return 0;
  }

  if (opened_here && !keep_open) close_device_silent();
  set_last_error("", UHF_ERR_OK);
  return 1;
}

static int ensure_usb_config_ready(void) {
  int usb_count = 0;
  int already_open = UHF_IsOpen();
  if (!already_open) {
    usb_count = UHF_GetUsbCount();
    if (usb_count <= 0) {
      set_last_error("No USB device found", UHF_ERR_NO_DEVICE);
      return 0;
    }
  }
  int opened_here = 0;
  if (!already_open) {
    if (!UHF_Open(0)) {
      set_last_error("Open failed for USB config check", UHF_ERR_VENDOR_CALL_FAILED);
      return 0;
    }
    opened_here = 1;
  }
  if (!ensure_transport_usb_opened(1)) {
    if (opened_here) {
      close_device_silent();
    }
    return 0;
  }
  set_last_error("", UHF_ERR_OK);
  return 1;
}
