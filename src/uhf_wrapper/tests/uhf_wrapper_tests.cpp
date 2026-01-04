#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../uhf_wrapper.h"

static int expect_ok(int ok, const char* msg) {
  if (!ok) {
    const char* err = UHF_GetLastError();
    std::fprintf(stderr, "FAIL: %s\n", msg);
    if (err && err[0]) {
      std::fprintf(stderr, "  last_error: %s\n", err);
    }
    return 0;
  }
  return 1;
}

int main() {
  int all_ok = 1;

  all_ok &= expect_ok(UHF_Init() != 0, "UHF_Init");

  // Try to open device 0; if no device, exit cleanly for CI environments.
  if (!UHF_Open(0)) {
    const char* err = UHF_GetLastError();
    if (err && std::strstr(err, "No USB device") != nullptr) {
      std::printf("SKIP: no USB device available\n");
      UHF_Shutdown();
      return 0;
    }
    std::fprintf(stderr, "FAIL: UHF_Open(0)\n");
    if (err && err[0]) {
      std::fprintf(stderr, "  last_error: %s\n", err);
    }
    UHF_Shutdown();
    return 1;
  }

  // Basic API sanity checks (should not crash, should return a value).
  char info[256] = {0};
  (void)UHF_GetInfo(info, sizeof(info));
  (void)UHF_GetPowerDbm();

  // WorkMode getters should not crash even if unsupported.
  (void)UHF_GetWorkMode();

  all_ok &= expect_ok(UHF_Close() != 0, "UHF_Close");
  UHF_Shutdown();

  return all_ok ? 0 : 1;
}
