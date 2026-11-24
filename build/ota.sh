# ota.sh — wygenerowanie payloadu OTA (A/B) dla Ninetails Mobile.
#
# Tworzy payload aktualizacji przyrostowej albo pełnej, podpisany kluczem
# deweloperskim. Wynik wgrywa się przez update_engine na urządzeniu.
#
# Użycie:
#   ./build/ota.sh --full --to out/image/aerophone-user.img
#   ./build/ota.sh --delta --from out/image/old.img --to out/image/new.img

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

MODE="full"
FROM_IMG=""
TO_IMG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --full)  MODE="full"; shift ;;
    --delta) MODE="delta"; shift ;;
    --from)  FROM_IMG="$2"; shift 2 ;;
    --to)    TO_IMG="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,12p' "$0"
      exit 0 ;;
    *) die "nieznany argument: $1" ;;
  esac
done

[[ -n "${TO_IMG}" ]] || die "podaj obraz docelowy: --to PATH"
[[ -f "${TO_IMG}" ]] || die "nie ma pliku: ${TO_IMG}"

step "Przygotowanie środowiska OTA"
require_cmd openssl
require_cmd python3

# delta_generator jest opcjonalny — sprawdzamy sami (require_cmd kończy skrypt)
HAVE_DELTA=0
command -v delta_generator >/dev/null 2>&1 && HAVE_DELTA=1

OTA_DIR="${OUT_DIR}/ota"
mkdir -p "${OTA_DIR}"

KEY="${KEYS_DIR}/payload.pem"
if [[ ! -f "${KEY}" ]]; then
  warn "brak klucza ${KEY} — generuję deweloperski"
  mkdir -p "${KEYS_DIR}"
  openssl genrsa -out "${KEY}" 4096 2>/dev/null
fi

step "Generowanie payloadu (${MODE})"

if [[ "${MODE}" == "delta" ]]; then
  [[ -n "${FROM_IMG}" ]] || die "tryb delta wymaga --from PATH"
  [[ -f "${FROM_IMG}" ]] || die "nie ma pliku: ${FROM_IMG}"
  if (( HAVE_DELTA )); then
    delta_generator \
      --out_file="${OTA_DIR}/payload.bin" \
      --old_image="${FROM_IMG}" \
      --new_image="${TO_IMG}" \
      --new_unsigned_payload=false
    log "payload przyrostowy: ${OTA_DIR}/payload.bin"
  else
    warn "brak delta_generator — nie mogę zrobić delty"
    warn "zainstaluj: https://android.googlesource.com/platform/system/update_engine/"
    exit 1
  fi
else
  if (( HAVE_DELTA )); then
    delta_generator \
      --out_file="${OTA_DIR}/payload.bin" \
      --new_image="${TO_IMG}" \
      --new_unsigned_payload=false
  else
    warn "brak delta_generator — tworzę payload zastępczy (tylko do testów)"
    cp "${TO_IMG}" "${OTA_DIR}/payload.bin"
  fi
  log "payload pełny: ${OTA_DIR}/payload.bin"
fi

step "Podpisywanie payloadu"
openssl dgst -sha256 -sign "${KEY}" \
  -out "${OTA_DIR}/payload.bin.sig" "${OTA_DIR}/payload.bin"
log "podpis: ${OTA_DIR}/payload.bin.sig"

step "Metadane"
SIZE=$(stat -c%s "${OTA_DIR}/payload.bin")
SHA=$(sha256sum "${OTA_DIR}/payload.bin" | cut -d' ' -f1)
cat > "${OTA_DIR}/payload_properties.txt" <<EOF
FILE_HASH=${SHA}
FILE_SIZE=${SIZE}
METADATA_HASH=${SHA}
METADATA_SIZE=${SIZE}
EOF

cat > "${OTA_DIR}/metadata.json" <<EOF
{
  "product": "${PRODUCT_NAME}",
  "device": "${DEVICE}",
  "version": "${BUILD_ID}",
  "type": "${MODE}",
  "sha256": "${SHA}",
  "size": ${SIZE},
  "generated": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

log "metadane: ${OTA_DIR}/payload_properties.txt, ${OTA_DIR}/metadata.json"

step "Gotowe"
cat <<EOF

Payload OTA: ${OTA_DIR}/payload.bin

Wgranie na urządzenie (przez update_engine):
  adb push ${OTA_DIR}/payload.bin /data/ota/payload.bin
  adb push ${OTA_DIR}/payload_properties.txt /data/ota/
  adb shell update_engine_client \\
      --payload=/data/ota/payload.bin \\
      --headers="FILE_HASH=$(grep FILE_HASH ${OTA_DIR}/payload_properties.txt | cut -d= -f2)" \\
      --follow

Albo przez sideload (recovery):
  adb reboot recovery
  adb sideload ${OTA_DIR}/payload.bin

EOF
