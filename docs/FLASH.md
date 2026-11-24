# Wgrywanie obrazu na urządzenie

## Wymagania

- Urządzenie z **odblokowanym bootloaderem**
- `adb` i `fastboot` w PATH
- Kabel USB (dane, nie tylko ładowanie)
- **Backup danych** — flash kasuje partycję userdata

## 1. Odblokowanie bootloadera (jednorazowo)

```bash
adb reboot bootloader
fastboot flashing unlock
# na ekranie urządzenia: potwierdź (Volume Up + Power)
fastboot reboot
```

⚠️ Odblokowanie **kasuje wszystkie dane** i wyłącza verified boot.
Na produkcji bootloader jest zablokowany, a obraz podpisany kluczem
produkcyjnym — odblokowanie jest tylko dla devkitów.

## 2. Klucze deweloperskie

Przed pierwszym buildem wygeneruj klucze:

```bash
mkdir -p keys/dev
cd keys/dev

# AVB (verified boot)
openssl genrsa -out avb.pem 4096
openssl req -new -x509 -key avb.pem -out avb.x509.pem -days 3650 \
  -subj "/CN=AeroPhone Dev AVB/"

# Payload OTA
openssl genrsa -out payload.pem 4096

# vbmeta (dla sign_image w build/lib/common.sh)
cp avb.pem vbmeta.pem
```

⚠️ **Nie commituj kluczy.** `keys/` jest w `.gitignore`.
Na produkcji klucze trzymaj w HSM — `AEROPHONE_HSM=1` w `build/env.sh`
przełącza podpisywanie na `pkcs11-sign`.

## 3. Flash

### Standardowy (slot A)

```bash
make flash
```

Skrypt:
1. wykrywa urządzenie (albo przełącza przez `adb reboot bootloader`)
2. sprawdza stan bootloadera
3. weryfikuje artefakty
4. flashuje `boot_a`, `system_a`, `arcvm_a`, `vbmeta`
5. restartuje i czeka na boot (max 180 s)
6. weryfikuje: `ro.product.device`, wersję Androida, obecność AeroRadar

### Pełny (z czyszczeniem danych)

```bash
./build/flash.sh --full
```

### Konkretne urządzenie (kilka podłączonych)

```bash
./build/flash.sh --serial ABC12345
```

### Aktualizacja slotu B (bez utraty danych)

```bash
./build/flash.sh --ota
```

Zapisuje do `_b`, ustawia slot B jako aktywny. Slot A zostaje nietknięty —
rollback to jedno polecenie.

## 4. Weryfikacja po flashu

```bash
# Podstawowe
adb shell getprop ro.product.device        # aerophone
adb shell getprop ro.build.version.release # 14
adb shell getprop ro.boot.slot_suffix      # _a lub _b

# AeroRadar
adb shell pm list packages | grep aeroradar
adb shell dumpsys package com.aerophone.aeroradar | grep -A2 "granted=true"

# Telefonia (most RIL)
adb shell getprop | grep rilbridge
adb shell dumpsys telephony.registry | head -20

# Host: usługa telefonii
adb shell ps -A | grep rilbridged
adb shell journalctl -u rilbridged -n 50
```

## 5. Rollback

### Szybki (przełączenie slotu)

```bash
fastboot --set-active=a
fastboot reboot
```

### Pełny (powrót do poprzedniego obrazu)

```bash
./build/flash.sh --serial ABC12345   # z poprzednim out/image/
```

## 6. Tryby awaryjne

### Urządzenie nie wchodzi w fastboot

- Przytrzymaj **Volume Down + Power** przy podłączeniu USB
- Sprawdź kabel (niektóre kable tylko ładują)
- `lsusb` — czy urządzenie jest widoczne?

### Bootloop

```bash
# Zobacz, gdzie się zatrzymuje
adb logcat -b all | grep -iE "aerophone|rilbridge|arcvm|fatal"

# Wróć do slotu A
fastboot --set-active=a && fastboot reboot

# Albo wyłącz weryfikację (tylko devkit!)
fastboot --disable-verity --disable-verification flash vbmeta vbmeta.img
```

### ARCVM nie startuje

```bash
adb shell journalctl -u concierge -n 100
adb shell journalctl -u arcvm-start -n 100
# Sprawdź, czy obraz ARCVM jest na partycji
adb shell ls -la /dev/block/by-name/ | grep arcvm
```

### Brak telefonii

```bash
# Host
adb shell journalctl -u rilbridged -n 100
adb shell ls -la /dev/qmi0

# Gość
adb shell logcat -s RadioBridge
adb shell dumpsys telephony.registry | grep -i "mServiceState"
```

Najczęstsza przyczyna: `rilbridged` nie wystartował przed ARCVM.
Sprawdź kolejność w `rilbridged.service` (`Before=arcvm-start.service`).

## 7. Przywrócenie do stanu fabrycznego

```bash
fastboot flashing lock     # ponowne zablokowanie (wymaga oryginalnego obrazu)
fastboot erase userdata
fastboot erase metadata
```

⚠️ Zablokowanie bootloadera z **nieoryginalnym** obrazem = cegła.
Rób to tylko z obrazem podpisanym właściwym kluczem.
