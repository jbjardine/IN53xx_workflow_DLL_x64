# UHF Wrapper - Guide utilisateur

## Objectif
Ce projet fournit une DLL native **UhfWrapper** (x64/x86) au-dessus du vendor `SWHidApi.dll`.

- API canonique stable : `UHF_*`
- Re-export 1=1 des fonctions vendor (mêmes noms) quand elles existent
- CLI de test `UhfWrapperCli.exe`
- Option .NET (P/Invoke) conservant la même API

## Parite x64 / x86 (important)
La **DLL simple expose la meme API UHF_** en x64 et en x86.

- Si une fonction n'existe pas dans la DLL vendor courante, le wrapper retourne `0`
  et `UHF_GetLastError()` renvoie un message **Not supported**.
- Certains exports vendor differents entre x64/x86 (ex: whitelist delete). Le wrapper
  gere automatiquement les deux noms.

Fonctions sensibles a la presence d'exports vendor :
- `UHF_Relay2On/Off`, `UHF_Out1On/Off`, `UHF_Out2On/Off`
- `UHF_ModuleCommand`

## Erreurs (messages + codes)
Le wrapper expose :
- `UHF_GetLastError()` : message texte
- `UHF_GetLastErrorCode()` : code numerique

Convention de retour (important) :
- Fonctions d'action (`Open`, `StartRead`, `Write*`, etc.) : `1` = succes, `0` = echec
- Fonctions qui renvoient une valeur (`UHF_GetTransport`, `UHF_GetWorkMode`, `UHF_GetPowerDbm`, `UHF_GetPowerPct`) :
  valeur `>= 0` en succes, `-1` en echec
- `UHF_GetUsbCount()` : nombre de lecteurs `0..N`, ou `-1` en erreur interne

Codes principaux :
- `UHF_ERR_OK` (0)
- `UHF_ERR_UNKNOWN` (-1)
- `UHF_ERR_INVALID_ARG` (-2)
- `UHF_ERR_VENDOR_MISSING` (-3)
- `UHF_ERR_VENDOR_CALL_FAILED` (-4)
- `UHF_ERR_NOT_SUPPORTED` (-5)
- `UHF_ERR_NOT_OPEN` (-6)
- `UHF_ERR_NOT_CONNECTED` (-7)
- `UHF_ERR_NO_DEVICE` (-8)
- `UHF_ERR_MULTI_TAG` (-9)
- `UHF_ERR_VERIFY_FAILED` (-10)
- `UHF_ERR_ALREADY_READING` (-11)
- `UHF_ERR_NOT_READING` (-12)
- `UHF_ERR_NO_TAG` (-13)
- `UHF_ERR_CALIBRATION_FAILED` (-14)

## Interface / Transport (USB HID)
Le reader doit être en **USB/HID** pour que le wrapper fonctionne.

- Paramètre vendor : **Transport** (index `0x01`, protocole V1.9)
- Valeurs :
  - **0 = USB**
  - **1 = RS232/RS485**
  - **2 = RJ45**
  - **3 = WIFI**
  - **4 = Weigand**

Note : le `SystemConfig.ini` du ReaderSoft utilise son propre mapping (ex: `Transport=1`),
qui ne correspond pas aux valeurs du paramètre device `0x01`.

Helpers wrapper :
```
UHF_GetTransport();         // lit la valeur brute (0..255)
UHF_SetTransport(v);        // fixe une valeur brute (si vous connaissez le mapping)
UHF_SetTransportUsb();      // tente de passer en USB
UHF_EnsureUsbTransport();   // vérifie + passe en USB si besoin
```
Si le device est en mode **Weigand (WG26/WG34)**, la lecture HID échoue : il faut repasser en USB
(via `UHF_SetTransportUsb()` ou ReaderSoft).

## Build (Windows)
```
cd src/uhf_wrapper
mkdir build_x64
cd build_x64
cmake -A x64 ..
cmake --build . --config Release
```

x86 :
```
cd src/uhf_wrapper
mkdir build_x86
cd build_x86
cmake -A Win32 ..
cmake --build . --config Release
```

