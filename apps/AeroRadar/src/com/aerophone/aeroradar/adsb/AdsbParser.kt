package com.aerophone.aeroradar.adsb

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Parser ramek ADS-B (Mode S Extended Squitter).
 *
 * Obsługuje najczęściej używane typy wiadomości:
 *   DF17 (ADS-B) — identyfikacja, pozycja, prędkość
 *   DF18 (TIS-B / ADS-B rebroadcast)
 *
 * Format ramki: 112 bitów = 28 hex znaków.
 *   [0..4]   DF (Downlink Format)
 *   [5..7]   CA (Capability)
 *   [8..31]  ICAO 24-bit address
 *   [32..87] ME (Message, Extended Squitter)
 *   [88..111] PI (Parity/Interrogator ID)
 *
 * Uwaga: to parser ramek, nie odbiornik. Dane w AeroRadar pochodzą z API
 * (patrz AdsbClient) — parser jest potrzebny, gdy urządzenie ma własny
 * odbiornik RTL-SDR albo gdy API zwraca surowe ramki.
 */
object AdsbParser {

    /** Typ wiadomości ME (bits 33-37). */
    private val TC_IDENTIFICATION = 1..4
    private val TC_SURFACE_POSITION = 5..8
    private val TC_AIRBORNE_POSITION_BARO = 9..18
    private const val TC_AIRBORNE_VELOCITY = 19
    private val TC_AIRBORNE_POSITION_GNSS = 20..22

    data class Parsed(
        val icaoHex: String,
        val callsign: String? = null,
        val altitudeFt: Int? = null,
        val latitude: Double? = null,
        val longitude: Double? = null,
        val groundSpeedKt: Int? = null,
        val trackDeg: Int? = null,
        val verticalRateFpm: Int? = null,
        val squawk: String? = null,
    )

    /**
     * Parsuje ramkę hex (28 znaków). Zwraca null, jeśli ramka jest
     * nieprawidłowa albo nieobsługiwana.
     */
    fun parse(hex: String): Parsed? {
        val clean = hex.trim().uppercase().replace(" ", "")
        if (clean.length != 28) return null
        if (!clean.all { it in "0123456789ABCDEF" }) return null

        val bits = hexToBits(clean) ?: return null
        val df = bitsToInt(bits, 0, 5)

        // Obsługujemy tylko DF17 i DF18 (ADS-B)
        if (df != 17 && df != 18) return null

        val icao = bitsToHex(bits, 8, 24)
        val tc = bitsToInt(bits, 32, 5)

        return when (tc) {
            in TC_IDENTIFICATION -> Parsed(
                icaoHex = icao,
                callsign = decodeCallsign(bits),
            )
            in TC_AIRBORNE_POSITION_BARO -> Parsed(
                icaoHex = icao,
                altitudeFt = decodeAltitude(bits),
                // Pozycja CPR wymaga dwóch ramek (even/odd) — patrz CprDecoder
            )
            TC_AIRBORNE_VELOCITY -> decodeVelocity(bits, icao)
            in TC_AIRBORNE_POSITION_GNSS -> Parsed(
                icaoHex = icao,
                altitudeFt = decodeAltitude(bits),
            )
            else -> Parsed(icaoHex = icao)
        }
    }

    // -----------------------------------------------------------------------
    // Dekodowanie pól
    // -----------------------------------------------------------------------

    /** Callsign: 8 znaków 6-bitowych (charset ADS-B). */
    private fun decodeCallsign(bits: BooleanArray): String? {
        val charset = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ##### ###############0123456789######"
        val sb = StringBuilder(8)
        for (i in 0 until 8) {
            val idx = bitsToInt(bits, 40 + i * 6, 6)
            if (idx < charset.length) sb.append(charset[idx])
        }
        return sb.toString().trim().takeIf { it.isNotEmpty() }
    }

    /** Wysokość: 12 bitów, kodowanie Q-bit (25 ft) lub Gillham (100 ft). */
    private fun decodeAltitude(bits: BooleanArray): Int? {
        val raw = bitsToInt(bits, 40, 12)
        if (raw == 0) return null
        val qBit = (raw shr 4) and 1
        return if (qBit == 1) {
            // 25 ft increments
            val n = ((raw and 0xFE0) shr 1) or (raw and 0xF)
            n * 25 - 1000
        } else {
            // Gillham coded — uproszczone (pełna implementacja w GillhamDecoder)
            null
        }
    }

