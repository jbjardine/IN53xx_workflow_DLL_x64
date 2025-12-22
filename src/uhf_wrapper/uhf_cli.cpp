#include "uhf_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <chrono>
#include <thread>

enum OutputFormat {
  OUT_TEXT = 0,
  OUT_JSON = 1,
  OUT_CSV = 2,
};

struct CliOptions {
  int index = 0;
  OutputFormat out = OUT_TEXT;
  int compact = 0;
  int friendly = 0;
  int timeout_ms = 500;
  int interval_ms = 200;
  int duration_ms = 0;
  int dedup = 1;
  int safe = 0;
  int min_rssi = -1000;
  int antenna = -1;
  char epc_prefix[UHF_MAX_EPC_HEX + 1] = {0};
  int epc_prefix_len = 0;
};

static void usage() {
  printf("UhfWrapper CLI\n");
  printf("Usage: uhf_cli [options] <cmd> [args]\n\n");
  printf("Options:\n");
  printf("  -i <index>\n");
  printf("  --json | --csv | --text\n");
  printf("  --compact\n");
  printf("  --friendly\n");
  printf("  --timeout <ms>    (read-once/read-count)\n");
  printf("  --interval <ms>   (read-stream/read-count loop)\n");
  printf("  --duration <ms>   (read-stream total, 0=forever)\n");
  printf("  --dedup | --all   (default: dedup)\n");
  printf("  --safe            (use pop-*-safe)\n");
  printf("  --min-rssi <dbm>\n");
  printf("  --epc-prefix <hex>\n");
  printf("  --antenna <n>\n\n");

  printf("Commands:\n");
  printf("  count\n");
  printf("  usbinfo\n");
  printf("  status\n");
  printf("  open\n");
  printf("  close\n");
  printf("  info\n");
  printf("  start\n");
  printf("  stop\n");
  printf("  buffer-clear\n");
  printf("  peek-all\n");
  printf("  peek-dedup\n");
  printf("  pop-all\n");
  printf("  pop-dedup\n");
  printf("  pop-all-safe\n");
  printf("  pop-dedup-safe\n");
  printf("  read-once\n");
  printf("  read-count <n>\n");
  printf("  read-stream\n");
  printf("  power-get\n");
  printf("  power-set <dbm>\n");
  printf("  power-get-pct\n");
  printf("  power-set-pct <pct>\n");
  printf("  power-set-preset <low|med|high>\n");
  printf("  relay-on\n");
  printf("  relay-off\n");
  printf("  freq-get\n");
  printf("  freq-set <byte0> <byte1>   (hex like 0x31 0x80 or decimal)\n");
  printf("  freq-set-region <EU|US|JP|CN|KR|AU|NZ|IN|SG|HK|TW|CA|MX|BR|IL|ZA|TH|MY>\n");
  printf("  read <bank> <wordPtr> <wordCount> <pwdHex>\n");
  printf("  write <bank> <wordPtr> <hexData> <pwdHex>\n");
  printf("  write-epc <epcHex> <pwdHex>\n");
}

static int parse_hex(const char* hex, unsigned char* out, int out_cap) {
  int len = 0;
  int hi = -1;
  for (const char* p = hex; *p; ++p) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') continue;
    if (p == hex && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
      ++p;
      continue;
    }
    int v = -1;
    if (*p >= '0' && *p <= '9') v = *p - '0';
    else if (*p >= 'a' && *p <= 'f') v = 10 + (*p - 'a');
    else if (*p >= 'A' && *p <= 'F') v = 10 + (*p - 'A');
    if (v < 0) return -1;
    if (hi < 0) {
      hi = v;
    } else {
      if (len >= out_cap) return -1;
      out[len++] = (unsigned char)((hi << 4) | v);
      hi = -1;
    }
  }
  if (hi >= 0) return -1;
  return len;
}

