# Ninetails Mobile

System operacyjny dla telefonów oparty na **Chromium OS (host) + Android w ARCVM (gość)**,
zgodny z architekturą warstwową L0–L8 opisaną w dokumencie projektu.

Repozytorium zawiera **realny, budowalny szkielet projektu**: drzewo konfiguracji,
skrypty budowania obrazu, warstwę telefonii (most RIL host↔gość) oraz aplikację
systemową **AeroRadar** z modułem **Aero ID**.

> **Nazwa produktu:** Ninetails Mobile.
> **Kryptonim wewnętrzny:** `aerophone` (płytka, katalogi, pakiety `com.aerophone.*`).
> Kryptonim pozostaje niezmieniony — to nazwa kodowa warstwy sprzętowej, nie marka.

```
ninetails-mobile/
├── Makefile                  # główny punkt wejścia (make help)
├── build/                    # skrypty budowania i wgrywania
│   ├── env.sh                # zmienne środowiskowe, wersje, ścieżki
│   ├── lib/common.sh         # wspólne funkcje (log, require, die)
│   ├── bootstrap.sh          # pobranie depot_tools + repo, sync źródeł
│   ├── build-host.sh         # obraz hosta (Chromium OS / board aerophone)
│   ├── build-arcvm.sh        # obraz ARCVM (AOSP + AeroRadar + most RIL)
│   ├── make-image.sh         # złożenie obrazu dysku + partycji A/B
│   ├── flash.sh              # wgranie na urządzenie (fastboot / recovery)
│   └── ota.sh                # wygenerowanie payloadu OTA (A/B)
├── device/aerophone/         # konfiguracja urządzenia (overlay)
│   ├── BoardConfig.mk
│   ├── device.mk
│   ├── manifest.xml          # repo manifest (pinning wersji)
│   └── sepolicy/
├── telephony/                # warstwa telefonii (L4 ↔ L6)
│   ├── proto/rilbridge.proto # protokół mostu RIL
│   ├── host/                 # demon hosta: rilbridged (C++)
│   └── guest/                # HAL radiowy w ARCVM (C++/AIDL)
├── apps/AeroRadar/           # aplikacja systemowa (Kotlin, priv-app)
├── tools/ci/                 # shimy wyłącznie dla CI (nie produkt)
├── .github/workflows/        # CI: kompilacja tego, co buildowalne na runnerze
└── docs/
    ├── ARCHITECTURE.md       # mapowanie na warstwy L0–L8
    ├── BUILD.md              # instrukcja budowy krok po kroku
    ├── FLASH.md              # instrukcja wgrania obrazu
    └── AERORADAR.md          # integracja AeroRadar / Aero ID
```

## Szybki start

```bash
make help          # lista celów
make bootstrap     # ~120 GB dysku, 4–8 h (pierwsze pobranie źródeł)
make host          # obraz hosta
make arcvm         # obraz ARCVM z AeroRadar
make image         # złożenie obrazu urządzenia
make flash         # wgranie na podłączony telefon (fastboot)
```

Pełna instrukcja: [`docs/BUILD.md`](docs/BUILD.md) i [`docs/FLASH.md`](docs/FLASH.md).

## Co buduje CI (GitHub Actions)

Workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) buduje to,
co da się zbudować na publicznym runnerze `ubuntu-24.04`:

| Job | Co robi | Wynik |
|---|---|---|
| `scripts` | `bash -n` + shellcheck + `xmllint` na całym drzewie | walidacja składni |
| `ril-bridge` | `protoc` + `g++` → binarka `rilbridged` | **kompiluje się** |
| `aeroradar` | Gradle + Android SDK → APK debug | kompiluje się (patrz job) |
| `full-image` | raport, czego wymaga pełne środowisko | obraz **nie** powstaje w CI |

**Obraz systemu nie powstaje w CI.** Chromium OS + ARCVM wymaga ~200 GB źródeł,
32 GB+ RAM i kilku godzin — patrz `docs/BUILD.md`.

## Status implementacji

| Warstwa | Komponent | Status |
|---|---|---|
| L0 | Definicja płytki `aerophone` (SoC, partycje) | ✅ konfiguracja |
| L1 | vboot / bootloader, klucze deweloperskie | ✅ skrypty + konfiguracja |
| L2 | Kernel hosta, dm-verity, KVM | ✅ konfiguracja |
| L3 | crosvm / concierge / cicerone | ✅ z upstreamu, konfiguracja |
| L4 | `rilbridged` — usługa telefonii hosta | ⚠️ szkielet + backend QMI/MBIM (stuby) |
| L5 | Powłoka (Ash/Chrome), launcher telefonu | ⚠️ konfiguracja, UI do dopracowania |
| L6 | ARCVM + HAL radiowy | ⚠️ szkielet mostu, pełny AIDL do uzupełnienia |
| L7 | Mosty: powiadomienia, schowek, tożsamość | ✅ z upstreamu (ARC) |
| L8 | AeroRadar (priv-app) | ✅ źródła + zasoby + uprawnienia |

**Uczciwie:** to nie jest gotowy produkt. To szkielet, który się kompiluje i ma
poprawną strukturę; pełna implementacja stosu radiowego (VoLTE/IMS, eSIM) to
praca zespołowa na wiele miesięcy. Szczegóły w `docs/ARCHITECTURE.md` § „Czego brakuje”.

## Wymagania sprzętowe (deweloperskie)

- Host budujący: x86_64, 32 GB RAM (64 GB zalecane), 200 GB SSD, Ubuntu 22.04/24.04
- Urządzenie docelowe: devkit ARM64 z odblokowanym bootloaderem, modem Quectel/Qualcomm,
  min. 8 GB RAM, 128 GB UFS, TPM 2.0

## Licencje

- Kod własny: Apache-2.0 (`LICENSE`)
- Chromium OS: BSD-3-Clause (komponenty) — patrz `NOTICE`
- AOSP: Apache-2.0
- AeroRadar: aplikacja własna; dane ADS-B podlegają warunkom dostawcy (patrz `docs/AERORADAR.md`)