    /** Prędkość: podtyp 1/2 (prędkość względem ziemi), 3/4 (powietrzna). */
    private fun decodeVelocity(bits: BooleanArray, icao: String): Parsed {
        val subtype = bitsToInt(bits, 37, 3)
        if (subtype != 1 && subtype != 2) return Parsed(icaoHex = icao)

        val ewRaw = bitsToInt(bits, 46, 10)
        val nsRaw = bitsToInt(bits, 57, 10)
        val ewSign = bitsToInt(bits, 45, 1)
        val nsSign = bitsToInt(bits, 56, 1)

        val ew = (ewRaw - 1) * (if (ewSign == 1) -1 else 1)
        val ns = (nsRaw - 1) * (if (nsSign == 1) -1 else 1)

        val speed = sqrt((ew * ew + ns * ns).toDouble()).toInt()
        var track = Math.toDegrees(atan2(ew.toDouble(), ns.toDouble())).toInt()
        if (track < 0) track += 360

        val vrRaw = bitsToInt(bits, 69, 9)
        val vrSign = bitsToInt(bits, 68, 1)
        val vr = if (vrRaw == 0) null else (vrRaw - 1) * 64 * (if (vrSign == 1) -1 else 1)

        return Parsed(
            icaoHex = icao,
            groundSpeedKt = speed,
            trackDeg = track,
            verticalRateFpm = vr,
        )
    }

    // -----------------------------------------------------------------------
    // Pomocnicze: bity
    // -----------------------------------------------------------------------
    private fun hexToBits(hex: String): BooleanArray? {
        val out = BooleanArray(hex.length * 4)
        for ((i, c) in hex.withIndex()) {
            val v = Character.digit(c, 16)
            if (v < 0) return null
            for (b in 0 until 4) {
                out[i * 4 + b] = (v shr (3 - b)) and 1 == 1
            }
        }
        return out
    }

    private fun bitsToInt(bits: BooleanArray, start: Int, len: Int): Int {
        var v = 0
        for (i in start until start + len) {
            v = (v shl 1) or (if (bits[i]) 1 else 0)
        }
        return v
    }

    private fun bitsToHex(bits: BooleanArray, start: Int, len: Int): String {
        val sb = StringBuilder()
        var i = start
        while (i < start + len) {
            var v = 0
            for (b in 0 until 4) v = (v shl 1) or (if (bits[i + b]) 1 else 0)
            sb.append("0123456789ABCDEF"[v])
            i += 4
        }
        return sb.toString()
    }
}

/**
 * Dekoder CPR (Compact Position Reporting) — pozycja z dwóch ramek.
 *
 * ADS-B nadaje pozycję w dwóch połowach: even i odd. Dopiero para
 * pozwala wyliczyć pozycję globalną. Pojedyncza ramka daje pozycję
 * lokalną względem znanej pozycji odbiornika.
 */
object CprDecoder {

    private const val NZ = 15

    data class Frame(
        val latCpr: Int,
        val lonCpr: Int,
        val isOdd: Boolean,
    )

    /**
     * Pozycja globalna z pary ramek (even + odd).
     * Zwraca null, jeśli ramki są zbyt odległe w czasie (>10 s).
     */
    fun decodeGlobal(
        even: Frame,
        odd: Frame,
        evenTimeMs: Long,
        oddTimeMs: Long,
    ): Pair<Double, Double>? {
        if (abs(evenTimeMs - oddTimeMs) > 10_000) return null

        val latEven = even.latCpr / 131072.0
        val latOdd = odd.latCpr / 131072.0
        val lonEven = even.lonCpr / 131072.0
        val lonOdd = odd.lonCpr / 131072.0

        val j = Math.floor(59 * latEven - 60 * latOdd + 0.5).toInt()

        var rlatEven = 6.0 * (mod(j, 60) + latEven)
        var rlatOdd = (360.0 / 59) * (mod(j, 59) + latOdd)

        if (rlatEven >= 270) rlatEven -= 360
        if (rlatOdd >= 270) rlatOdd -= 360

        if (rlatEven < -90 || rlatEven > 90) return null
        if (rlatOdd < -90 || rlatOdd > 90) return null

        // Wybierz ramkę bliższą teraz (zwykle nowszą)
        val useEven = evenTimeMs >= oddTimeMs
        val lat = if (useEven) rlatEven else rlatOdd

        val nl = cprNl(lat)
        val ni = maxOf(nl - if (useEven) 0 else 1, 1)

        val m = Math.floor(lonEven * (nl - 1) - lonOdd * nl + 0.5).toInt()
        val lon = (360.0 / ni) * (mod(m, ni) + if (useEven) lonEven else lonOdd)
        val lonNorm = if (lon > 180) lon - 360 else lon

        return lat to lonNorm
    }

