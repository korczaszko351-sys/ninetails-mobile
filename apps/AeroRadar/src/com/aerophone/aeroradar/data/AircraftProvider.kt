package com.aerophone.aeroradar.data

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import com.aerophone.aeroradar.adsb.Aircraft

/**
 * AircraftProvider — ContentProvider z danymi o samolotach.
 *
 * Pozwala innym aplikacjom systemowym (widżet ekranu blokady, asystent)
 * czytać dane bez własnego klienta ADS-B. Dostęp chroniony uprawnieniem
 * com.aerophone.permission.READ_AIRCRAFT (signature).
 *
 * URI: content://com.aerophone.aeroradar.provider/aircraft
 *
 * Kolumny: icao, aero_id, callsign, registration, type, airline, route,
 *          lat, lon, altitude_ft, speed_kt, track_deg, distance_km, last_seen
 */
class AircraftProvider : ContentProvider() {

    companion object {
        const val AUTHORITY = "com.aerophone.aeroradar.provider"
        val CONTENT_URI: Uri = Uri.parse("content://$AUTHORITY/aircraft")

        val COLUMNS = arrayOf(
            "icao", "aero_id", "callsign", "registration", "type", "airline",
            "route", "lat", "lon", "altitude_ft", "speed_kt", "track_deg",
            "distance_km", "last_seen",
        )

        /**
         * Ostatnia znana lista samolotów — aktualizowana przez RadarService.
         * W produkcji: wspólna pamięć albo baza Room; tu prosta statyczna lista.
         */
        @Volatile
        var snapshot: List<Aircraft> = emptyList()
    }

    override fun onCreate(): Boolean = true

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?,
    ): Cursor {
        val cols = projection ?: COLUMNS
        val cursor = MatrixCursor(cols)

        val data = snapshot.let {
            if (sortOrder?.contains("distance") == true) {
                it.sortedBy { a -> a.distanceKm ?: Double.MAX_VALUE }
            } else it
        }

        for (a in data) {
            val row = arrayOfNulls<Any>(cols.size)
            for ((i, c) in cols.withIndex()) {
                row[i] = when (c) {
                    "icao" -> a.icaoHex
                    "aero_id" -> a.aeroId
                    "callsign" -> a.callsign
                    "registration" -> a.registration
                    "type" -> a.typeCode
                    "airline" -> a.airline
                    "route" -> a.route
                    "lat" -> a.latitude
                    "lon" -> a.longitude
                    "altitude_ft" -> a.altitudeFt
                    "speed_kt" -> a.groundSpeedKt
                    "track_deg" -> a.trackDeg
                    "distance_km" -> a.distanceKm
                    "last_seen" -> a.lastSeenMs
                    else -> null
                }
            }
            cursor.addRow(row)
        }
        return cursor
    }

    override fun getType(uri: Uri): String = "vnd.android.cursor.dir/vnd.aeroradar.aircraft"

    // Tylko odczyt — zapis przez usługę, nie przez provider
    override fun insert(uri: Uri, values: ContentValues?): Uri? =
        throw UnsupportedOperationException("AircraftProvider jest tylko do odczytu")

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int =
        throw UnsupportedOperationException("AircraftProvider jest tylko do odczytu")

    override fun update(
        uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?,
    ): Int = throw UnsupportedOperationException("AircraftProvider jest tylko do odczytu")
}
