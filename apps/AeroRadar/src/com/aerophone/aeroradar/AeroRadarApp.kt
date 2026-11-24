package com.aerophone.aeroradar

import android.app.Application
import android.util.Log

/**
 * AeroRadarApp — klasa Application.
 *
 * Inicjalizuje: kanały powiadomień (przez RadarService), domyślne
 * ustawienia, oraz weryfikuje dostęp do mostu telefonii (opcjonalny).
 */
class AeroRadarApp : Application() {

    override fun onCreate() {
        super.onCreate()

        // Domyślne ustawienia przy pierwszym uruchomieniu
        val prefs = getSharedPreferences("aeroradar", MODE_PRIVATE)
        if (!prefs.contains("radius_km")) {
            prefs.edit()
                .putInt("radius_km", 150)
                .putBoolean("units_metric", true)
                .putBoolean("alerts_overhead", true)
                .putBoolean("radar_enabled", false)
                .apply()
        }

        Log.i("AeroRadar/App", "AeroRadar wystartował (Ninetails Mobile)")
    }
}
