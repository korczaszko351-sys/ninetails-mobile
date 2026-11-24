package com.aerophone.aeroradar.ui

import android.os.Bundle
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.lifecycle.lifecycleScope
import com.aerophone.aeroradar.R
import com.aerophone.aeroradar.adsb.Aircraft
import com.aerophone.aeroradar.data.AdsbClient
import kotlinx.coroutines.launch

/**
 * AircraftDetailActivity — ekran szczegółów samolotu (Aero ID).
 *
 * Pokazuje pełną identyfikację: callsign, rejestracja, typ, linia, trasa,
 * parametry lotu. To jest "Aero ID" w praktyce — zamiast hexu użytkownik
 * widzi "LOT3AB · Boeing 737-800 · LOT Polish Airlines · WAW → FRA".
 */
class AircraftDetailActivity : ComponentActivity() {

    companion object {
        const val EXTRA_ICAO = "icao_hex"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_aircraft_detail)

        val icao = intent.getStringExtra(EXTRA_ICAO)
        if (icao.isNullOrBlank()) {
            finish()
            return
        }

        findViewById<TextView>(R.id.detail_icao).text = icao.uppercase()

        // Pobierz szczegóły (jednorazowo — pełny rekord z API)
        lifecycleScope.launch {
            try {
                val lm = getSystemService(LOCATION_SERVICE) as android.location.LocationManager
                val loc = lm.getLastKnownLocation(android.location.LocationManager.GPS_PROVIDER)
                if (loc != null) {
                    val list = AdsbClient().fetch(loc.latitude, loc.longitude)
                    list.firstOrNull { it.icaoHex.equals(icao, ignoreCase = true) }
                        ?.let { bind(it) }
                }
            } catch (_: Exception) {
                // Brak sieci — pokazujemy to, co mamy (sam hex)
            }
        }
    }

    private fun bind(a: Aircraft) {
        findViewById<TextView>(R.id.detail_callsign).text = a.callsign ?: "—"
        findViewById<TextView>(R.id.detail_registration).text = a.registration ?: "—"
        findViewById<TextView>(R.id.detail_type).text =
            listOfNotNull(a.typeCode, a.typeName).joinToString(" · ").ifEmpty { "—" }
        findViewById<TextView>(R.id.detail_airline).text = a.airline ?: "—"
        findViewById<TextView>(R.id.detail_route).text = a.route ?: "—"
        findViewById<TextView>(R.id.detail_altitude).text =
            a.altitudeFt?.let { "%,d ft".format(it) } ?: "—"
        findViewById<TextView>(R.id.detail_speed).text =
            a.groundSpeedKt?.let { "$it kt" } ?: "—"
        findViewById<TextView>(R.id.detail_distance).text =
            a.distanceKm?.let { "%.1f km".format(it) } ?: "—"
        findViewById<TextView>(R.id.detail_heading).text =
            a.trackDeg?.let { "$it°" } ?: "—"
        findViewById<TextView>(R.id.detail_squawk).text = a.squawk ?: "—"
    }
}
