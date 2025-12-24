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
```

Buffer :
```
UhfWrapperCli.exe --friendly pop-dedup-safe
UhfWrapperCli.exe --friendly buffer-clear
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
- Lecture : `UHF_StartRead`, `UHF_StopRead`, `UHF_PeekBuffer*`, `UHF_PopBuffer*`
- Tag : `UHF_ReadTag`, `UHF_WriteTag`, `UHF_WriteEpc`, `UHF_WriteEpcSelected`,
  `UHF_WriteTagSelected`, `UHF_SelectEpc`, `UHF_ClearSelect`
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
- `UHF_GetTagBuf` **vide** le buffer apres lecture (comportement vendor).
