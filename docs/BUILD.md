# Budowa Ninetails Mobile — instrukcja krok po kroku

Ten dokument opisuje **realny** proces budowy obrazu systemu. Czas i wymagania
są podane dla maszyny deweloperskiej; na słabszym sprzęcie będzie dłużej.

## 0. Wymagania

| Element | Minimum | Zalecane |
|---|---|---|
| CPU | 8 rdzeni x86_64 | 16+ rdzeni |
| RAM | 32 GB | 64 GB |
| Dysk | 200 GB SSD | 400 GB NVMe |
| System | Ubuntu 22.04 LTS | Ubuntu 24.04 LTS |
| Łącze | 50 Mb/s | 200+ Mb/s |

**Dlaczego tyle:** Chromium OS + AOSP to ~120 GB źródeł, ~60 GB artefaktów
pośrednich, a linkowanie Chromium potrafi zjeść 20+ GB RAM na jeden proces.

## 1. Zależności systemowe

```bash
sudo apt-get update
sudo apt-get install -y \
  git curl python3 python3-pip openssl make gcc g++ unzip xz-utils \
  bison flex gperf pkg-config libssl-dev libelf-dev bc rsync \
  ninja-build ccache lz4 zstd cpio kmod \
  parted dosfstools e2fsprogs android-sdk-platform-tools-common
```

Weryfikacja:

```bash
make check
```

Skrypt sprawdza wersje (Python ≥ 3.8, git ≥ 2.28), pakiety i wolne miejsce.
Jeśli coś brakuje — wypisze dokładnie co doinstalować.

## 2. Pobranie źródeł

```bash
make bootstrap
```

Co się dzieje:

1. `depot_tools` (narzędzia Chromium) → `.cache/depot_tools`
2. `repo` (zarządzanie wieloma repozytoriami git) → `.cache/bin/repo`
3. `repo init` + `repo sync` wg `device/aerophone/manifest.xml` → `src/`
4. Toolchain i prebuilt kernel (jeśli dostępne w cache)

**Czas:** 1–4 h zależnie od łącza. **Rozmiar:** ~120 GB.

Jeśli `repo sync` padnie w połowie — uruchom ponownie, jest wznawialne.

## 3. Budowa obrazu hosta

```bash
make host
```

Etapy:

1. **SDK** — `cros_sdk` tworzy kontener z toolchainem Chromium OS (~2 GB, ~10 min)
2. **Overlay** — `device/aerophone/` kopiowany do `src/src/overlays/overlay-aerophone/`
3. **Kernel** — `chromeos-kernel-6-1` dla ARM64 (~20–40 min)
4. **Pakiety** — `emerge virtual/target-os` + nasz `aerophone-telephony` (~30–90 min)
5. **Obraz** — `build_image` składa rootfs i partycje
6. **Podpis** — `vbmeta` kluczem deweloperskim

Wynik: `out/host/chromiumos_image.bin` + `out/host/vmlinuz`

Tylko kernel (szybka iteracja przy zmianach sterowników):

```bash
./build/build-host.sh --kernel-only
```

## 4. Budowa obrazu ARCVM

```bash
make arcvm
```

Etapy:

1. `lunch aosp_arm64-userdebug`
2. Wstrzyknięcie modułów:
   - `telephony/guest/` → `hardware/aerophone/telephony/`
   - `apps/AeroRadar/` → `packages/apps/AeroRadar/`
3. `m -j$(nproc)` — pełny build AOSP (~1–3 h pierwszy raz)
4. Złożenie obrazu ARCVM (system + vendor + product)

Wynik: `out/arcvm/arcvm.img` (+ `system.img`, `vendor.img`)

Tylko aplikacja (szybka iteracja przy zmianach w AeroRadar):

```bash
./build/build-arcvm.sh --app-only
```

Skrypt na końcu sprawdza, czy `AeroRadar` faktycznie trafił do
`/system/priv-app` — to najczęstszy błąd konfiguracji.

## 5. Złożenie obrazu urządzenia

```bash
make image
```

Tworzy GPT z partycjami A/B:

```
p1  KERN-A     64 MiB
p2  KERN-B     64 MiB
p3  ROOT-A     ~3 GiB
p4  ROOT-B     ~3 GiB
p5  OEM        32 MiB
p6  MISC       8 MiB
p7  ARCVM-A    ~4 GiB
p8  ARCVM-B    ~4 GiB
p9  USERDATA   8 GiB
p10 VBMETA
```

Wynik: `out/image/aerophone-userdebug.img` oraz pojedyncze obrazy do fastboot
(`boot.img`, `system.img`, `arcvm.img`, `vbmeta.img`).

## 6. Wgranie na urządzenie

```bash
make flash
```

Szczegóły i tryby awaryjne: [`FLASH.md`](FLASH.md).

## 7. Aktualizacja OTA

```bash
./build/ota.sh --full --to out/image/aerophone-user.img
```

Wynik: `out/ota/payload.bin` + `payload_properties.txt` + `metadata.json`.
Instrukcja wgrania jest wypisywana na końcu.

## Typowe problemy

| Objaw | Przyczyna | Rozwiązanie |
|---|---|---|
| `repo sync` przerywa | limit połączeń / brak miejsca | uruchom ponownie; `df -h` |
| `emerge` OOM | za mało RAM | `MAKEOPTS="-j4"` w `cros_sdk` |
| `build_image` nie znajduje overlaya | zła nazwa katalogu | musi być `overlay-aerophone` |
| AeroRadar nie w `priv-app` | brak w `PRODUCT_PACKAGES` | sprawdź `device.mk` |
| `avbtool` błąd klucza | brak `keys/dev/avb.pem` | wygeneruj: patrz `FLASH.md` § klucze |
| bootloop po flashu | zły slot / brak vbmeta | `fastboot --set-active=a` |

## Powtarzalność builda

Wersje są pinowane w `build/env.sh`:

```bash
CHROMEOS_BRANCH=release-R126-15886.B
AOSP_TAG=android-14.0.0_r50
KERNEL_VERSION=6.1
```

Zmiana którejkolwiek = nowy build ID. Ten sam zestaw pinów + ten sam
toolchain = ten sam obraz (bit-for-bit, poza znacznikiem czasu w vbmeta).
