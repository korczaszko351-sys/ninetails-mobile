// build.gradle.kts — AeroRadar, build Gradle (CI / deweloperski)
//
// W systemie docelowym AeroRadar buduje Soong (Android.bp) jako priv-app.
// Ten plik pozwala zbudować APK bez drzewa AOSP — używany przez
// .github/workflows/build.yml oraz do lokalnych testów UI.

plugins {
    id("com.android.application") version "8.5.2"
    id("org.jetbrains.kotlin.android") version "1.9.24"
}

android {
    namespace = "com.aerophone.aeroradar"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.aerophone.aeroradar"
        minSdk = 34
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"
    }

    // Źródła i zasoby leżą w układzie AOSP (src/, res/), nie w app/src/main/.
    sourceSets["main"].manifest.srcFile("AndroidManifest.gradle.xml")
    sourceSets["main"].java.srcDirs("src")
    sourceSets["main"].res.srcDirs("res")

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildTypes {
        getByName("debug") {
            isMinifyEnabled = false
        }
        getByName("release") {
            isMinifyEnabled = false
            // Podpisywanie: w CI używamy klucza debug; produkcyjnie klucz platformy.
        }
    }

    lint {
        abortOnError = false
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-ktx:1.9.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
}
