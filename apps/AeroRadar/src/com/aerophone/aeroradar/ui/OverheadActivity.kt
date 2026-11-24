package com.aerophone.aeroradar.ui

import android.os.Bundle
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.lifecycle.lifecycleScope
import com.aerophone.aeroradar.R
import com.aerophone.aeroradar.data.AdsbClient
import kotlinx.coroutines.launch

/**
 * OverheadActivity — szybki podgląd "co nade mną?".
 *
 * Uruchamiana skrótem (intent com.aerophone.aeroradar.OVERHEAD) albo
 * z widżetu. Pokazuje najbliższy samolot w przezroczystym oknie.
 */
class OverheadActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_overhead)

        val title = findViewById<TextView>(R.id.overhead_title)
        val detail = findViewById<TextView>(R.id.overhead_detail)

        lifecycleScope.launch {
            try {
                val lm = getSystemService(LOCATION_SERVICE) as android.location.LocationManager
                val loc = lm.getLastKnownLocation(android.location.LocationManager.GPS_PROVIDER)
                    ?: lm.getLastKnownLocation(android.location.LocationManager.NETWORK_PROVIDER)

                if (loc == null) {
                    title.text = getString(R.string.err_no_fix)
                    return@launch
                }

                val list = AdsbClient().fetch(loc.latitude, loc.longitude)
                val nearest = list.firstOrNull()
                if (nearest == null) {
                    title.text = getString(R.string.status_empty)
                } else {
                    title.text = nearest.aeroId
                    detail.text = buildString {
                        nearest.typeName?.let { append(it).append(" · ") }
                        nearest.distanceKm?.let { append("%.1f km".format(it)).append(" · ") }
                        nearest.altitudeFt?.let { append("%,d ft".format(it)) }
                    }
                }
            } catch (_: Exception) {
                title.text = getString(R.string.status_empty)
            }
        }
    }
}
