#!/usr/bin/env bash
# Wspólne funkcje pomocnicze. Źródłowane po env.sh.

if [[ -t 1 ]]; then
  C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YEL=$'\033[33m'
  C_BLU=$'\033[34m'; C_DIM=$'\033[2m'; C_RST=$'\033[0m'
else
  C_RED=""; C_GRN=""; C_YEL=""; C_BLU=""; C_DIM=""; C_RST=""
fi

log()  { printf '%s[ninetails]%s %s\n' "${C_BLU}" "${C_RST}" "$*"; }
ok()   { printf '%s[   ok   ]%s %s\n' "${C_GRN}" "${C_RST}" "$*"; }
warn() { printf '%s[  warn  ]%s %s\n' "${C_YEL}" "${C_RST}" "$*" >&2; }
die()  { printf '%s[  fail  ]%s %s\n' "${C_RED}" "${C_RST}" "$*" >&2; exit 1; }
step() { printf '\n%s==> %s%s\n' "${C_BLU}" "$*" "${C_RST}"; }

require_cmd() {
  local c
  for c in "$@"; do
    command -v "$c" >/dev/null 2>&1 || die "brak narzędzia: $c (zainstaluj i spróbuj ponownie)"
  done
}

require_file() {
  [[ -f "$1" ]] || die "brak pliku: $1"
}

require_dir() {
  [[ -d "$1" ]] || die "brak katalogu: $1"
}

# Sprawdza minimalną ilość wolnego miejsca w GB na partycji danego katalogu.
require_disk() {
  local path="$1" need_gb="$2"
  local avail_gb
  avail_gb=$(df -BG --output=avail "$path" 2>/dev/null | tail -1 | tr -dc '0-9')
  [[ -n "${avail_gb}" ]] || { warn "nie mogę sprawdzić miejsca na $path"; return 0; }
  if (( avail_gb < need_gb )); then
    die "za mało miejsca w $path: ${avail_gb} GB, potrzeba ${need_gb} GB"
  fi
  ok "miejsce w $path: ${avail_gb} GB (wymagane ${need_gb} GB)"
}

# Idempotentne pobranie repozytorium git (płytkie, z pinem).
git_clone_pinned() {
  local url="$1" dest="$2" ref="$3"
  if [[ -d "${dest}/.git" ]]; then
    log "repozytorium już istnieje: ${dest}"
    git -C "${dest}" fetch --depth 1 origin "${ref}" >/dev/null 2>&1 || true
    git -C "${dest}" checkout -q "${ref}" 2>/dev/null || warn "nie udało się przełączyć ${dest} na ${ref}"
  else
    mkdir -p "$(dirname "${dest}")"
    git clone --depth 1 --branch "${ref}" "${url}" "${dest}" \
      || die "klonowanie nie powiodło się: ${url} @ ${ref}"
  fi
}

# Podpisanie obrazu kluczem z KEYS_DIR (devkit) lub przez HSM (produkcja).
sign_image() {
  local image="$1" key="${2:-${KEYS_DIR}/vbmeta.pem}"
  if [[ "${NINETAILS_HSM:-0}" == "1" ]]; then
    log "podpisywanie przez HSM: ${image}"
    command -v pkcs11-sign >/dev/null || die "brak pkcs11-sign (HSM)"
    pkcs11-sign --slot "${HSM_SLOT:?}" --in "${image}" --out "${image}.sig"
  else
    require_file "${key}"
    log "podpisywanie kluczem deweloperskim: ${key}"
    openssl dgst -sha256 -sign "${key}" -out "${image}.sig" "${image}"
  fi
  ok "podpisano: ${image}.sig"
}

# Wypisuje podsumowanie artefaktów z rozmiarami.
summary() {
  echo
  printf '%s%-46s %10s%s\n' "${C_DIM}" "ARTEFAKT" "ROZMIAR" "${C_RST}"
  local f
  for f in "$@"; do
    if [[ -f "$f" ]]; then
      printf '%-46s %10s\n' "${f#${AEROPHONE_ROOT}/}" "$(du -h "$f" | cut -f1)"
    else
      printf '%-46s %10s\n' "${f#${AEROPHONE_ROOT}/}" "BRAK"
    fi
  done
  echo
}
