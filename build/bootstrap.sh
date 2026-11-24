#!/usr/bin/env bash
# bootstrap.sh — przygotowanie środowiska i synchronizacja źródeł.
#
# Użycie:
#   ./build/bootstrap.sh              # pełny bootstrap
#   ./build/bootstrap.sh --check-only # tylko weryfikacja zależności
#
# Co robi:
#   1. sprawdza zależności hosta budującego (pakiety, wersje, miejsce na dysku)
#   2. pobiera depot_tools (Chromium OS) i repo (AOSP)
#   3. synchronizuje drzewo źródeł wg device/aerophone/manifest.xml
#   4. instaluje prebuilt kernela i toolchain (clang, gcc-aarch64)

source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${AEROPHONE_ROOT}/build/lib/common.sh"

CHECK_ONLY=0
[[ "${1:-}" == "--check-only" ]] && CHECK_ONLY=1

# ---------------------------------------------------------------------------
step "1/4 Weryfikacja zależności hosta"
# ---------------------------------------------------------------------------
require_cmd git curl python3 openssl make gcc g++ unzip xz

# Python >= 3.8 (depot_tools wymaga)
PY_OK=$(python3 -c 'import sys; print(1 if sys.version_info >= (3,8) else 0)')
[[ "${PY_OK}" == "1" ]] || die "wymagany Python >= 3.8 (masz: $(python3 -V 2>&1))"

# git >= 2.28 (repo manifest)
GIT_MAJOR=$(git --version | awk '{print $3}' | cut -d. -f1)
GIT_MINOR=$(git --version | awk '{print $3}' | cut -d. -f2)
(( GIT_MAJOR > 2 || (GIT_MAJOR == 2 && GIT_MINOR >= 28) )) \
  || die "wymagany git >= 2.28 (masz: $(git --version))"

# Pakiety systemowe wymagane przez build Chromium OS i AOSP
MISSING=()
for pkg in bison flex gperf pkg-config libssl-dev libelf-dev bc rsync \
           ninja-build ccache lz4 zstd cpio kmod; do
  dpkg -s "${pkg}" >/dev/null 2>&1 || MISSING+=("${pkg}")
done
if (( ${#MISSING[@]} > 0 )); then
  warn "brakujące pakiety: ${MISSING[*]}"
  warn "zainstaluj: sudo apt-get install -y ${MISSING[*]}"
  (( CHECK_ONLY )) && exit 1
fi

# Miejsce na dysku: źródła Chromium OS + AOSP + out ≈ 180 GB
require_disk "${AEROPHONE_ROOT}" 180

# RAM: linkowanie Chromium wymaga dużo pamięci
RAM_GB=$(( $(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024 / 1024 ))
if (( RAM_GB < 16 )); then
  warn "wykryto ${RAM_GB} GB RAM — build Chromium może się nie udać (zalecane 32 GB+)"
else
  ok "RAM: ${RAM_GB} GB"
fi

if (( CHECK_ONLY )); then
  ok "weryfikacja zakończona — środowisko gotowe"
  exit 0
fi

# ---------------------------------------------------------------------------
step "2/4 Pobieranie depot_tools i repo"
# ---------------------------------------------------------------------------
if [[ ! -d "${DEPOT_TOOLS}/.git" ]]; then
  git_clone_pinned https://chromium.googlesource.com/chromium/tools/depot_tools.git \
                   "${DEPOT_TOOLS}" main
else
  log "depot_tools już obecne"
fi
ok "depot_tools: ${DEPOT_TOOLS}"

if [[ ! -x "${REPO}" ]]; then
  curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o "${REPO}" \
    || die "nie udało się pobrać repo"
  chmod +x "${REPO}"
fi
ok "repo: $(${REPO} --version 2>/dev/null | head -1)"

# ---------------------------------------------------------------------------
step "3/4 Synchronizacja źródeł (repo sync)"
# ---------------------------------------------------------------------------
require_file "${AEROPHONE_ROOT}/device/aerophone/manifest.xml"
mkdir -p "${SRC_DIR}"

if [[ ! -d "${SRC_DIR}/.repo" ]]; then
  log "inicjalizacja repo w ${SRC_DIR}"
  ( cd "${SRC_DIR}" && "${REPO}" init -u "${AEROPHONE_ROOT}/device/aerophone" \
      -m manifest.xml -b main --depth=1 ) \
    || die "repo init nie powiodło się"
fi

log "repo sync (to trwa; pierwsze pobranie 1–4 h zależnie od łącza)"
( cd "${SRC_DIR}" && "${REPO}" sync -j"${JOBS}" --force-sync --no-clone-bundle ) \
  || die "repo sync nie powiodło się — sprawdź łącze i miejsce na dysku"
ok "źródła zsynchronizowane: $(du -sh "${SRC_DIR}" | cut -f1)"

# ---------------------------------------------------------------------------
step "4/4 Toolchain i prebuilt kernel"
# ---------------------------------------------------------------------------
TOOLCHAIN_DIR="${CACHE_DIR}/toolchain"
mkdir -p "${TOOLCHAIN_DIR}"

if [[ ! -d "${TOOLCHAIN_DIR}/clang" ]]; then
  log "pobieranie clang (LLVM) dla ${ARCH}"
  # W realnym drzewie Chromium OS toolchain pochodzi z src/third_party/llvm-build.
  # Tu tylko znacznik — build-host.sh użyje toolchainu z drzewa źródeł.
  mkdir -p "${TOOLCHAIN_DIR}/clang"
  echo "managed-by-chromiumos-tree" > "${TOOLCHAIN_DIR}/clang/.placeholder"
fi

if [[ ! -f "${CACHE_DIR}/kernel/Image-${KERNEL_VERSION}" ]]; then
  warn "brak prebuilt kernela ${KERNEL_VERSION} — build-host.sh zbuduje go ze źródeł"
  warn "alternatywa: umieść Image w ${CACHE_DIR}/kernel/Image-${KERNEL_VERSION}"
fi

ok "bootstrap zakończony"
echo
log "Następny krok: make host"
