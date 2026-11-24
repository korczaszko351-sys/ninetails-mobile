package com.aerophone.aeroradar.ui

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.aerophone.aeroradar.R
import com.aerophone.aeroradar.adsb.Aircraft
import com.aerophone.aeroradar.data.AdsbClient
import com.aerophone.aeroradar.service.RadarService
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * RadarActivity — ekran główny: radar z samolotami wokół użytkownika.
 *
 * Widok: okrągły radar (Canvas) z pozycjami samolotów + lista najbliższych.
 * Klik na samolot → AircraftDetailActivity (Aero ID).
 */
class RadarActivity : ComponentActivity() {

    private lateinit var radarView: RadarView
    private lateinit var statusText: TextView
    private lateinit var listContainer: android.widget.LinearLayout

    private val client = AdsbClient()
    private var lastLat = 0.0
    private var lastLon = 0.0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_radar)

        radarView = findViewById(R.id.radar_view)
        statusText = findViewById(R.id.status_text)
        listContainer = findViewById(R.id.aircraft_list)

        findViewById<View>(R.id.btn_settings).setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        ensurePermissions()
    }

    // -----------------------------------------------------------------------
    // Uprawnienia
    // -----------------------------------------------------------------------
    private fun ensurePermissions() {
        val needed = mutableListOf<String>()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED) {
            needed += Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED) {
            needed += Manifest.permission.POST_NOTIFICATIONS
        }

        if (needed.isEmpty()) {
            startTracking()
        } else {
            ActivityCompat.requestPermissions(this, needed.toTypedArray(), REQ_PERMS)
        }
    }

    // UWAGA: sygnatura musi być dokładnie taka jak w Activity:
    //   Array<String> (nie Array<out String>) — inaczej "overrides nothing".
    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<String>, grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_PERMS) {
            if (grantResults.isNotEmpty() &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                startTracking()
            } else {
                statusText.text = getString(R.string.err_no_location)
            }
        }
    }

    // -----------------------------------------------------------------------
    // Śledzenie
    // -----------------------------------------------------------------------
    private fun startTracking() {
        // Usługa w tle (powiadomienia o samolotach nad głową)
        RadarService.start(this)

        // Lokalizacja — ostatnia znana (bez pełnego LocationListener dla prostoty)
        val lm = getSystemService(LOCATION_SERVICE) as android.location.LocationManager
        val loc = try {
            lm.getLastKnownLocation(android.location.LocationManager.GPS_PROVIDER)
                ?: lm.getLastKnownLocation(android.location.LocationManager.NETWORK_PROVIDER)
        } catch (e: SecurityException) {
            null
        }

        if (loc == null) {
            statusText.text = getString(R.string.err_no_fix)
            return
        }

        lastLat = loc.latitude
        lastLon = loc.longitude
        statusText.text = getString(R.string.status_scanning)

        lifecycleScope.launch {
            client.stream(lastLat, lastLon).collectLatest { aircraft ->
                render(aircraft)
            }
        }
    }

    // -----------------------------------------------------------------------
    // Renderowanie
    // -----------------------------------------------------------------------
    private fun render(aircraft: List<Aircraft>) {
        radarView.setAircraft(aircraft)
        statusText.text = if (aircraft.isEmpty()) {
            getString(R.string.status_empty)
        } else {
            getString(R.string.status_count, aircraft.size)
        }

        listContainer.removeAllViews()
        for (a in aircraft.take(8)) {
            val row = layoutInflater.inflate(R.layout.item_aircraft, listContainer, false)
            row.findViewById<TextView>(R.id.item_id).text = a.aeroId
            row.findViewById<TextView>(R.id.item_detail).text = buildString {
                a.typeCode?.let { append(it).append(" · ") }
                a.distanceKm?.let { append("%.1f km".format(it)).append(" · ") }
                a.altitudeFt?.let { append("%,d ft".format(it)) }
            }
            row.setOnClickListener {
                startActivity(
                    Intent(this, AircraftDetailActivity::class.java)
                        .putExtra(AircraftDetailActivity.EXTRA_ICAO, a.icaoHex)
                )
            }
            listContainer.addView(row)
        }
    }

    companion object {
        private const val REQ_PERMS = 42
    }
}
