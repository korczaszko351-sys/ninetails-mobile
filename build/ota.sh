#!/usr/bin/env bash
# ota.sh — generowanie podpisanego payloadu OTA (A/B) dla Ninetails Mobile.
#
# Użycie:
#   ./build/ota.sh --from out/image/aerophone-userdebug.img \
#                  --to   out/image/aerophone-user.img
#   ./build/ota.sh --full   # pełny payload (nie przyrostowy)
#
# Wynik: out/ota/payload.bin + payload_properties.txt + metadata
#
# Aktualizacje A/B: update_engine zapisuje payload do nieaktywnego slotu,
# weryfikuje podpis i sumy, potem przełącza slot przy restarcie.
# ARCVM jest częścią tego samego payloadu — inaczej host i gość
# mogłyby się rozjechać wersjami (niedozwolone).

source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${AEROPHONE_ROOT}/build/lib/common.sh"

FROM_IMG=""
TO_IMG=""
FULL=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from) FROM_IMG="$2"; shift 2 ;;
    --to)   TO_IMG="$2"; shift 2 ;;
    --full) FULL=1; shift ;;
    *) die "nieznany argument: $1" ;;
  esac
done

OTA_OUT="${OUT_DIR}/ota"
mkdir -p "${OTA_OUT}"

require_cmd delta_generator 2>/dev/null || true

# ---------------------------------------------------------------------------
step "1/4 Weryfikacja wejścia"
# ---------------------------------------------------------------------------
if (( FULL )); then
  [[ -n "${TO_IMG}" ]] || TO_IMG="${DISK_IMAGE}"
  require_file "${TO_IMG}"
  log "payload pełny z: ${TO_IMG}"
else
  [[ -n "${FROM_IMG}" && -n "${TO_IMG}" ]] \
    || die "podaj --from i --to (albo --full --to)"
  require_file "${FROM_IMG}"
  require_file "${TO_IMG}"
  log "payload przyrostowy: ${FROM_IMG} → ${TO_IMG}"
fi

# ---------------------------------------------------------------------------
step "2/4 Generowanie payloadu"
# ---------------------------------------------------------------------------
PAYLOAD="${OTA_OUT}/payload.bin"
PROPS="${OTA_OUT}/payload_properties.txt"

if command -v delta_generator >/dev/null 2>&1; then
  if (( FULL )); then
    delta_generator \
      --out_file="${PAYLOAD}" \
      --new_image="${TO_IMG}" \
      --new_partitions=boot:${OUT_DIR}/image/boot.img \
      --new_partitions=system:${OUT_DIR}/image/system.img \
      --new_partitions=arcvm:${OUT_DIR}/image/arcvm.img \
      --private_key="${KEYS_DIR}/payload.pem" \
      --signature_size=256 \
      || die "delta_generator (full) nie powiodło się"
  else
    delta_generator \
      --out_file="${PAYLOAD}" \
      --old_image="${FROM_IMG}" \
      --new_image="${TO_IMG}" \
      --private_key="${KEYS_DIR}/payload.pem" \
      --signature_size=256 \
      || die "delta_generator (delta) nie powiodło się"
  fi
else
  warn "brak delta_generator (brillo/payload_generator) — generuję payload zastępczy"
  warn "w pełnym drzewie: emerge-${BOARD} update_engine"
  # Zastępczy payload: skompresowany obraz + sumy (do testów przepływu OTA)
  xz -T0 -9 -c "${TO_IMG}" > "${PAYLOAD}.xz" || die "kompresja nie powiodła się"
  mv "${PAYLOAD}.xz" "${PAYLOAD}"
  sha256sum "${PAYLOAD}" | awk '{print "FILE_HASH="$1}' > "${PROPS}"
  stat -c "FILE_SIZE=%s" "${PAYLOAD}" >> "${PROPS}"
  echo "METADATA_HASH=$(sha256sum "${PROPS}" | cut -d' ' -f1)" >> "${PROPS}"
fi

[[ -f "${PAYLOAD}" ]] || die "payload nie powstał"
ok "payload: $(du -h "${PAYLOAD}" | cut -f1)"

# ---------------------------------------------------------------------------
step "3/4 Metadane OTA"
# ---------------------------------------------------------------------------
BUILD_ID="$(date -u +%Y%m%d.%H%M)-${VARIANT}"
META="${OTA_OUT}/metadata.json"
cat > "${META}" <<EOF
{
  "board": "${BOARD}",
  "variant": "${VARIANT}",
  "build_id": "${BUILD_ID}",
  "chromeos_branch": "${CHROMEOS_BRANCH}",
  "arcvm_android": "${ARCVM_ANDROID_VERSION}",
  "kernel": "${KERNEL_VERSION}",
  "payload": "payload.bin",
  "payload_size": $(stat -c%s "${PAYLOAD}"),
  "payload_sha256": "$(sha256sum "${PAYLOAD}" | cut -d' ' -f1)",
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "type": "$( (( FULL )) && echo full || echo delta )"
}
EOF
ok "metadane: ${META}"

# ---------------------------------------------------------------------------
step "4/4 Instrukcja wgrania OTA"
# ---------------------------------------------------------------------------
cat <<EOF

Payload gotowy. Dwie drogi wgrania:

A) Przez update_engine (zalecane, bez utraty danych):
   adb push ${PAYLOAD} /data/local/tmp/payload.bin
   adb shell update_engine_client \\
     --payload=/data/local/tmp/payload.bin \\
     --headers="$(cat "${PROPS}" | tr '\n' ' ')" \\
     --follow
   # po zakończeniu: adb reboot (slot przełączy się automatycznie)

B) Przez fastboot (slot B, szybsze, wymaga reboot):
   ./build/flash.sh --ota

Weryfikacja po restarcie:
   adb shell getprop ro.build.id          # powinien być ${BUILD_ID}
   adb shell getprop ro.boot.slot_suffix  # _b po aktualizacji
   adb shell dumpsys update_engine | head -20

Rollback (jeśli coś nie działa):
   fastboot --set-active=a && fastboot reboot

EOF

summary "${PAYLOAD}" "${PROPS}" "${META}"
