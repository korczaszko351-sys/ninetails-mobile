#!/usr/bin/env bash
# Wspólne zmienne środowiskowe dla wszystkich skryptów budowania.
# Źródłowany przez build/*.sh — nie uruchamiaj bezpośrednio.

set -euo pipefail

# --- Katalogi ---------------------------------------------------------------
export AEROPHONE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export OUT_DIR="${AEROPHONE_ROOT}/out"
export SRC_DIR="${AEROPHONE_ROOT}/src"
export CACHE_DIR="${AEROPHONE_ROOT}/.cache"

# --- Płytka i wariant -------------------------------------------------------
export BOARD="${BOARD:-aerophone}"
export VARIANT="${VARIANT:-userdebug}"     # userdebug | user | eng
export ARCH="${ARCH:-arm64}"
export JOBS="${JOBS:-$(nproc)}"

# --- Wersje (pinowane — zmiana = nowy build ID) -----------------------------
export CHROMEOS_BRANCH="${CHROMEOS_BRANCH:-release-R126-15886.B}"
export AOSP_TAG="${AOSP_TAG:-android-14.0.0_r50}"   # ARCVM guest
export ARCVM_ANDROID_VERSION="14"
export KERNEL_VERSION="6.1"

# --- Ścieżki narzędzi -------------------------------------------------------
export DEPOT_TOOLS="${CACHE_DIR}/depot_tools"
export REPO="${CACHE_DIR}/bin/repo"
export PATH="${DEPOT_TOOLS}:${CACHE_DIR}/bin:${PATH}"

# --- Artefakty --------------------------------------------------------------
export HOST_IMAGE="${OUT_DIR}/host/chromiumos_image.bin"
export ARCVM_IMAGE="${OUT_DIR}/arcvm/arcvm.img"
export DISK_IMAGE="${OUT_DIR}/image/aerophone-${VARIANT}.img"
export OTA_PAYLOAD="${OUT_DIR}/ota/payload.bin"

# --- Klucze (devkit: klucze deweloperskie; produkcja: HSM) ------------------
export KEYS_DIR="${KEYS_DIR:-${AEROPHONE_ROOT}/keys/dev}"

# --- Zachowanie -------------------------------------------------------------
export LC_ALL=C
export TZ=UTC

mkdir -p "${OUT_DIR}" "${CACHE_DIR}/bin"
