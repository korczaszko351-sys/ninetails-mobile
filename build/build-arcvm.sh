#!/usr/bin/env bash
# build-arcvm.sh — budowa obrazu ARCVM (Android w VM) z AeroRadar i mostem RIL.
#
# Użycie:
#   ./build/build-arcvm.sh
#   ./build/build-arcvm.sh --app-only   # tylko przebuduj AeroRadar
#
# Wynik: out/arcvm/arcvm.img (system + vendor + product + AeroRadar jako priv-app)
#
# ARCVM to Android uruchamiany przez crosvm. Obraz budujemy z AOSP z
# dodatkami: HAL radiowy (telephony/guest), AeroRadar (apps/AeroRadar),
# oraz konfiguracja urządzenia wirtualnego (arcvm-aerophone).

source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${AEROPHONE_ROOT}/build/lib/common.sh"

APP_ONLY=0
[[ "${1:-}" == "--app-only" ]] && APP_ONLY=1

ARCVM_OUT="${OUT_DIR}/arcvm"
mkdir -p "${ARCVM_OUT}"

AOSP_DIR="${SRC_DIR}/arcvm"
require_dir "${AOSP_DIR}" || die "brak drzewa AOSP w ${AOSP_DIR} — uruchom make bootstrap"

# ---------------------------------------------------------------------------
step "Konfiguracja środowiska AOSP"
# ---------------------------------------------------------------------------
( cd "${AOSP_DIR}" && source build/envsetup.sh >/dev/null ) || die "envsetup.sh nie powiodło się"

# lunch: cel dla ARCVM na naszej płytce
# aosp_arm64-eng = pełny build z debugowaniem; userdebug dla devkitu
TARGET="aosp_arm64-${VARIANT}"
log "lunch ${TARGET}"
( cd "${AOSP_DIR}" && lunch "${TARGET}" >/dev/null ) || die "lunch nie powiodło się"

# ---------------------------------------------------------------------------
step "Wstrzyknięcie modułów Ninetails do drzewa AOSP"
# ---------------------------------------------------------------------------
# 1. HAL radiowy (gość) → hardware/aerophone/telephony
GUEST_SRC="${AEROPHONE_ROOT}/telephony/guest"
GUEST_DST="${AOSP_DIR}/hardware/aerophone/telephony"
mkdir -p "$(dirname "${GUEST_DST}")"
rsync -a --delete "${GUEST_SRC}/" "${GUEST_DST}/"
ok "HAL radiowy: ${GUEST_DST}"

# 2. AeroRadar → packages/apps/AeroRadar
APP_SRC="${AEROPHONE_ROOT}/apps/AeroRadar"
APP_DST="${AOSP_DIR}/packages/apps/AeroRadar"
rsync -a --delete "${APP_SRC}/" "${APP_DST}/"
ok "AeroRadar: ${APP_DST}"

# 3. Konfiguracja urządzenia wirtualnego
DEV_SRC="${AEROPHONE_ROOT}/device/aerophone/arcvm"
DEV_DST="${AOSP_DIR}/device/aerophone/arcvm"
if [[ -d "${DEV_SRC}" ]]; then
  mkdir -p "$(dirname "${DEV_DST}")"
  rsync -a --delete "${DEV_SRC}/" "${DEV_DST}/"
  ok "device config: ${DEV_DST}"
fi

# ---------------------------------------------------------------------------
step "Budowa"
# ---------------------------------------------------------------------------
if (( APP_ONLY )); then
  log "przebudowa tylko AeroRadar"
  ( cd "${AOSP_DIR}" && m AeroRadar ) || die "build AeroRadar nie powiódł się"
  APK=$(find "${AOSP_DIR}/out" -name "AeroRadar.apk" -newer "${AOSP_DIR}/build/envsetup.sh" | head -1)
  [[ -n "${APK}" ]] && cp "${APK}" "${ARCVM_OUT}/AeroRadar.apk"
  ok "APK: ${ARCVM_OUT}/AeroRadar.apk"
  summary "${ARCVM_OUT}/AeroRadar.apk"
  exit 0