static int normalize_hex(const char* hex, char* out, int out_cap) {
  int len = 0;
  for (const char* p = hex; *p; ++p) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') continue;
    if (p == hex && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
      ++p;
      continue;
    }
    char c = *p;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      if (len + 1 >= out_cap) return -1;
      out[len++] = (char)toupper((unsigned char)c);
    } else {
      return -1;
    }
  }
  out[len] = '\0';
  return len;
}

static int parse_byte(const char* s, unsigned char* out) {
  if (!s || !out) return 0;
  char* end = nullptr;
  long v = 0;
  if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
    v = strtol(s + 2, &end, 16);
  } else {
    v = strtol(s, &end, 10);
  }
  if (end == s || v < 0 || v > 255) return 0;
  *out = (unsigned char)v;
  return 1;
}

static void print_hex(const unsigned char* data, int len) {
  for (int i = 0; i < len; ++i) {
    printf("%02X", data[i]);
  }
  printf("\n");
}

static const char* yn(int v) { return v ? "YES" : "NO"; }

static void print_friendly_bool(const char* label, int v) {
  printf("%s: %s\n", label, yn(v));
}

static void print_friendly_ok(const char* label, int ok) {
  printf("%s: %s\n", label, ok ? "OK" : "FAILED");
}

static void print_friendly_int(const char* label, int v) {
  printf("%s: %d\n", label, v);
}

static void bytes_to_hex_str(const unsigned char* data, int len, char* out, int out_cap) {
  if (!out || out_cap <= 0) return;
  int pos = 0;
  for (int i = 0; i < len && pos + 2 < out_cap; ++i) {
    unsigned char b = data[i];
    out[pos++] = (char)((b >> 4) < 10 ? '0' + (b >> 4) : 'A' + ((b >> 4) - 10));
    out[pos++] = (char)((b & 0x0F) < 10 ? '0' + (b & 0x0F) : 'A' + ((b & 0x0F) - 10));
  }
  out[pos] = '\0';
}

static int tag_matches(const UHF_Tag* t, const CliOptions& opt) {
  if (!t) return 0;
  if (opt.min_rssi > -1000 && t->rssiDbm < opt.min_rssi) return 0;
  if (opt.antenna >= 0 && (int)t->antenna != opt.antenna) return 0;
  if (opt.epc_prefix_len > 0) {
    for (int i = 0; i < opt.epc_prefix_len; ++i) {
      char a = (char)toupper((unsigned char)t->epc[i]);
      if (a != opt.epc_prefix[i]) return 0;
    }
  }
  return 1;
}

static void print_tag_text(const UHF_Tag* t, const CliOptions& opt) {
  if (!t) return;
  if (opt.compact) {
    printf("%s\n", t->epc);
    return;
  }
  printf("EPC=%s len=%u rssi=%d ant=%u tagType=0x%02X", t->epc, t->epcLenBytes,
         t->rssiDbm, t->antenna, t->tagType);
  if (t->hasTs) {
    printf(" ts=");
    for (int i = 0; i < UHF_TS_LEN; ++i) {
      printf("%02X", t->tsRaw[i]);
    }
  }
  printf("\n");
}

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

static void print_tag_csv(const UHF_Tag* t, int* header_done) {
  if (!t) return;
  if (header_done && !*header_done) {
    printf("epc,len,rssi,ant,tagType,ts\n");
    *header_done = 1;
  }
  printf("%s,%u,%d,%u,%u,", t->epc, t->epcLenBytes, t->rssiDbm, t->antenna, t->tagType);
  if (t->hasTs) {
    for (int i = 0; i < UHF_TS_LEN; ++i) {
      printf("%02X", t->tsRaw[i]);
    }
  }
  printf("\n");
}

static void print_tag(const UHF_Tag* t, const CliOptions& opt, int* csv_header_done) {
  if (!t) return;
  if (opt.out == OUT_JSON) {
    print_tag_json(t);
  } else if (opt.out == OUT_CSV) {
    print_tag_csv(t, csv_header_done);
  } else {
    print_tag_text(t, opt);
  }
}

