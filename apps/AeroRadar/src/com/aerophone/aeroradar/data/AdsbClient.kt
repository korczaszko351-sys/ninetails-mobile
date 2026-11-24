package com.aerophone.aeroradar.data

import com.aerophone.aeroradar.adsb.Aircraft
import com.aerophone.aeroradar.adsb.GeoMath
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * AdsbClient — źródło danych o samolotach.
 *
 * Dwa tryby:
 *   1. SIECIOWY (domyślny): odpytuje API agregatora ADS-B o samoloty
 *      w zadanym promieniu od pozycji użytkownika.
 *   2. LOKALNY (opcjonalny): czyta ramki z własnego odbiornika RTL-SDR
 *      podłączonego przez USB OTG (wymaga sterownika w kernelu).
 *
 * API: endpoint jest konfigurowalny (SettingsActivity), bo różni
 * dostawcy mają różne warunki użycia. Domyślnie używamy publicznego
 * endpointu zgodnego z formatem /api/aircraft?lat=..&lon=..&radius=..
 *
 * UWAGA PRAWNA: dane ADS-B mają warunki użycia dostawcy. Przed
 * komercyjnym wdrożeniem sprawdź licencję (patrz docs/AERORADAR.md).
 */
class AdsbClient(
    private val endpoint: String = DEFAULT_ENDPOINT,
    private val radiusKm: Int = 150,
) {

    companion object {
        // Konfigurowalny — patrz SettingsActivity
        const val DEFAULT_ENDPOINT = "https://api.aeroradar.example/v1/aircraft"
        const val POLL_INTERVAL_MS = 5_000L
    }

    /**
     * Strumień samolotów wokół pozycji. Emituje nową listę co
     * POLL_INTERVAL_MS. Anulowanie korutyny zatrzymuje pętlę.
     */
    fun stream(lat: Double, lon: Double): Flow<List<Aircraft>> = flow {
        while (true) {
            val list = try {
                fetch(lat, lon)
            } catch (e: Exception) {
                emptyList()   // błąd sieci — nie zabijamy strumienia
            }
            emit(list)
            delay(POLL_INTERVAL_MS)
        }
    }.flowOn(Dispatchers.IO)

    /**
     * Pobiera jedną porcję danych i mapuje na model Aircraft.
     */
    suspend fun fetch(lat: Double, lon: Double): List<Aircraft> {
        val url = URL("$endpoint?lat=$lat&lon=$lon&radius=$radiusKm")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = 8_000
            readTimeout = 8_000
            setRequestProperty("Accept", "application/json")
            setRequestProperty("User-Agent", "AeroRadar/1.0 (Ninetails Mobile)")
        }

        try {
            if (conn.responseCode != 200) {
                throw java.io.IOException("HTTP ${conn.responseCode}")
            }
            val body = conn.inputStream.bufferedReader().use { it.readText() }
            return parseResponse(body, lat, lon)
        } finally {
            conn.disconnect()
        }
    }

    /**
     * Parsuje odpowiedź JSON.
     *
     * Oczekiwany format (elastyczny — obsługujemy dwa warianty):
     *   { "aircraft": [ { "hex": "48C12A", "flight": "LOT3AB", "alt_baro": 34000,
     *                     "gs": 450, "track": 275, "lat": 52.1, "lon": 20.9,
     *                     "t": "B738", "r": "SP-LVA", "baro_rate": 0 }, ... ] }
     * albo tablica bez opakowania.
     */
    private fun parseResponse(body: String, userLat: Double, userLon: Double): List<Aircraft> {
        val root = JSONObject("{\"aircraft\":$body}")
        val arr = root.optJSONArray("aircraft") ?: return emptyList()

        val out = ArrayList<Aircraft>(arr.length())
        for (i in 0 until arr.length()) {
            val o = arr.optJSONObject(i) ?: continue
            val hex = o.optString("hex", "").takeIf { it.isNotBlank() } ?: continue

            val aLat = o.optDouble("lat", Double.NaN)
            val aLon = o.optDouble("lon", Double.NaN)
            val hasPos = !aLat.isNaN() && !aLon.isNaN()

            val dist = if (hasPos) GeoMath.distanceKm(userLat, userLon, aLat, aLon) else null
            val brg = if (hasPos) GeoMath.bearingDeg(userLat, userLon, aLat, aLon) else null

            out += Aircraft(
                icaoHex = hex.uppercase(),
                callsign = o.optString("flight", "").trim().takeIf { it.isNotEmpty() },
                registration = o.optString("r", "").takeIf { it.isNotEmpty() },
                typeCode = o.optString("t", "").takeIf { it.isNotEmpty() },
                typeName = o.optString("desc", "").takeIf { it.isNotEmpty() },
                airline = o.optString("ownOp", "").takeIf { it.isNotEmpty() },
                route = o.optString("route", "").takeIf { it.isNotEmpty() },
                latitude = if (hasPos) aLat else null,
                longitude = if (hasPos) aLon else null,
                altitudeFt = o.optInt("alt_baro", -1).takeIf { it >= 0 },
                groundSpeedKt = o.optInt("gs", -1).takeIf { it >= 0 },
                trackDeg = o.optInt("track", -1).takeIf { it >= 0 },
                verticalRateFpm = o.optInt("baro_rate", Int.MIN_VALUE)
                    .takeIf { it != Int.MIN_VALUE },
                squawk = o.optString("squawk", "").takeIf { it.isNotEmpty() },
                lastSeenMs = System.currentTimeMillis(),
                distanceKm = dist,
                bearingDeg = brg,
            )
        }

        // Sortuj po odległości — najbliższe na górze
        return out.sortedBy { it.distanceKm ?: Double.MAX_VALUE }
    }
}