### Charger une DLL vendor alternative
Par defaut, le wrapper charge `SWHidApi.dll` present a cote.
Tu peux forcer un chemin via :
```
set UHF_VENDOR_DLL=C:\path\SWHidApi.dll
```

## CLI (tests rapides)
Base :
```
UhfWrapperCli.exe --friendly status
UhfWrapperCli.exe --friendly read-once
UhfWrapperCli.exe --friendly read-stream --duration 2000
UhfWrapperCli.exe --friendly calib-prepare
UhfWrapperCli.exe --friendly calib-run --calib-max 10 --apply
UhfWrapperCli.exe --friendly rssi-filter --rssi-min -65 --rssi-max -40
```

Buffer :
```
UhfWrapperCli.exe --friendly pop-dedup-safe
UhfWrapperCli.exe --friendly buffer-clear
```

Anti‑double (fenetre de temps) :
```
UHF_DedupWindowSet(3000)   // 3 secondes
UHF_DedupKeySet(0)         // 0=EPC seul, 1=EPC+antenne
UHF_PopBufferDedupFiltered(tags, 256, &count)
```
Par defaut, la fenetre est a 0 (desactive). Le cache est remis a zero par `UHF_ClearBuffer()`.

Lecture ponctuelle (custom, sans G2) :
```
UHF_ReadOnce(300, tags, 256, &count)
```

Filtre RSSI (software) :
```
UHF_RssiFilterSet(-65, -40)   // n'accepte que [-65..-40] dBm
UHF_RssiFilterReset()         // desactive le filtre
```
Le filtre s'applique a `UHF_PopBufferDedupFiltered` et `UHF_ReadOnce`.

Auto‑calibration (puissance + RSSI) :
```
char calibEpc[128] = {0};
UHF_CalibrationResult res{};

// Garde l'EPC du tag present (1 seul tag requis)
UHF_CalibrationTagPrepare(nullptr, 0, calibEpc, sizeof(calibEpc));

// Balaye la puissance puis capture RSSI (applique les reglages)
UHF_CalibrateByTag(calibEpc, 0, 26, 1, 3, 2, 8000, 3, 1, &res);
```
Notes :
- `UHF_CalibrationTagPrepare(..., writeNew=1)` permet d'ecrire un EPC de calibration
  (EPC fourni ou genere aleatoirement).
- `UHF_CalibrateByTag` peut appliquer directement la puissance + filtre RSSI (`applySettings=1`)
  ou juste renvoyer les valeurs (`applySettings=0`).
- Pendant la calibration, le filtrage EPC est fait **en software** (pas de mask hard),
  pour eviter un blocage de `SetPowerDbm` sur certains firmwares.
- Le balayage de puissance se fait de **max → min**, et un palier est valide uniquement
  si **toutes** les lectures prevues reussissent.
- Les lectures pendant la calibration utilisent **Active + buffer** pour fiabilite;
  `InventoryG2` (Answer) peut renvoyer 0 tag sur certains firmwares.
- Si un EPC de calibration est fourni, la presence d'autres EPC est acceptee;
  la calibration refuse uniquement si elle detecte plusieurs tags partageant
  le meme EPC (verification best‑effort via TID).
- Si le lecteur ignore le mask select, la verification de doublon est ignoree.

Lecture calibree (profil deja charge) :
```
UHF_ReadOnceCalibrated(300, tags, 256, &count);
UHF_ReadStreamCalibrated(5000, tags, 256, &count);
```

Persistence (CLI) :
```
UhfWrapperCli.exe --friendly calib-save C:\temp\uhf_calib.txt
UhfWrapperCli.exe --friendly calib-load C:\temp\uhf_calib.txt --apply
UhfWrapperCli.exe --friendly read-once-calib
UhfWrapperCli.exe --friendly read-stream-calib --duration 2000
```

Debug WorkMode (CLI) :
```
UhfWrapperCli.exe --friendly workmode-get
UhfWrapperCli.exe --friendly workmode-set answer
UhfWrapperCli.exe --friendly workmode-set active
```

