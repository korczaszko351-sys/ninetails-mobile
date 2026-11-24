package com.aerophone.aeroradar.ui

import android.os.Bundle
import android.widget.CheckBox
import android.widget.EditText
import android.widget.SeekBar
import android.widget.TextView
import androidx.activity.ComponentActivity
import com.aerophone.aeroradar.R

/**
 * SettingsActivity — ustawienia radaru.
 *
 * Zapisuje do SharedPreferences ("aeroradar"):
 *   radius_km, units_metric, alerts_overhead, radar_enabled, endpoint
 */
class SettingsActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        val prefs = getSharedPreferences("aeroradar", MODE_PRIVATE)

        // Promień
        val radiusBar = findViewById<SeekBar>(R.id.seek_radius)
        val radiusLabel = findViewById<TextView>(R.id.label_radius)
        val savedRadius = prefs.getInt("radius_km", 150)
        radiusBar.progress = (savedRadius - 25) / 25   // 25..300 km → 0..11
        radiusLabel.text = getString(R.string.settings_radius) + ": $savedRadius km"
        radiusBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                val km = 25 + progress * 25
                radiusLabel.text = getString(R.string.settings_radius) + ": $km km"
                prefs.edit().putInt("radius_km", km).apply()
            }
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })

        // Jednostki
        val metric = findViewById<CheckBox>(R.id.check_metric)
        metric.isChecked = prefs.getBoolean("units_metric", true)
        metric.setOnCheckedChangeListener { _, checked ->
            prefs.edit().putBoolean("units_metric", checked).apply()
        }

        // Alerty
        val alerts = findViewById<CheckBox>(R.id.check_alerts)
        alerts.isChecked = prefs.getBoolean("alerts_overhead", true)
        alerts.setOnCheckedChangeListener { _, checked ->
            prefs.edit().putBoolean("alerts_overhead", checked).apply()
        }

        // Radar włączony (start po boocie)
        val enabled = findViewById<CheckBox>(R.id.check_enabled)
        enabled.isChecked = prefs.getBoolean("radar_enabled", false)
        enabled.setOnCheckedChangeListener { _, checked ->
            prefs.edit().putBoolean("radar_enabled", checked).apply()
        }

        // Endpoint API
        val endpoint = findViewById<EditText>(R.id.edit_endpoint)
        endpoint.setText(prefs.getString("endpoint", com.aerophone.aeroradar.data.AdsbClient.DEFAULT_ENDPOINT))
        endpoint.setOnFocusChangeListener { _, hasFocus ->
            if (!hasFocus) {
                prefs.edit().putString("endpoint", endpoint.text.toString().trim()).apply()
            }
        }
    }
}
