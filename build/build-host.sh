#!/usr/bin/env bash
# build-host.sh — budowa obrazu hosta (Chromium OS) dla płytki aerophone.
#
# Użycie:
#   ./build/build-host.sh                 # pełny build
#   ./build/build-host.sh --kernel-only   # tylko kernel
#   VARIANT=user ./build/build-host.sh    # build produkcyjny
#
# Wynik: out/host/chromiumos_image.bin (+ vmlinuz, rootfs, vbmeta)
#
# Uwaga: Chromium OS buduje się przez `cros build-packages` / `build_image`
# z drzewa src/. Skrypt opakowuje te narzędzia i dokłada nasz overlay.

source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${AEROPHONE_ROOT}/build/lib/common.sh"

KERNEL_ONLY=0
[[ "${1:-}" == "--kernel-only" ]] && KERNEL_ONLY=1

require_dir "${SRC_DIR}/src/scripts"   # drzewo Chromium OS

HOST_OUT="${OUT_DIR}/host"
mkdir -p "${HOST_OUT}"

# ---------------------------------------------------------------------------
step "Przygotowanie SDK (cros_sdk)"
# ---------------------------------------------------------------------------
# cros_sdk uruchamia kontener z pełnym toolchainem Chromium OS.
# Pierwsze uruchomienie pobiera ~2 GB obrazu SDK.
if [[ ! -d "${CACHE_DIR}/cros-sdk" ]]; then
  log "pobieranie SDK Chromium OS (pierwsze uruchomienie, ~10 min)"
  ( cd "${SRC_DIR}/src" && "${DEPOT_TOOLS}/cros_sdk" --create ) \
    || die "nie udało się utworzyć SDK"
fi
ok "SDK gotowe"

# ---------------------------------------------------------------------------
step "Konfiguracja płytki ${BOARD}"
# ---------------------------------------------------------------------------
# Overlay urządzenia: device/aerophone/ → src/src/overlays/overlay-aerophone/
OVERLAY_SRC="${AEROPHONE_ROOT}/device/aerophone"
OVERLAY_DST="${SRC_DIR}/src/src/overlays/overlay-${BOARD}"

if [[ ! -d "${OVERLAY_DST}" ]]; then
  log "kopiowanie overlay do drzewa źródeł"
  mkdir -p "$(dirname "${OVERLAY_DST}")"
  cp -r "${OVERLAY_SRC}" "${OVERLAY_DST}"
else
  log "synchronizacja overlay"
  rsync -a --delete "${OVERLAY_SRC}/" "${OVERLAY_DST}/"
fi
ok "overlay: ${OVERLAY_DST}"

# ---------------------------------------------------------------------------
step "Budowa kernela (${KERNEL_VERSION}, ${ARCH})"
# ---------------------------------------------------------------------------
KERNEL_OUT="${HOST_OUT}/vmlinuz"
KERNEL_PKG="chromeos-kernel-${KERNEL_VERSION//./-}"

if [[ -f "${CACHE_DIR}/kernel/Image-${KERNEL_VERSION}" ]]; then
  log "używam prebuilt kernela z cache"
  cp "${CACHE_DIR}/kernel/Image-${KERNEL_VERSION}" "${KERNEL_OUT}"
else
  log "budowa kernela ze źródeł (może potrwać 20–40 min)"
  ( cd "${SRC_DIR}/src" && "${DEPOT_TOOLS}/cros_sdk" -- \
      bash -lc "cros_workon --board=${BOARD} start ${KERNEL_PKG} && \
                emerge-${BOARD} ${KERNEL_PKG}" ) \
    || die "budowa kernela nie powiodła się"
  # Artefakt trafia do chroot; kopiujemy do out/
  find "${SRC_DIR}/src" -name "Image-${KERNEL_VERSION}" -newer "${SRC_DIR}/src/.repo" \
       -exec cp {} "${KERNEL_OUT}" \; 2>/dev/null || true
fi
[[ -f "${KERNEL_OUT}" ]] || die "brak artefaktu kernela: ${KERNEL_OUT}"
ok "kernel: $(du -h "${KERNEL_OUT}" | cut -f1)"

if (( KERNEL_ONLY )); then
  summary "${KERNEL_OUT}"
  exit 0
fi

# ---------------------------------------------------------------------------
step "Budowa pakietów systemowych (host)"
# ---------------------------------------------------------------------------
# Pakiety: chromeos-base, chromeos-login, shill, powerd, update_engine,
#          concierge, crosvm, cicerone, systemd, oraz nasz rilbridged.
log "emerge pakietów (30–90 min przy pierwszym buildzie)"
( cd "${SRC_DIR}/src" && "${DEPOT_TOOLS}/cros_sdk" -- \
    bash -lc "cros_workon --board=${BOARD} start chromeos-base/aerophone-telephony && \
              emerge-${BOARD} virtual/target-os" ) \
  || die "emerge nie powiodło się — sprawdź logi w src/src/out/"

# ---------------------------------------------------------------------------
step "Budowa obrazu dysku hosta"
# ---------------------------------------------------------------------------
log "build_image (rootfs + partycje)"
( cd "${SRC_DIR}/src" && "${DEPOT_TOOLS}/cros_sdk" -- \
    bash -lc "./build_image --board=${BOARD} --noenable_rootfs_verification" ) \
  || die "build_image nie powiodło się"

# Zbierz artefakty
for f in chromiumos_image.bin rootfs.img; do
  found=$(find "${SRC_DIR}/src" -name "${f}" -newer "${SRC_DIR}/src/.repo" 2>/dev/null | head -1)
  [[ -n "${found}" ]] && cp "${found}" "${HOST_OUT}/"
done

[[ -f "${HOST_IMAGE}" ]] || die "brak obrazu hosta: ${HOST_IMAGE}"

# ---------------------------------------------------------------------------
step "Podpisanie obrazu (vbmeta)"
# ---------------------------------------------------------------------------
sign_image "${HOST_IMAGE}"

ok "obraz hosta gotowy"
summary "${HOST_IMAGE}" "${KERNEL_OUT}"
log "Następny krok: make arcvm"