Selection (cibler un tag) :
```
UhfWrapperCli.exe --friendly select-epc <EPC_HEX>
UhfWrapperCli.exe --friendly write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly select-clear
```
Note : la sélection force le **mask** via `SWHid_SetDeviceSpecialParam` quand disponible, afin que les opérations d’écriture ciblent bien un seul tag. Sur SDKs incomplets, la sélection peut ne s’appliquer qu’à l’inventaire.

Sécurité multi‑tag (optionnelle) :
```
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> --force write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> write 3 0 11223344 00000000
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> --force write 3 0 11223344 00000000
```
Par defaut, toutes les commandes **d'ecriture** bloquent si plusieurs tags sont detectes.
Utilise `--force` si tu veux outrepasser cette securite.

Whitelist :
```
UhfWrapperCli.exe --friendly whitelist-count
UhfWrapperCli.exe --friendly whitelist-add <EPC_HEX>
UhfWrapperCli.exe --friendly whitelist-get 0
UhfWrapperCli.exe --friendly whitelist-del <EPC_HEX>
UhfWrapperCli.exe --friendly whitelist-clear
```

GPIO :
```
UhfWrapperCli.exe --friendly relay-on
UhfWrapperCli.exe --friendly relay-off
UhfWrapperCli.exe --friendly relay2-on   (si supporte)
UhfWrapperCli.exe --friendly out1-on     (si supporte)
```

Lock :
```
UhfWrapperCli.exe --friendly lock <lockType> <lockMem> <PWD_HEX>
```

Module raw (avances, si supporte) :
```
UhfWrapperCli.exe --friendly module-cmd <cmdHex> [payloadHex]
```

## API UHF_* (principales)
- Connexion : `UHF_Init`, `UHF_Open`, `UHF_Close`, `UHF_IsOpen`, `UHF_IsConnected`
- Infos : `UHF_GetInfo`, `UHF_GetUsbCount`, `UHF_GetUsbInfoRaw`
- Transport : `UHF_GetTransport`, `UHF_SetTransport`, `UHF_SetTransportUsb`, `UHF_EnsureUsbTransport`
- Lecture : `UHF_StartRead`, `UHF_StopRead`, `UHF_PeekBuffer*`, `UHF_PopBuffer*`
- Tag : `UHF_ReadTag`, `UHF_WriteTag`, `UHF_WriteEpc`, `UHF_WriteEpcSelected`,
  `UHF_WriteTagSelected`, `UHF_SelectEpc`, `UHF_ClearSelect`
- Dedup : `UHF_DedupWindowSet`, `UHF_DedupWindowReset`, `UHF_DedupKeySet`,
  `UHF_PopBufferDedupFiltered`
- Read once : `UHF_ReadOnce`
- Filtre RSSI : `UHF_RssiFilterSet`, `UHF_RssiFilterReset`
- Calibration : `UHF_CalibrationTagPrepare`, `UHF_CalibrateByTag`
- Puissance : `UHF_GetPowerDbm`, `UHF_SetPowerDbm`, `UHF_GetPowerPct`, `UHF_SetPowerPct`
- Frequence : `UHF_GetFreq`, `UHF_SetFreq`
- GPIO : `UHF_RelayOn/Off`, `UHF_Relay2On/Off`, `UHF_Out1On/Off`, `UHF_Out2On/Off`
- Whitelist : `UHF_WhitelistCount`, `UHF_WhitelistGetRaw/Hex`, `UHF_WhitelistAddEpc`,
  `UHF_WhitelistRemoveEpc`, `UHF_WhitelistClear`
- Avance : `UHF_ModuleCommand`

## Notes techniques
- `UHF_WriteTag` et `UHF_WriteEpc` verifient la valeur ecrite via un read-back.
  En cas d’ecart, ils renvoient `UHF_ERR_VERIFY_FAILED`.
- `UHF_LockTag` utilise `lockCfg = (lockMem << 4) | lockType`.
- `UHF_WhitelistCount` utilise `SWHid_ReadWhiteListCnt` si disponible,
  sinon il enumere les indices jusqu'a echec.
- Puissance RF : la plage device est **0–26 dBm** (ReaderSoft peut afficher 0–30).
- `UHF_GetTagBuf` **vide** le buffer apres lecture (comportement vendor).
