# BoardConfig.mk — konfiguracja płytki aerophone (AeroPhone OS)
#
# Ten plik definiuje sprzęt: SoC, partycje, A/B, weryfikację rozruchu.
# Jest używany zarówno przez build hosta (Chromium OS overlay), jak i
# przez build ARCVM (AOSP device config).

# ---------------------------------------------------------------------------
# Architektura i SoC
# ---------------------------------------------------------------------------
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-2a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := cortex-a78
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-2a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a55

# SoC: referencyjny devkit (Qualcomm SM7325 / Snapdragon 778G)
# Produkcja: podmienić na docelowy SoC + sterowniki dostawcy
TARGET_BOARD_PLATFORM := sm7325
TARGET_BOOTLOADER_BOARD_NAME := aerophone

# ---------------------------------------------------------------------------
# Kernel
# ---------------------------------------------------------------------------
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_VERSION := 6.1
TARGET_KERNEL_SOURCE := kernel/aerophone
TARGET_KERNEL_CONFIG := aerophone_defconfig
BOARD_KERNEL_CMDLINE := console=ttyMSM0,115200n8 androidboot.hardware=aerophone
BOARD_KERNEL_CMDLINE += androidboot.selinux=enforcing
BOARD_KERNEL_CMDLINE += dm_verity.mode=2
BOARD_KERNEL_CMDLINE += cros_secure=1
BOARD_KERNEL_IMAGE_NAME := Image

# ---------------------------------------------------------------------------
# Partycje (A/B, zgodnie z make-image.sh)
# ---------------------------------------------------------------------------
BOARD_USES_METADATA_PARTITION := true
AB_OTA_UPDATER := true
AB_OTA_PARTITIONS := boot system vendor product arcvm

BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864        # 64 MiB
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472    # 3 GiB
BOARD_VENDORIMAGE_PARTITION_SIZE := 1073741824    # 1 GiB
BOARD_PRODUCTIMAGE_PARTITION_SIZE := 536870912    # 512 MiB
BOARD_USERDATAIMAGE_PARTITION_SIZE := 8589934592  # 8 GiB
BOARD_ARCVMIMAGE_PARTITION_SIZE := 4294967296     # 4 GiB

BOARD_SUPER_PARTITION_SIZE := 8589934592
BOARD_SUPER_PARTITION_GROUPS := aerophone_dynamic
BOARD_AEROPHONE_DYNAMIC_PARTITION_LIST := system vendor product
BOARD_AEROPHONE_DYNAMIC_SIZE := 4831838208

# ---------------------------------------------------------------------------
# Weryfikacja rozruchu (L1)
# ---------------------------------------------------------------------------
BOARD_AVB_ENABLE := true
BOARD_AVB_ALGORITHM := SHA256_RSA4096
BOARD_AVB_KEY_PATH := keys/dev/avb.pem
BOARD_AVB_ROLLBACK_INDEX := 1
BOARD_AVB_ROLLBACK_INDEX_LOCATION := 1

# dm-verity na partycjach systemowych
BOARD_VERITY_MODE := 2
BOARD_VERITY_ERROR_BEHAVIOR := restart

# ---------------------------------------------------------------------------
# Wirtualizacja (L3) — ARCVM
# ---------------------------------------------------------------------------
BOARD_USES_ARCVM := true
BOARD_ARCVM_ANDROID_VERSION := 14
BOARD_ARCVM_USE_PKVM := true          # protected KVM (izolacja gościa)
BOARD_ARCVM_GPU_MODE := virgl          # virtio-gpu + virglrenderer
BOARD_ARCVM_VSOCK_CID := 3             # CID dla mostu RIL
BOARD_ARCVM_MEMORY_MB := 3072          # 3 GB dla gościa

# ---------------------------------------------------------------------------
# Telefonia (L4/L6)
# ---------------------------------------------------------------------------
BOARD_HAS_RADIO := true
BOARD_RIL_CLASS := ../../../device/aerophone/ril/
BOARD_MODEM_BACKEND := qmi             # QMI (Qualcomm) | mbim (uniwersalny)
BOARD_RILBRIDGE_ENABLE := true
BOARD_RILBRIDGE_VSOCK_PORT := 5000
BOARD_RILBRIDGE_MAX_CLIENTS := 4

# ---------------------------------------------------------------------------
# Bezpieczeństwo (SELinux)
# ---------------------------------------------------------------------------
BOARD_SEPOLICY_DIRS += device/aerophone/sepolicy
BOARD_PLAT_PUBLIC_SEPOLICY_DIR += device/aerophone/sepolicy/public
BOARD_PLAT_PRIVATE_SEPOLICY_DIR += device/aerophone/sepolicy/private
BOARD_SEPOLICY_M4DEFS += board_excludes_rilbridge=false

# ---------------------------------------------------------------------------
# Aplikacje systemowe
# ---------------------------------------------------------------------------
# AeroRadar jako priv-app (uprawnienia systemowe: lokalizacja w tle,
# dostęp do sieci, powiadomienia o samolotach)
PRODUCT_PACKAGES += AeroRadar
PRODUCT_PACKAGES += AeroRadarOverlay

# ---------------------------------------------------------------------------
# Znakowanie
# ---------------------------------------------------------------------------
PRODUCT_BRAND := AeroPhone
PRODUCT_MANUFACTURER := AeroPhone
PRODUCT_MODEL := AeroPhone One
PRODUCT_DEVICE := aerophone
TARGET_COPY_OUT_VENDOR := vendor
