#!/usr/bin/env bash
# make-image.sh — złożenie finalnego obrazu urządzenia z obrazów hosta i ARCVM.
#
# Użycie:
#   ./build/make-image.sh
#   ./build/make-image.sh --no-arcvm   # obraz bez ARCVM (tylko host, do debugowania)
#
# Wynik: out/image/aerophone-<wariant>.img
#        out/image/{boot,vbmeta,super,arcvm}.img  (do flashowania przez fastboot)
#
# Układ partycji (GPT, A/B):
#   p1  KERN-A     p2  KERN-B
#   p3  ROOT-A     p4  ROOT-B
#   p5  OEM        p6  MISC
#   p7  ARCVM-A    p8  ARCVM-B
#   p9  USERDATA   p10 VBMETA

source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${AEROPHONE_ROOT}/build/lib/common.sh"

NO_ARCVM=0
[[ "${1:-}" == "--no-arcvm" ]] && NO_ARCVM=1

IMG_OUT="${OUT_DIR}/image"
mkdir -p "${IMG_OUT}"

require_file "${HOST_IMAGE}"
if (( ! NO_ARCVM )); then
  require_file "${ARCVM_IMAGE}"
fi

# ---------------------------------------------------------------------------
step "1/5 Obliczenie rozmiaru obrazu"
# ---------------------------------------------------------------------------
HOST_SZ=$(stat -c%s "${HOST_IMAGE}")
ARCVM_SZ=0
(( ! NO_ARCVM )) && ARCVM_SZ=$(stat -c%s "${ARCVM_IMAGE}")

# USERDATA: 8 GB (telefon), zapas 1 GB na metadane
USERDATA_SZ=$(( 8 * 1024 * 1024 * 1024 ))
TOTAL=$(( HOST_SZ * 2 + ARCVM_SZ * 2 + USERDATA_SZ + 1024*1024*1024 ))

log "host:   $(numfmt --to=iec "${HOST_SZ}")"
(( ! NO_ARCVM )) && log "arcvm:  $(numfmt --to=iec "${ARCVM_SZ}")"
log "razem:  $(numfmt --to=iec "${TOTAL}")"

require_disk "${IMG_OUT}" $(( TOTAL / 1024 / 1024 / 1024 + 2 ))

# ---------------------------------------------------------------------------
step "2/5 Tworzenie tablicy partycji (GPT, A/B)"
# ---------------------------------------------------------------------------
rm -f "${DISK_IMAGE}"
truncate -s "${TOTAL}" "${DISK_IMAGE}"

parted -s "${DISK_IMAGE}" mklabel gpt
parted -s "${DISK_IMAGE}" mkpart KERN-A   ext4 1MiB 65MiB
parted -s "${DISK_IMAGE}" mkpart KERN-B   ext4 65MiB 129MiB
parted -s "${DISK_IMAGE}" mkpart ROOT-A   ext4 129MiB $(( 129 + HOST_SZ/1024/1024 ))MiB
parted -s "${DISK_IMAGE}" mkpart ROOT-B   ext4 $(( 129 + HOST_SZ/1024/1024 ))MiB $(( 129 + 2*HOST_SZ/1024/1024 ))MiB
parted -s "${DISK_IMAGE}" mkpart OEM      ext4 $(( 129 + 2*HOST_SZ/1024/1024 ))MiB $(( 129 + 2*HOST_SZ/1024/1024 + 32 ))MiB
parted -s "${DISK_IMAGE}" mkpart MISC     ext4 $(( 129 + 2*HOST_SZ/1024/1024 + 32 ))MiB $(( 129 + 2*HOST_SZ/1024/1024 + 40 ))MiB

