package com.aerophone.aeroradar.adsb

/**
 * Model pojedynczego samolotu widzianego przez radar.
 *
 * "Aero ID" to nasz identyfikator samolotu: łączy surowe dane ADS-B
 * (ICAO 24-bit address, callsign) z danymi referencyjnymi (rejestracja,
 * typ, linia, trasa), żeby użytkownik zobaczył "co to za samolot",
 * a nie tylko hex.
 */
data class Aircraft(
    /** ICAO 24-bit address — unikalny identyfikator transpondera (hex). */
    val icaoHex: String,

    /** Callsign z ADS-B (np. "LOT3AB"), może być pusty. */
    val callsign: String? = null,

    /** Rejestracja (np. "SP-LVA") — z bazy referencyjnej, nie z ADS-B. */
    val registration: String? = null,

    /** Typ maszyny (np. "B738", "A320"). */
    val typeCode: String? = null,

    /** Pełna nazwa typu (np. "Boeing 737-800"). */
    val typeName: String? = null,

    /** Linia lotnicza (np. "LOT Polish Airlines"). */
    val airline: String? = null,

    /** Trasa: lotnisko wylotu → przylotu (np. "WAW → FRA"). */
    val route: String? = null,

    /** Pozycja (stopnie dziesiętne). */
    val latitude: Double? = null,
    val longitude: Double? = null,

    /** Wysokość barometryczna w stopach. */
    val altitudeFt: Int? = null,

    /** Prędkość względem ziemi w węzłach. */
    val groundSpeedKt: Int? = null,

    /** Kurs w stopniach (0 = północ). */
    val trackDeg: Int? = null,

    /** Pionowa prędkość w ft/min (dodatnia = wznoszenie). */
    val verticalRateFpm: Int? = null,

    /** Squawk (kod transpondera). */
    val squawk: String? = null,

    /** Znacznik czasu ostatniej aktualizacji (ms epoch). */
    val lastSeenMs: Long = 0L,

    /** Odległość od użytkownika w km (obliczana lokalnie). */
    val distanceKm: Double? = null,

    /** Azymut od użytkownika w stopniach. */
    val bearingDeg: Double? = null,
) {
    /**
     * Aero ID — czytelny identyfikator do wyświetlenia.
     * Priorytet: callsign → rejestracja → hex.
     */
    val aeroId: String
        get() = callsign?.takeIf { it.isNotBlank() }
            ?: registration?.takeIf { it.isNotBlank() }
            ?: icaoHex.uppercase()

    /** Czy samolot jest w powietrzu (wysokość > 500 ft). */
    val isAirborne: Boolean
        get() = (altitudeFt ?: 0) > 500

    /** Krótki opis do listy: "LOT3AB · B738 · 11 200 ft". */
    fun shortLabel(): String = buildString {
        append(aeroId)
        typeCode?.let { append(" · ").append(it) }
        altitudeFt?.let { append(" · ").append("%,d".format(it)).append(" ft") }
    }
}
