#!/usr/bin/env bash
# flash.sh — wgranie obrazu Ninetails Mobile na urządzenie.
#
# Użycie:
#   ./build/flash.sh                    # auto-detekcja urządzenia
#   ./build/flash.sh --serial ABC123    # konkretne urządzenie
#   ./build/flash.sh --full             # pełny flash (wszystkie partycje)
#   ./build/flash.sh --ota              # tylko aktualizacja slotu B
#
# Wymagania:
#   - odblokowany bootloader (fastboot flashing unlock)
#   - adb + fastboot w PATH
#   - urządzenie w trybie fastboot (adb reboot bootloader)
#
# UWAGA: flash kasuje dane użytkownika (--full). Zrób backup.

source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${AEROPHONE_ROOT}/build/lib/common.sh"

SERIAL=""
FULL=0
OTA_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) SERIAL="$2"; shift 2 ;;
    --full)   FULL=1; shift ;;
    --ota)    OTA_ONLY=1; shift ;;
    *) die "nieznany argument: $1" ;;
  esac
done

require_cmd adb fastboot

IMG_OUT="${OUT_DIR}/image"
FB=(fastboot)
[[ -n "${SERIAL}" ]] && FB+=( -s "${SERIAL}" )

# ---------------------------------------------------------------------------
step "1/6 Weryfikacja urządzenia"
# ---------------------------------------------------------------------------
DEVICES=$("${FB[@]}" devices 2>/dev/null | grep -c "fastboot" || true)
if (( DEVICES == 0 )); then
  warn "brak urządzenia w trybie fastboot"
  log "próba przełączenia przez adb..."
  adb devices | grep -q "device$" || die "brak urządzenia (adb i fastboot puste)"
  adb reboot bootloader
  log "czekam na fastboot (max 60 s)"
  for i in $(seq 1 30); do
    sleep 2
    "${FB[@]}" devices 2>/dev/null | grep -q "fastboot" && break
  done
  "${FB[@]}" devices 2>/dev/null | grep -q "fastboot" || die "urządzenie nie weszło w fastboot"
fi
ok "urządzenie wykryte: $("${FB[@]}" devices | head -1)"

# Sprawdź stan bootloadera
UNLOCKED=$("${FB[@]}" getvar unlocked 2>&1 | grep -o "unlocked: *yes" || true)
if [[ -z "${UNLOCKED}" ]]; then
  warn "bootloader może być zablokowany"
  warn "odblokuj: fastboot flashing unlock (kasuje dane!)"
  read -r -p "kontynuować mimo to? [t/N] " ans
  [[ "${ans,,}" == "t" ]] || die "przerwano"
fi

# ---------------------------------------------------------------------------
step "2/6 Weryfikacja artefaktów"
# ---------------------------------------------------------------------------
for f in boot.img system.img; do
  require_file "${IMG_OUT}/${f}"
done
[[ -f "${IMG_OUT}/arcvm.img" ]] || warn "brak arcvm.img — system bez warstwy Android"
[[ -f "${IMG_OUT}/vbmeta.img" ]] || warn "brak vbmeta.img — boot może wymagać --disable-verity"

ok "artefakty kompletne"

# ---------------------------------------------------------------------------
step "3/6 (opcjonalnie) Wipe danych"
# ---------------------------------------------------------------------------
if (( FULL )); then
  warn "FULL flash: dane użytkownika zostaną usunięte"
  read -r -p "kontynuować? [t/N] " ans
  [[ "${ans,,}" == "t" ]] || die "przerwano"
  "${FB[@]}" erase userdata || warn "erase userdata nie powiodło się (może być OK)"
  "${FB[@]}" erase metadata || true
  ok "dane wyczyszczone"
fi

# ---------------------------------------------------------------------------
step "4/6 Flash partycji"
# ---------------------------------------------------------------------------
flash_part() {
  local part="$1" img="$2"
  [[ -f "${img}" ]] || { warn "pomijam ${part} (brak ${img})"; return 0; }
  log "fastboot flash ${part} $(basename "${img}")"
  "${FB[@]}" flash "${part}" "${img}" || die "flash ${part} nie powiódł się"
  ok "${part} ✓"
}

if (( OTA_ONLY )); then
  # Aktualizacja slotu B (A/B): piszemy do _b, potem set_active b
  log "aktualizacja slotu B"
  "${FB[@]}" --set-active=b || warn "set-active=b nie powiodło się"
  flash_part "boot_b"   "${IMG_OUT}/boot.img"
  flash_part "system_b" "${IMG_OUT}/system.img"
  flash_part "arcvm_b"  "${IMG_OUT}/arcvm.img"
  "${FB[@]}" --set-active=b || die "nie mogę ustawić slotu B jako aktywnego"
else
  flash_part "boot_a"   "${IMG_OUT}/boot.img"
  flash_part "system_a" "${IMG_OUT}/system.img"
  flash_part "arcvm_a"  "${IMG_OUT}/arcvm.img"
  flash_part "vbmeta"   "${IMG_OUT}/vbmeta.img"
fi

# ---------------------------------------------------------------------------
step "5/6 Weryfikacja i restart"
# ---------------------------------------------------------------------------
if [[ -f "${IMG_OUT}/vbmeta.img" ]]; then
  log "sprawdzenie łańcucha zaufania"
  "${FB[@]}" getvar current-slot 2>&1 | grep -q "current-slot" \
    && ok "slot: $("${FB[@]}" getvar current-slot 2>&1 | grep -o '[ab]$' | head -1)"
fi

log "restart urządzenia"
"${FB[@]}" reboot || die "reboot nie powiódł się"

# ---------------------------------------------------------------------------
step "6/6 Oczekiwanie na boot"
# ---------------------------------------------------------------------------
log "czekam na adb (max 180 s — pierwszy boot jest dłuższy)"
for i in $(seq 1 90); do
  sleep 2
  if adb devices | grep -q "device$"; then
    ok "urządzenie wystartowało"
    break
  fi
done

if adb devices | grep -q "device$"; then
  echo
  log "weryfikacja systemu:"
  adb shell getprop ro.product.device 2>/dev/null | sed 's/^/  device: /'
  adb shell getprop ro.build.version.release 2>/dev/null | sed 's/^/  android: /'
  adb shell pm list packages 2>/dev/null | grep -i aeroradar | sed 's/^/  /' \
    || warn "AeroRadar nie znaleziony — sprawdź logcat"
  adb shell dumpsys activity service com.android.phone 2>/dev/null | head -3 | sed 's/^/  /' \
    || warn "usługa telefonii nie odpowiada"
else
  warn "urządzenie nie odpowiedziało w 180 s"
  warn "sprawdź: adb logcat | grep -i 'rilbridge\\|aerophone'"
  warn "jeśli bootloop: fastboot --set-active=a (wróć do slotu A)"
fi

ok "flash zakończony"
