// settings.gradle.kts — projekt Gradle dla AeroRadar (build CI / deweloperski)
//
// UWAGA: to NIE jest build produkcyjny Ninetails Mobile. W systemie AeroRadar
// buduje Soong (apps/AeroRadar/Android.bp) jako priv-app w obrazie ARCVM.
// Ten projekt Gradle istnieje po to, żeby dało się zbudować i przetestować
// sam APK na zwykłej maszynie / w GitHub Actions, bez drzewa AOSP.

pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "AeroRadar"
