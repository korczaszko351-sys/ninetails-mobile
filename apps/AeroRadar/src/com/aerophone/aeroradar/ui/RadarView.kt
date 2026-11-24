package com.aerophone.aeroradar.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.util.AttributeSet
import android.view.View
import com.aerophone.aeroradar.adsb.Aircraft
import kotlin.math.cos
import kotlin.math.sin

/**
 * RadarView — rysuje radar: okrągłe pierścienie + pozycje samolotów.
 *
 * Skala: promień widoku = maksymalna odległość (domyślnie 150 km).
 * Kąt: azymut (0° = północ, zgodnie z ruchem wskazówek zegara).
 *
 * Rysowanie na Canvas (bez zewnętrznych bibliotek mapowych) — radar
 * jest z natury okrągły, więc mapa nie jest potrzebna.
 */
class RadarView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private var aircraft: List<Aircraft> = emptyList()
    private var maxRangeKm = 150.0

    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.parseColor("#2A3B52")
        strokeWidth = 2f
    }
    private val crossPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.parseColor("#1E2C3E")
        strokeWidth = 1f
    }
    private val planePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.parseColor("#0F9D58")
    }
    private val planeHighPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.parseColor("#E8710A")
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#C7D3E3")
        textSize = 26f
        typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
    }
    private val centerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.parseColor("#1A73E8")
    }

    fun setAircraft(list: List<Aircraft>) {
        aircraft = list
        invalidate()
    }

    fun setMaxRange(km: Double) {
        maxRangeKm = km.coerceAtLeast(1.0)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        val cx = width / 2f
        val cy = height / 2f
        val radius = minOf(cx, cy) - 24f

        // Pierścienie: 25%, 50%, 75%, 100% zasięgu
        for (i in 1..4) {
            canvas.drawCircle(cx, cy, radius * i / 4f, ringPaint)
        }

        // Krzyż (N-S, E-W)
        canvas.drawLine(cx, cy - radius, cx, cy + radius, crossPaint)
        canvas.drawLine(cx - radius, cy, cx + radius, cy, crossPaint)

        // Etykiety kierunków
        canvas.drawText("N", cx - 8f, cy - radius - 8f, labelPaint)
        canvas.drawText("S", cx - 8f, cy + radius + 26f, labelPaint)
        canvas.drawText("E", cx + radius + 8f, cy + 8f, labelPaint)
        canvas.drawText("W", cx - radius - 26f, cy + 8f, labelPaint)

        // Środek = użytkownik
        canvas.drawCircle(cx, cy, 8f, centerPaint)

        // Samoloty
        for (a in aircraft) {
            val dist = a.distanceKm ?: continue
            val brg = a.bearingDeg ?: continue
            if (dist > maxRangeKm) continue

            val r = (dist / maxRangeKm).toFloat() * radius
            val rad = Math.toRadians(brg)
            val x = cx + (r * sin(rad)).toFloat()
            val y = cy - (r * cos(rad)).toFloat()

            // Kolor: wyżej = pomarańczowy, niżej = zielony
            val paint = if ((a.altitudeFt ?: 0) > 20000) planeHighPaint else planePaint
            canvas.drawCircle(x, y, 7f, paint)

            // Etykieta Aero ID (tylko dla najbliższych, żeby nie zaśmiecać)
            if (dist < maxRangeKm * 0.6) {
                canvas.drawText(a.aeroId, x + 12f, y + 8f, labelPaint)
            }
        }
    }
}
