package com.aerophone.aeroradar.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.location.Location
import android.location.LocationManager
import android.os.IBinder
import android.util.Log
import com.aerophone.aeroradar.R
import com.aerophone.aeroradar.adsb.Aircraft
import com.aerophone.aeroradar.data.AdsbClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * RadarService — usługa pierwszoplanowa śledząca samoloty w tle.
 *
 * Jako priv-app ma uprawnienie FOREGROUND_SERVICE_LOCATION, więc może
 * działać przy zgaszonym ekranie bez zabijania przez system.
 *
 * Emituje powiadomienie o najbliższym samolocie (aktualizowane co poll),
 * a przy przejściu samolotu "nad głową" (< 5 km) — powiadomienie alertowe.
 */
class RadarService : Service() {

    companion object {
        private const val TAG = "AeroRadar/Service"
        private const val CHANNEL_ONGOING = "radar_ongoing"
        private const val CHANNEL_ALERTS = "radar_alerts"
        private const val NOTIF_ID_ONGOING = 1001
        private const val NOTIF_ID_ALERT = 1002
        private const val OVERHEAD_KM = 5.0

        const val ACTION_START = "com.aerophone.aeroradar.START"
        const val ACTION_STOP = "com.aerophone.aeroradar.STOP"

        fun start(context: Context) {
            val i = Intent(context, RadarService::class.java).setAction(ACTION_START)
            context.startForegroundService(i)
        }

        fun stop(context: Context) {
            val i = Intent(context, RadarService::class.java).setAction(ACTION_STOP)
            context.startService(i)
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var radarJob: Job? = null
    private val client = AdsbClient()
    private var lastOverheadHex: String? = null

    override fun onCreate() {
        super.onCreate()
        createChannels()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopSelf()
                return START_NOT_STICKY
            }
            else -> startRadar()
        }
        return START_STICKY   // system wznowi usługę po zabiciu
    }

    private fun startRadar() {
        if (radarJob?.isActive == true) return

        // Powiadomienie "ongoing" — wymagane dla foreground service
        startForeground(NOTIF_ID_ONGOING, buildOngoingNotification("Uruchamiam radar…"))

        radarJob = scope.launch {
            val loc = lastKnownLocation()
            if (loc == null) {
                Log.w(TAG, "brak lokalizacji — radar nie wystartuje")
                updateOngoing("Brak lokalizacji — włącz GPS")
                return@launch
            }

            client.stream(loc.latitude, loc.longitude).collectLatest { aircraft ->
                onAircraftUpdate(aircraft)
            }
        }
    }

    private fun onAircraftUpdate(list: List<Aircraft>) {
        // Udostępnij dane innym aplikacjom systemowym (AircraftProvider)
        com.aerophone.aeroradar.data.AircraftProvider.snapshot = list

        val nearest = list.firstOrNull()
        if (nearest == null) {
            updateOngoing("Brak samolotów w zasięgu")
            return
        }

        val label = buildString {
            append(nearest.aeroId)
            nearest.distanceKm?.let { append(" · %.1f km".format(it)) }
            nearest.altitudeFt?.let { append(" · %,d ft".format(it)) }
        }
        updateOngoing(label)

        // Alert "nad głową" — tylko raz na samolot
        val dist = nearest.distanceKm ?: return
        if (dist < OVERHEAD_KM && nearest.icaoHex != lastOverheadHex) {
            lastOverheadHex = nearest.icaoHex
            showOverheadAlert(nearest)
        }
    }

    // -----------------------------------------------------------------------
    // Lokalizacja
    // -----------------------------------------------------------------------
    private fun lastKnownLocation(): Location? {
        val lm = getSystemService(Context.LOCATION_SERVICE) as LocationManager
        // Preferuj GPS, potem network
        for (provider in listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)) {
            try {
                lm.getLastKnownLocation(provider)?.let { return it }
            } catch (e: SecurityException) {
                Log.w(TAG, "brak uprawnień do $provider", e)
            }
        }
        return null
    }

    // -----------------------------------------------------------------------
    // Powiadomienia
    // -----------------------------------------------------------------------
    private fun createChannels() {
        val nm = getSystemService(NotificationManager::class.java)

        nm.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ONGOING,
                getString(R.string.channel_ongoing),
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = getString(R.string.channel_ongoing_desc)
                setShowBadge(false)
            }
        )

        nm.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ALERTS,
                getString(R.string.channel_alerts),
                NotificationManager.IMPORTANCE_DEFAULT,
            ).apply {
                description = getString(R.string.channel_alerts_desc)
            }
        )
    }

    private fun buildOngoingNotification(text: String): Notification =
        Notification.Builder(this, CHANNEL_ONGOING)
            .setContentTitle(getString(R.string.notif_radar_title))
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_radar)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .build()

    private fun updateOngoing(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_ID_ONGOING, buildOngoingNotification(text))
    }

    private fun showOverheadAlert(a: Aircraft) {
        val nm = getSystemService(NotificationManager::class.java)
        val body = buildString {
            append(a.aeroId)
            a.typeName?.let { append(" (").append(it).append(")") }
            a.route?.let { append(" · ").append(it) }
            a.distanceKm?.let { append(" · %.1f km".format(it)) }
        }
        val n = Notification.Builder(this, CHANNEL_ALERTS)
            .setContentTitle(getString(R.string.notif_overhead_title))
            .setContentText(body)
            .setSmallIcon(R.drawable.ic_plane)
            .setAutoCancel(true)
            .build()
        nm.notify(NOTIF_ID_ALERT, n)
    }

    override fun onDestroy() {
        radarJob?.cancel()
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null
}
