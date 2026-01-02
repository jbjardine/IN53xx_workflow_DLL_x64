#include "uhf_wrapper.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void print_tag_json(const UHF_Tag* t) {
  if (!t) return;
  printf("{\"epc\":\"%s\",\"len\":%u,\"rssi\":%d,\"ant\":%u,\"tagType\":%u",
         t->epc, t->epcLenBytes, t->rssiDbm, t->antenna, t->tagType);
  if (t->hasTs) {
    printf(",\"ts\":\"");
    for (int i = 0; i < UHF_TS_LEN; ++i) {
      printf("%02X", t->tsRaw[i]);
    }
    printf("\"");
  }
  printf("}\n");
}

int main(int argc, char** argv) {
  int timeout_ms = 3000;
  int index = 0;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
      timeout_ms = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) {
      index = atoi(argv[++i]);
    }
  }

  if (!UHF_Init()) {
    fprintf(stderr, "Init failed: %s\n", UHF_GetLastError());
    return 1;
  }
  if (!UHF_Open((unsigned short)index)) {
    fprintf(stderr, "Open failed: %s\n", UHF_GetLastError());
    UHF_Shutdown();
    return 1;
  }

  // Force Answer mode for single-shot reads.
  (void)UHF_SetWorkModeAnswer();

  UHF_Tag tags[256];
  int count = 0;
  int ok = UHF_ReadOnce(timeout_ms, tags, 256, &count);
  if (!ok) {
    fprintf(stderr, "ReadOnce failed: %s\n", UHF_GetLastError());
    UHF_Close();
    UHF_Shutdown();
    return 1;
  }

  for (int i = 0; i < count; ++i) {
    print_tag_json(&tags[i]);
  }

  UHF_Close();
  UHF_Shutdown();
  return (count == 0) ? 2 : 0;
}