    private fun cprNl(lat: Double): Int {
        val a = abs(lat)
        return when {
            a < 10.47047130 -> 59
            a < 14.82817437 -> 58
            a < 18.18626357 -> 57
            a < 21.02939493 -> 56
            a < 23.54504487 -> 55
            a < 25.82924707 -> 54
            a < 27.93898710 -> 53
            a < 29.91135686 -> 52
            a < 31.77209708 -> 51
            a < 33.53993436 -> 50
            a < 35.22899598 -> 49
            a < 36.85025108 -> 48
            a < 38.41241892 -> 47
            a < 39.92256684 -> 46
            a < 41.38651832 -> 45
            a < 42.80914012 -> 44
            a < 44.19454951 -> 43
            a < 45.54626723 -> 42
            a < 46.86733252 -> 41
            a < 48.16039128 -> 40
            a < 49.42776439 -> 39
            a < 50.67150166 -> 38
            a < 51.89342469 -> 37
            a < 53.09516153 -> 36
            a < 54.27817472 -> 35
            a < 55.44378444 -> 34
            a < 56.59318756 -> 33
            a < 57.72747354 -> 32
            a < 58.84763776 -> 31
            a < 59.95459277 -> 30
            a < 61.04917774 -> 29
            a < 62.13216659 -> 28
            a < 63.20427479 -> 27
            a < 64.26616523 -> 26
            a < 65.31845310 -> 25
            a < 66.36171008 -> 24
            a < 67.39646774 -> 23
            a < 68.42322022 -> 22
            a < 69.44242631 -> 21
            a < 70.45451075 -> 20
            a < 71.45986473 -> 19
            a < 72.45884545 -> 18
            a < 73.45177442 -> 17
            a < 74.43893416 -> 16
            a < 75.42056257 -> 15
            a < 76.39684391 -> 14
            a < 77.36789461 -> 13
            a < 78.33374083 -> 12
            a < 79.29428225 -> 11
            a < 80.24923213 -> 10
            a < 81.19801349 -> 9
            a < 82.13956981 -> 8
            a < 83.07199445 -> 7
            a < 83.99173563 -> 6
            a < 84.89166191 -> 5
            a < 85.75541621 -> 4
            a < 86.53536998 -> 3
            a < 87.00000000 -> 2
            else -> 1
        }
    }

    private fun mod(a: Int, b: Int): Int = ((a % b) + b) % b
}

/**
 * Geometria: odległość i azymut od użytkownika do samolotu.
 */
object GeoMath {

    private const val EARTH_RADIUS_KM = 6371.0

    /** Odległość haversine w km. */
    fun distanceKm(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val dLat = Math.toRadians(lat2 - lat1)
        val dLon = Math.toRadians(lon2 - lon1)
        val a = sin(dLat / 2) * sin(dLat / 2) +
                cos(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) *
                sin(dLon / 2) * sin(dLon / 2)
        return EARTH_RADIUS_KM * 2 * atan2(sqrt(a), sqrt(1 - a))
    }

    /** Azymut (0 = północ, zgodnie z ruchem wskazówek) w stopniach. */
    fun bearingDeg(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val dLon = Math.toRadians(lon2 - lon1)
        val y = sin(dLon) * cos(Math.toRadians(lat2))
        val x = cos(Math.toRadians(lat1)) * sin(Math.toRadians(lat2)) -
                sin(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) * cos(dLon)
        var brng = Math.toDegrees(atan2(y, x))
        if (brng < 0) brng += 360
        return brng
    }
}