static int pop_tags(UHF_Tag* tags, int max_tags, int* out_count, const CliOptions& opt) {
  if (opt.safe) {
    return opt.dedup ? UHF_PopBufferDedupSafe(tags, max_tags, out_count)
                     : UHF_PopBufferAllSafe(tags, max_tags, out_count);
  }
  return opt.dedup ? UHF_PopBufferDedup(tags, max_tags, out_count)
                   : UHF_PopBufferAll(tags, max_tags, out_count);
}

static int peek_tags(UHF_Tag* tags, int max_tags, int* out_count, const CliOptions& opt) {
  return opt.dedup ? UHF_PeekBufferDedup(tags, max_tags, out_count)
                   : UHF_PeekBufferAll(tags, max_tags, out_count);
}

static int region_to_freq(const char* region, unsigned char* b0, unsigned char* b1) {
  if (!region || !b0 || !b1) return 0;
  char r[8] = {0};
  int n = 0;
  for (const char* p = region; *p && n < 7; ++p) {
    if (*p == '-' || *p == '_' || *p == ' ') continue;
    r[n++] = (char)toupper((unsigned char)*p);
  }
  r[n] = '\0';

  struct Map { const char* key; unsigned char b0; unsigned char b1; } map[] = {
    {"US", 0x31, 0x80}, {"USA", 0x31, 0x80}, {"CA", 0x31, 0x80}, {"MX", 0x31, 0x80},
    {"EU", 0x4E, 0x00}, {"EUR", 0x4E, 0x00},
    {"CN", 0x2C, 0xA3}, {"CHINA", 0x2C, 0xA3}, {"HK", 0x2C, 0xA3},
    {"KR", 0x29, 0x9D}, {"KOREA", 0x29, 0x9D},
    {"AU", 0x2E, 0x9F}, {"AUS", 0x2E, 0x9F}, {"AUSTRALIA", 0x2E, 0x9F},
    {"NZ", 0x4E, 0x00}, {"NEWZEALAND", 0x4E, 0x00},
    {"IN", 0x4E, 0x00}, {"INDIA", 0x4E, 0x00},
    {"SG", 0x2C, 0x81}, {"SINGAPORE", 0x2C, 0x81},
    {"TW", 0x31, 0xA7}, {"TAIWAN", 0x31, 0xA7},
    {"BR", 0x31, 0x99}, {"BRAZIL", 0x31, 0x99},
    {"IL", 0x1C, 0x99}, {"ISRAEL", 0x1C, 0x99},
    {"ZA", 0x24, 0x9D}, {"SOUTHAFRICA", 0x24, 0x9D},
    {"TH", 0x2C, 0xA3}, {"THAILAND", 0x2C, 0xA3},
    {"MY", 0x28, 0xA1}, {"MALAYSIA", 0x28, 0xA1},
    {"JP", 0x29, 0x9D}, {"JAPAN", 0x29, 0x9D},
  };

  for (const auto& it : map) {
    if (strcmp(r, it.key) == 0) {
      *b0 = it.b0;
      *b1 = it.b1;
      return 1;
    }
  }
  return 0;
}

static int handle_status(const CliOptions& opt) {
  int present = UHF_IsReaderPresent();
  int open = UHF_IsOpen();
  int connected = UHF_IsConnected();
  if (opt.out == OUT_JSON) {
    printf("{\"present\":%d,\"open\":%d,\"connected\":%d}\n", present, open, connected);
  } else if (opt.out == OUT_CSV) {
    printf("present,open,connected\n%d,%d,%d\n", present, open, connected);
  } else if (opt.friendly) {
    print_friendly_bool("Reader present", present);
    print_friendly_bool("Port open", open);
    print_friendly_bool("Connected", connected);
  } else {
    printf("present=%d open=%d connected=%d\n", present, open, connected);
  }
  if (!present) return 3;
  return 0;
}

