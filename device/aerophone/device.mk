# device.mk — pakiety i właściwości urządzenia aerophone
#
# Definiuje, co wchodzi do obrazu: usługi hosta, ARCVM, aplikacje systemowe.

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base_telephony.mk)

PRODUCT_NAME := aerophone
PRODUCT_DEVICE := aerophone
PRODUCT_BRAND := AeroPhone
PRODUCT_MODEL := AeroPhone One

# ---------------------------------------------------------------------------
# Warstwa telefonii (L4 host + L6 gość)
# ---------------------------------------------------------------------------
PRODUCT_PACKAGES += \
    rilbridged \
    aerophone-telephony-init \
    aerophone-telephony-dbus \
    android.hardware.radio-service.aerophone

# Konfiguracja modemu
PRODUCT_COPY_FILES += \
    device/aerophone/telephony/modem.conf:$(TARGET_COPY_OUT_VENDOR)/etc/modem.conf \
    device/aerophone/telephony/apns-conf.xml:$(TARGET_COPY_OUT_VENDOR)/etc/apns-conf.xml

# ---------------------------------------------------------------------------
# ARCVM (L3/L6)
# ---------------------------------------------------------------------------
PRODUCT_PACKAGES += \
    arcvm-image \
    crosvm \
    concierge \
    cicerone

PRODUCT_PROPERTY_OVERRIDES += \
    ro.boot.arcvm.enabled=true \
    ro.arcvm.vsock.cid=3 \
    ro.arcvm.memory_mb=3072

# ---------------------------------------------------------------------------
# Aplikacja systemowa AeroRadar (L8)
# ---------------------------------------------------------------------------
PRODUCT_PACKAGES += \
    AeroRadar \
    AeroRadarOverlay

# AeroRadar jako priv-app: uprawnienia systemowe.
# Moduł AeroRadarPrivappPermissions (prebuilt_etc w apps/AeroRadar/Android.bp)
# sam kopiuje plik do /system/etc/permissions — nie duplikujemy tego przez
# PRODUCT_COPY_FILES (podwójny wpis = błąd "duplicate module").
PRODUCT_PACKAGES += AeroRadarPrivappPermissions

# ---------------------------------------------------------------------------
# Właściwości systemu
# ---------------------------------------------------------------------------
PRODUCT_PROPERTY_OVERRIDES += \
    ro.product.aerophone.version=1.0.0 \
    ro.aerophone.telephony.bridge=vsock \
    ro.aerophone.telephony.backend=qmi \
    persist.aerophone.aeroradar.enabled=true

# ---------------------------------------------------------------------------
# SELinux
# ---------------------------------------------------------------------------
BOARD_SEPOLICY_DIRS += device/aerophone/sepolicy

# ---------------------------------------------------------------------------
# Znakowanie / build
# ---------------------------------------------------------------------------
PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_DEVICE=aerophone \
    PRODUCT_NAME=aerophone

# Wariant: userdebug ma roota i adb, user nie
ifeq ($(TARGET_BUILD_VARIANT),userdebug)
  PRODUCT_PROPERTY_OVERRIDES += ro.debuggable=1
  ADDITIONAL_DEFAULT_PROPERTIES += ro.adb.secure=0
endif