fi

log "m (pełny build ARCVM, 1–3 h przy pierwszym uruchomieniu)"
( cd "${AOSP_DIR}" && m -j"${JOBS}" ) || die "build AOSP nie powiódł się"

# ---------------------------------------------------------------------------
step "Złożenie obrazu ARCVM"
# ---------------------------------------------------------------------------
# ARCVM używa obrazu z partycjami: system, vendor, product, userdata.
# W drzewie Chromium OS służy do tego `arcvm_image` / `build_arcvm_image`.
ARCVM_TOOL="${SRC_DIR}/src/src/platform2/vm_tools/arcvm/build_arcvm_image.sh"

if [[ -x "${ARCVM_TOOL}" ]]; then
  log "używam build_arcvm_image.sh z drzewa Chromium OS"
  "${ARCVM_TOOL}" --board="${BOARD}" --aosp-out="${AOSP_DIR}/out/target/product/aerophone" \
                  --output="${ARCVM_IMAGE}" \
    || die "build_arcvm_image.sh nie powiodło się"
else
  warn "brak build_arcvm_image.sh — składam obraz ręcznie (simg2img + resize)"
  SYS_IMG="${AOSP_DIR}/out/target/product/aerophone/system.img"
  VENDOR_IMG="${AOSP_DIR}/out/target/product/aerophone/vendor.img"
  require_file "${SYS_IMG}"
  require_file "${VENDOR_IMG}"

  # Obraz ARCVM: GPT z partycjami system/vendor/product
  TMP="${ARCVM_OUT}/tmp"
  rm -rf "${TMP}"; mkdir -p "${TMP}"

  # Rozmiar: system + vendor + product + 512 MB zapasu
  SYS_SZ=$(stat -c%s "${SYS_IMG}")
  VEN_SZ=$(stat -c%s "${VENDOR_IMG}")
  TOTAL=$(( SYS_SZ + VEN_SZ + 512*1024*1024 ))

  truncate -s "${TOTAL}" "${ARCVM_IMAGE}"
  parted -s "${ARCVM_IMAGE}" mklabel gpt
  parted -s "${ARCVM_IMAGE}" mkpart system ext4 1MiB $(( SYS_SZ/1024/1024 + 2 ))MiB
  parted -s "${ARCVM_IMAGE}" mkpart vendor ext4 $(( SYS_SZ/1024/1024 + 2 ))MiB 100%

  # Zapisz obrazy partycji obok (flash.sh wgrywa je przez fastboot)
  cp "${SYS_IMG}" "${ARCVM_OUT}/system.img"
  cp "${VENDOR_IMG}" "${ARCVM_OUT}/vendor.img"
  ok "obrazy partycji: system.img, vendor.img"
fi

[[ -f "${ARCVM_IMAGE}" ]] || die "brak obrazu ARCVM: ${ARCVM_IMAGE}"

# ---------------------------------------------------------------------------
step "Weryfikacja zawartości (AeroRadar w priv-app)"
# ---------------------------------------------------------------------------
if command -v debugfs >/dev/null 2>&1 && [[ -f "${ARCVM_OUT}/system.img" ]]; then
  if debugfs -R "ls /system/priv-app" "${ARCVM_OUT}/system.img" 2>/dev/null | grep -q AeroRadar; then
    ok "AeroRadar obecny w /system/priv-app ✓"
  else
    warn "nie znalazłem AeroRadar w /system/priv-app — sprawdź device.mk"
  fi
fi

ok "obraz ARCVM gotowy"
summary "${ARCVM_IMAGE}" "${ARCVM_OUT}/system.img" "${ARCVM_OUT}/vendor.img"
log "Następny krok: make image"