static void sleep_ms(int ms) {
  if (ms <= 0) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 1;
  }

  CliOptions opt;
  int argi = 1;
  while (argi < argc) {
    const char* a = argv[argi];
    if (strcmp(a, "-i") == 0 && argi + 1 < argc) {
      opt.index = atoi(argv[++argi]);
      ++argi;
    } else if (strcmp(a, "--json") == 0) {
      opt.out = OUT_JSON;
      ++argi;
    } else if (strcmp(a, "--csv") == 0) {
      opt.out = OUT_CSV;
      ++argi;
    } else if (strcmp(a, "--text") == 0) {
      opt.out = OUT_TEXT;
      ++argi;
    } else if (strcmp(a, "--compact") == 0) {
      opt.compact = 1;
      ++argi;
    } else if (strcmp(a, "--friendly") == 0) {
      opt.friendly = 1;
      ++argi;
    } else if (strcmp(a, "--timeout") == 0 && argi + 1 < argc) {
      opt.timeout_ms = atoi(argv[++argi]);
      ++argi;
    } else if (strcmp(a, "--interval") == 0 && argi + 1 < argc) {
      opt.interval_ms = atoi(argv[++argi]);
      ++argi;
    } else if (strcmp(a, "--duration") == 0 && argi + 1 < argc) {
      opt.duration_ms = atoi(argv[++argi]);
      ++argi;
    } else if (strcmp(a, "--dedup") == 0) {
      opt.dedup = 1;
      ++argi;
    } else if (strcmp(a, "--all") == 0) {
      opt.dedup = 0;
      ++argi;
    } else if (strcmp(a, "--safe") == 0) {
      opt.safe = 1;
      ++argi;
    } else if (strcmp(a, "--min-rssi") == 0 && argi + 1 < argc) {
      opt.min_rssi = atoi(argv[++argi]);
      ++argi;
    } else if (strcmp(a, "--epc-prefix") == 0 && argi + 1 < argc) {
      opt.epc_prefix_len = normalize_hex(argv[++argi], opt.epc_prefix, sizeof(opt.epc_prefix));
      if (opt.epc_prefix_len < 0) {
        printf("Invalid EPC prefix\n");
        return 1;
      }
      ++argi;
    } else if (strcmp(a, "--antenna") == 0 && argi + 1 < argc) {
      opt.antenna = atoi(argv[++argi]);
      ++argi;
    } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
      usage();
      return 0;
    } else {
      break;
    }
  }

  if (argi >= argc) {
    usage();
    return 1;
  }

  if (!UHF_Init()) {
    printf("Init failed: %s\n", UHF_GetLastError());
    return 1;
  }

  const char* cmd = argv[argi++];
  int needs_open = 0;
  int exit_code = 0;

  if (strcmp(cmd, "count") == 0) {
    int count = UHF_GetUsbCount();
    if (opt.out == OUT_JSON) {
      printf("{\"usbCount\":%d}\n", count);
    } else if (opt.out == OUT_CSV) {
      printf("usbCount\n%d\n", count);
    } else if (opt.friendly) {
      print_friendly_int("USB devices", count);
    } else {
      printf("USB count: %d\n", count);
    }
  } else if (strcmp(cmd, "usbinfo") == 0) {
    int count = UHF_GetUsbCount();
    if (opt.out == OUT_JSON) {
      printf("{\"usbCount\":%d,\"devices\":[", count);
    } else if (opt.out == OUT_CSV) {
      printf("index,rawHex\n");
    } else if (opt.friendly) {
      print_friendly_int("USB devices", count);
    } else {
      printf("USB count: %d\n", count);
    }
    int first = 1;
    for (int i = 0; i < count; ++i) {
      unsigned char buf[64] = {0};
      if (UHF_GetUsbInfoRaw((unsigned short)i, buf, sizeof(buf))) {
        if (opt.out == OUT_JSON) {
          char hex[129];
          bytes_to_hex_str(buf, 64, hex, sizeof(hex));
          printf("%s{\"index\":%d,\"rawHex\":\"%s\"}", first ? "" : ",", i, hex);
          first = 0;
        } else if (opt.out == OUT_CSV) {
          char hex[129];
          bytes_to_hex_str(buf, 64, hex, sizeof(hex));
          printf("%d,%s\n", i, hex);
        } else if (opt.friendly) {
          char hex[129];
          bytes_to_hex_str(buf, 64, hex, sizeof(hex));
          printf("Device #%d: %s\n", i, hex);
        } else {
          printf("[%d] ", i);
          for (int k = 0; k < 64; ++k) printf("%02X", buf[k]);
          printf("\n");
        }
      }
    }
    if (opt.out == OUT_JSON) {
      printf("]}\n");
    }
  } else if (strcmp(cmd, "status") == 0) {
    exit_code = handle_status(opt);
  } else if (strcmp(cmd, "open") == 0) {
    int ok = UHF_Open((unsigned short)opt.index);
    printf("Open: %d\n", ok);
    if (!ok) {
      printf("Error: %s\n", UHF_GetLastError());
      exit_code = 1;
    }
  } else if (strcmp(cmd, "close") == 0) {
    int ok = UHF_Close();
    printf("Close: %d\n", ok);
    if (!ok) {
      printf("Error: %s\n", UHF_GetLastError());
      exit_code = 1;
    }
  } else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "start") == 0 ||
             strcmp(cmd, "stop") == 0 || strcmp(cmd, "buffer-clear") == 0 ||
             strncmp(cmd, "peek", 4) == 0 || strncmp(cmd, "pop", 3) == 0 ||
             strcmp(cmd, "read-once") == 0 || strcmp(cmd, "read-count") == 0 ||
             strcmp(cmd, "read-stream") == 0 ||
             strcmp(cmd, "power-get") == 0 || strcmp(cmd, "power-set") == 0 ||
             strcmp(cmd, "power-get-pct") == 0 || strcmp(cmd, "power-set-pct") == 0 ||
             strcmp(cmd, "power-set-preset") == 0 ||
             strcmp(cmd, "relay-on") == 0 || strcmp(cmd, "relay-off") == 0 ||
             strcmp(cmd, "freq-get") == 0 || strcmp(cmd, "freq-set") == 0 ||
             strcmp(cmd, "freq-set-region") == 0 ||
             strcmp(cmd, "read") == 0 || strcmp(cmd, "write") == 0 ||
             strcmp(cmd, "write-epc") == 0) {
    needs_open = 1;
  }

  if (needs_open) {
    if (!UHF_Open((unsigned short)opt.index)) {
      printf("Open failed: %s\n", UHF_GetLastError());
      UHF_Shutdown();
      return 1;
    }
  }

  if (strcmp(cmd, "info") == 0) {
    UHF_DeviceInfo info{};
    if (UHF_GetInfo(&info)) {
      if (opt.out == OUT_JSON) {
        printf("{\"swMajor\":%u,\"swMinor\":%u,\"hwMajor\":%u,\"hwMinor\":%u,\"sn\":\"%s\"}\n",
               info.softMajor, info.softMinor, info.hardMajor, info.hardMinor, info.sn);
      } else if (opt.out == OUT_CSV) {
        printf("swMajor,swMinor,hwMajor,hwMinor,sn\n%u,%u,%u,%u,%s\n",
               info.softMajor, info.softMinor, info.hardMajor, info.hardMinor, info.sn);
      } else if (opt.friendly) {
        printf("Firmware: %u.%u\n", info.softMajor, info.softMinor);
        printf("Hardware: %u.%u\n", info.hardMajor, info.hardMinor);
        printf("Serial: %s\n", info.sn);
      } else {
        printf("SW v%u.%u HW v%u.%u SN=%s\n",
               info.softMajor, info.softMinor, info.hardMajor, info.hardMinor, info.sn);
      }
    } else {
      printf("GetInfo failed: %s\n", UHF_GetLastError());
      exit_code = 1;
    }
  } else if (strcmp(cmd, "start") == 0) {
    int ok = UHF_StartRead();
    if (opt.friendly) print_friendly_ok("Reading started", ok);
    else printf("StartRead: %d\n", ok);
  } else if (strcmp(cmd, "stop") == 0) {
    int ok = UHF_StopRead();
    if (opt.friendly) print_friendly_ok("Reading stopped", ok);
    else printf("StopRead: %d\n", ok);
  } else if (strcmp(cmd, "buffer-clear") == 0) {
    int ok = UHF_ClearBuffer();
    if (opt.friendly) print_friendly_ok("Buffer cleared", ok);
    else printf("ClearBuffer: %d\n", ok);
    if (!ok) {
      printf("Error: %s\n", UHF_GetLastError());
      exit_code = 1;
    }
  } else if (strncmp(cmd, "peek", 4) == 0) {
    UHF_Tag tags[256];
    int count = 0;
    int ok = peek_tags(tags, 256, &count, opt);
    if (opt.friendly) {
      print_friendly_ok("Peek buffer", ok);
      print_friendly_int("Tags in buffer", count);
    } else {
      printf("ok=%d count=%d\n", ok, count);
    }
    int csv_header_done = 0;
    for (int i = 0; i < count; ++i) {
      if (!tag_matches(&tags[i], opt)) continue;
      print_tag(&tags[i], opt, &csv_header_done);
    }
  } else if (strncmp(cmd, "pop", 3) == 0) {
    UHF_Tag tags[256];
    int count = 0;
    int ok = pop_tags(tags, 256, &count, opt);
    if (opt.friendly) {
      print_friendly_ok("Pop buffer", ok);
      print_friendly_int("Tags returned", count);
    } else {
      printf("ok=%d count=%d\n", ok, count);
    }
    int csv_header_done = 0;
    for (int i = 0; i < count; ++i) {
      if (!tag_matches(&tags[i], opt)) continue;
      print_tag(&tags[i], opt, &csv_header_done);
    }
  } else if (strcmp(cmd, "read-once") == 0) {
    int ok = UHF_StartRead();
    if (!ok) {
      if (opt.friendly) {
        print_friendly_ok("Read once", 0);
        printf("Error: %s\n", UHF_GetLastError());
      } else {
        printf("StartRead failed: %s\n", UHF_GetLastError());
      }
      exit_code = 1;
    } else {
      sleep_ms(opt.timeout_ms);
      UHF_StopRead();
      UHF_Tag tags[256];
      int count = 0;
      ok = pop_tags(tags, 256, &count, opt);
      int printed = 0;
      int csv_header_done = 0;
      for (int i = 0; i < count; ++i) {
        if (!tag_matches(&tags[i], opt)) continue;
        print_tag(&tags[i], opt, &csv_header_done);
        ++printed;
      }
      if (opt.friendly && opt.out == OUT_TEXT) {
        print_friendly_ok("Read once", ok);
        print_friendly_int("Tags read", printed);
        if (printed == 0) printf("No tags found.\n");
      }
      if (printed == 0) exit_code = 2;
    }
  } else if (strcmp(cmd, "read-count") == 0) {
    if (argi >= argc) {
      printf("Missing count\n");
      exit_code = 1;
    } else {
      int target = atoi(argv[argi++]);
      int ok = UHF_StartRead();
      if (!ok) {
        if (opt.friendly) {
          print_friendly_ok("Read count", 0);
          printf("Error: %s\n", UHF_GetLastError());
        } else {
          printf("StartRead failed: %s\n", UHF_GetLastError());
        }
        exit_code = 1;
      } else {
        int printed = 0;
        int csv_header_done = 0;
        auto start = std::chrono::steady_clock::now();
        while (printed < target) {
          UHF_Tag tags[256];
          int count = 0;
          pop_tags(tags, 256, &count, opt);
          for (int i = 0; i < count && printed < target; ++i) {
            if (!tag_matches(&tags[i], opt)) continue;
            print_tag(&tags[i], opt, &csv_header_done);
            ++printed;
          }
          if (opt.timeout_ms > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= opt.timeout_ms) break;
          }
          sleep_ms(opt.interval_ms);
        }
        UHF_StopRead();
        if (opt.friendly && opt.out == OUT_TEXT) {
          print_friendly_ok("Read count", 1);
          print_friendly_int("Tags read", printed);
          if (printed == 0) printf("No tags found.\n");
        }
        if (printed == 0) exit_code = 2;
      }
    }
  } else if (strcmp(cmd, "read-stream") == 0) {
    int ok = UHF_StartRead();
    if (!ok) {
      if (opt.friendly) {
        print_friendly_ok("Read stream", 0);
        printf("Error: %s\n", UHF_GetLastError());
      } else {
        printf("StartRead failed: %s\n", UHF_GetLastError());
      }
      exit_code = 1;
    } else {
      int csv_header_done = 0;
      auto start = std::chrono::steady_clock::now();
      if (opt.friendly && opt.out == OUT_TEXT) {
        printf("Streaming started.\n");
      }
      while (1) {
        UHF_Tag tags[256];
        int count = 0;
        pop_tags(tags, 256, &count, opt);
        for (int i = 0; i < count; ++i) {
          if (!tag_matches(&tags[i], opt)) continue;
          print_tag(&tags[i], opt, &csv_header_done);
        }
        if (opt.duration_ms > 0) {
          auto now = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
          if (elapsed >= opt.duration_ms) break;
        }
        sleep_ms(opt.interval_ms);
      }
      UHF_StopRead();
      if (opt.friendly && opt.out == OUT_TEXT) {
        printf("Streaming ended.\n");
      }
    }
  } else if (strcmp(cmd, "power-get") == 0) {
    int dbm = UHF_GetPowerDbm();
    if (opt.friendly) print_friendly_int("Power (dBm)", dbm);
    else printf("Power dBm: %d\n", dbm);
  } else if (strcmp(cmd, "power-set") == 0) {
    if (argi >= argc) {
      printf("Missing dbm\n");
      exit_code = 1;
    } else {
      int dbm = atoi(argv[argi]);
      int ok = UHF_SetPowerDbm(dbm);
      if (opt.friendly) {
        printf("Power set to %d dBm: %s\n", dbm, ok ? "OK" : "FAILED");
      } else {
        printf("Set power: %d\n", ok);
      }
    }
  } else if (strcmp(cmd, "power-get-pct") == 0) {
    int pct = UHF_GetPowerPct();
    if (opt.friendly) print_friendly_int("Power (%)", pct);
    else printf("Power %%: %d\n", pct);
  } else if (strcmp(cmd, "power-set-pct") == 0) {
    if (argi >= argc) {
      printf("Missing pct\n");
      exit_code = 1;
    } else {
      int pct = atoi(argv[argi]);
      int ok = UHF_SetPowerPct(pct);
      if (opt.friendly) {
        printf("Power set to %d%%: %s\n", pct, ok ? "OK" : "FAILED");
      } else {
        printf("Set power pct: %d\n", ok);
      }
    }
  } else if (strcmp(cmd, "power-set-preset") == 0) {
    if (argi >= argc) {
      printf("Missing preset\n");
      exit_code = 1;
    } else {
      const char* p = argv[argi];
      int pct = 0;
      if (_stricmp(p, "low") == 0) pct = 30;
      else if (_stricmp(p, "med") == 0 || _stricmp(p, "medium") == 0) pct = 60;
      else if (_stricmp(p, "high") == 0) pct = 100;
      else {
        printf("Unknown preset\n");
        exit_code = 1;
      }
      if (pct > 0) {
        int ok = UHF_SetPowerPct(pct);
        if (opt.friendly) {
          printf("Power preset '%s' (%d%%): %s\n", p, pct, ok ? "OK" : "FAILED");
        } else {
          printf("Set power pct: %d\n", ok);
        }
      }
    }
  } else if (strcmp(cmd, "relay-on") == 0) {
    int ok = UHF_RelayOn();
    if (opt.friendly) print_friendly_ok("Relay ON", ok);
    else printf("RelayOn: %d\n", ok);
  } else if (strcmp(cmd, "relay-off") == 0) {
    int ok = UHF_RelayOff();
    if (opt.friendly) print_friendly_ok("Relay OFF", ok);
    else printf("RelayOff: %d\n", ok);
  } else if (strcmp(cmd, "freq-get") == 0) {
    unsigned char freq[2] = {0};
    int ok = UHF_GetFreq(freq);
    if (opt.friendly) {
      printf("Frequency: %02X %02X (%s)\n", freq[0], freq[1], ok ? "OK" : "FAILED");
    } else {
      printf("ok=%d freq=%02X %02X\n", ok, freq[0], freq[1]);
    }
  } else if (strcmp(cmd, "freq-set") == 0) {
    if (argi + 2 > argc) {
      printf("Missing freq bytes\n");
      exit_code = 1;
    } else {
      unsigned char b0 = 0, b1 = 0;
      if (!parse_byte(argv[argi++], &b0) || !parse_byte(argv[argi++], &b1)) {
        printf("Invalid byte format\n");
        exit_code = 1;
      } else {
        int ok = UHF_SetFreq(b0, b1);
        if (opt.friendly) {
          printf("Frequency set to %02X %02X: %s\n", b0, b1, ok ? "OK" : "FAILED");
        } else {
          printf("ok=%d\n", ok);
        }
      }
    }
  } else if (strcmp(cmd, "freq-set-region") == 0) {
    if (argi >= argc) {
      printf("Missing region\n");
      exit_code = 1;
    } else {
      unsigned char b0 = 0, b1 = 0;
      if (!region_to_freq(argv[argi++], &b0, &b1)) {
        printf("Unknown region\n");
        exit_code = 1;
      } else {
        int ok = UHF_SetFreq(b0, b1);
        if (opt.friendly) {
          printf("Frequency set (%02X %02X): %s\n", b0, b1, ok ? "OK" : "FAILED");
        } else {
          printf("ok=%d\n", ok);
        }
      }
    }
  } else if (strcmp(cmd, "read") == 0) {
    if (argi + 4 > argc) {
      printf("Missing args\n");
      exit_code = 1;
    } else {
      int bank = atoi(argv[argi++]);
      int wordPtr = atoi(argv[argi++]);
      int wordCount = atoi(argv[argi++]);
      const char* pwdHex = argv[argi++];
      unsigned char data[512];
      int bytesRead = 0;
      int ok = UHF_ReadTag((uint8_t)bank, (uint8_t)wordPtr, (uint8_t)wordCount,
                           pwdHex, data, sizeof(data), &bytesRead);
      if (opt.friendly) {
        printf("Read result: %s\n", ok ? "OK" : "FAILED");
        print_friendly_int("Bytes", bytesRead);
      } else {
        printf("ok=%d bytes=%d\n", ok, bytesRead);
      }
      if (ok && bytesRead > 0) {
        print_hex(data, bytesRead);
      }
    }
  } else if (strcmp(cmd, "write") == 0) {
    if (argi + 4 > argc) {
      printf("Missing args\n");
      exit_code = 1;
    } else {
      int bank = atoi(argv[argi++]);
      int wordPtr = atoi(argv[argi++]);
      const char* hexData = argv[argi++];
      const char* pwdHex = argv[argi++];
      unsigned char data[512];
      int dataLen = parse_hex(hexData, data, sizeof(data));
      if (dataLen <= 0) {
        printf("Invalid hex data\n");
        exit_code = 1;
      } else {
        int ok = UHF_WriteTag((uint8_t)bank, (uint8_t)wordPtr, data, dataLen, pwdHex);
        if (opt.friendly) {
          print_friendly_ok("Write", ok);
        } else {
          printf("ok=%d\n", ok);
        }
      }
    }
  } else if (strcmp(cmd, "write-epc") == 0) {
    if (argi + 2 > argc) {
      printf("Missing args\n");
      exit_code = 1;
    } else {
      const char* epcHex = argv[argi++];
      const char* pwdHex = argv[argi++];
      int ok = UHF_WriteEpc(epcHex, pwdHex);
      if (opt.friendly) {
        print_friendly_ok("Write EPC", ok);
      } else {
        printf("ok=%d\n", ok);
      }
    }
  } else {
    usage();
    exit_code = 1;
  }

  if (needs_open) {
    UHF_Close();
  }
  UHF_Shutdown();
  return exit_code;
}