if (( ! NO_ARCVM )); then
  A_START=$(( 129 + 2*HOST_SZ/1024/1024 + 40 ))
  A_END=$(( A_START + ARCVM_SZ/1024/1024 ))
  B_END=$(( A_END + ARCVM_SZ/1024/1024 ))
  parted -s "${DISK_IMAGE}" mkpart ARCVM-A ext4 ${A_START}MiB ${A_END}MiB
  parted -s "${DISK_IMAGE}" mkpart ARCVM-B ext4 ${A_END}MiB ${B_END}MiB
  parted -s "${DISK_IMAGE}" mkpart USERDATA ext4 ${B_END}MiB 100%
else
  parted -s "${DISK_IMAGE}" mkpart USERDATA ext4 $(( 129 + 2*HOST_SZ/1024/1024 + 40 ))MiB 100%
fi

ok "partycje utworzone"
parted -s "${DISK_IMAGE}" print

# ---------------------------------------------------------------------------
step "3/5 Zapis obrazów na partycje"
# ---------------------------------------------------------------------------
# Uwaga: zapis przez dd na offset partycji (bez montowania — szybsze i bezpieczniejsze)
write_part() {
  local part_num="$1" src="$2" label="$3"
  local start_sector
  start_sector=$(parted -s -m "${DISK_IMAGE}" unit s print | awk -F: -v n="${part_num}" '$1==n{gsub(/s/,"",$2); print $2}')
  [[ -n "${start_sector}" ]] || die "nie mogę odczytać offsetu partycji ${part_num}"
  log "zapis ${label} → partycja ${part_num} (sektor ${start_sector})"
  dd if="${src}" of="${DISK_IMAGE}" bs=512 seek="${start_sector}" conv=notrunc,sparse status=none \
    || die "dd nie powiodło się dla ${label}"
}

write_part 1 "${HOST_IMAGE}" "KERN-A"
write_part 3 "${HOST_IMAGE}" "ROOT-A"
(( ! NO_ARCVM )) && write_part 7 "${ARCVM_IMAGE}" "ARCVM-A"

ok "obrazy zapisane"

# ---------------------------------------------------------------------------
step "4/5 vbmeta i podpis"
# ---------------------------------------------------------------------------
VBMETA="${IMG_OUT}/vbmeta.img"
if command -v avbtool >/dev/null 2>&1; then
  log "avbtool make_vbmeta_image"
  avbtool make_vbmeta_image \
    --output "${VBMETA}" \
    --key "${KEYS_DIR}/avb.pem" \
    --algorithm SHA256_RSA4096 \
    --flags 2 \
    --include_descriptors_from_image "${HOST_IMAGE}" \
    || die "avbtool nie powiodło się"
  ok "vbmeta: ${VBMETA}"
else
  warn "brak avbtool — pomijam vbmeta (build deweloperski, bootloader odblokowany)"
fi

# ---------------------------------------------------------------------------
step "5/5 Rozbicie na obrazy do fastboot"
# ---------------------------------------------------------------------------
# fastboot flashuje pojedyncze partycje — wyciągamy je z obrazu dysku.
extract_part() {
  local part_num="$1" out="$2"
  local start_sector count
  start_sector=$(parted -s -m "${DISK_IMAGE}" unit s print | awk -F: -v n="${part_num}" '$1==n{gsub(/s/,"",$2); print $2}')
  count=$(parted -s -m "${DISK_IMAGE}" unit s print | awk -F: -v n="${part_num}" '$1==n{gsub(/s/,"",$4); print $4}')
  dd if="${DISK_IMAGE}" of="${out}" bs=512 skip="${start_sector}" count="${count}" status=none
}

extract_part 1 "${IMG_OUT}/boot.img"
extract_part 3 "${IMG_OUT}/system.img"
(( ! NO_ARCVM )) && extract_part 7 "${IMG_OUT}/arcvm.img"
ok "obrazy partycji wyodrębnione"

ok "obraz urządzenia gotowy"
summary "${DISK_IMAGE}" "${IMG_OUT}/boot.img" "${IMG_OUT}/system.img" "${IMG_OUT}/arcvm.img" "${VBMETA}"
log "Następny krok: make flash (urządzenie w trybie fastboot)"
