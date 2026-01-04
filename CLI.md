# UHF Wrapper CLI

`UhfWrapperCli.exe` is a diagnostics + production helper for the IN53xx reader
(HID mode). It uses the same `UHF_*` friendly API as the DLL and provides
text/JSON/CSV outputs.

## Usage

```
UhfWrapperCli.exe [options] <command> [args]
```

Common options:
- `--friendly` : use the high-level friendly workflow calls
- `--json` / `--csv` / `--text` : output format
- `--compact` : compact JSON output
- `--timeout <ms>` : read-once/read-count timeout
- `--interval <ms>` : interval between loop reads
- `--duration <ms>` : total duration for read-stream (0 = forever)
- `--dedup` / `--all` : buffer mode (default dedup)
- `--safe` : use safe pop (stronger checks)
- `--min-rssi <dbm>` : software filter min
- `--epc-prefix <hex>` : filter by EPC prefix
- `--antenna <n>` : filter by antenna

Write safety:
- `--target <epcHex>` : target a specific tag for write
- `--force` : allow write when multiple tags are detected

Calibration options:
- `--calib-epc <hex>`
- `--calib-min <dbm>` / `--calib-max <dbm>` / `--calib-step <dbm>`
- `--calib-reads <n>`
- `--calib-pwr-margin <dbm>`
- `--calib-rssi-margin <dbm>`
- `--calib-capture <ms>`
- `--apply` (apply power + RSSI window)

## Core Commands

- `count` : number of USB HID devices
- `status` : reader present / open / connected
- `usbinfo` : list USB HID devices
- `open` / `close`
- `info` : extended status (transport, power, freq) + firmware/hardware info

`info` JSON/CSV includes `freqRegion` (e.g., `EU`, `US`).
- `config-check` : system config diagnostics

## Read Commands

- `read-once` : single pass, returns tags
- `read-count <n>` : read until N tags or timeout
- `read-stream` : continuous buffer read
- `buffer-test` : start read, wait, pop+clear, stop

Calibrated versions:
- `read-once-calib`
- `read-stream-calib`

## Buffer Commands

- `start` / `stop`
- `buffer-clear`
- `peek-all`, `peek-dedup`
- `pop-all`, `pop-dedup`
- `pop-all-safe`, `pop-dedup-safe`

## Power / RSSI

- `power-get` / `power-set <dbm>`
- `power-get-pct` / `power-set-pct <pct>`
- `power-set-preset <low|med|high>`
- `rssi-filter --rssi-min <dbm> --rssi-max <dbm>`
- `rssi-reset`

## Select / Write / Lock

- `select-epc <epcHex>`
- `select-clear`
- `write-epc <epcHex> <pwdHex>`
- `read <bank> <wordPtr> <wordCount> <pwdHex>`
- `write <bank> <wordPtr> <hexData> <pwdHex>`
- `lock <lockType> <lockMem> <pwdHex>`

Safe write example:
```
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> write-epc <NEW_EPC> 00000000
```

If multiple tags are present, add `--force` to override.

## Calibration

- `calib-prepare` : pick or write a calibration EPC
- `calib-run` : power sweep + RSSI capture
- `calib-save <path>` / `calib-load <path>`

Example:
```
UhfWrapperCli.exe --friendly calib-prepare
UhfWrapperCli.exe --friendly calib-run --calib-min 0 --calib-max 26 --calib-step 1 \
  --calib-reads 3 --calib-pwr-margin 2 --calib-capture 8000 --calib-rssi-margin 3 --apply
```

## I/O / Relay / Outputs

- `relay-on` / `relay-off`
- `relay2-on` / `relay2-off`
- `out1-on` / `out1-off`
- `out2-on` / `out2-off`

## Whitelist

- `whitelist-count`
- `whitelist-get <index>`
- `whitelist-add <epcHex>`
- `whitelist-del <epcHex>`
- `whitelist-clear`

## Advanced

- `workmode-get` / `workmode-set <answer|active|trigger|0|1|2>`
- `freq-get` / `freq-set <byte0> <byte1>`
- `freq-set-region <EU|US|JP|CN|KR|AU|NZ|IN|SG|HK|TW|CA|MX|BR|IL|ZA|TH|MY>`
- `module-cmd <cmdHex> [payloadHex]`

## Full Help

```
UhfWrapperCli.exe --help
```
