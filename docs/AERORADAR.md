# AeroRadar + Aero ID — integracja z Ninetails Mobile

## Czym jest AeroRadar

AeroRadar to radar ADS-B: pokazuje samoloty wokół użytkownika na mapie/radarze,
z identyfikacją każdej maszyny. **Aero ID** to nasz identyfikator samolotu —
łączy surowe dane transpondera (ICAO 24-bit hex, callsign) z danymi
referencyjnymi (rejestracja, typ, linia, trasa), żeby użytkownik zobaczył
„co to za samolot", a nie tylko ciąg hexów.

## Dlaczego jako aplikacja systemowa (priv-app)

AeroRadar jest instalowany w `/system/priv-app/AeroRadar` i podpisany kluczem
platformy. To daje trzy rzeczy, których zwykła aplikacja z Play by nie miała:

| Uprawnienie | Dlaczego potrzebne | Zwykła apka |
|---|---|---|
| `ACCESS_BACKGROUND_LOCATION` | radar działa przy zgaszonym ekranie | pyta za każdym razem |
| `FOREGROUND_SERVICE_LOCATION` | usługa w tle bez zabijania | ograniczona |
| `POST_NOTIFICATIONS` bez limitu | alerty o samolotach nad głową | rate-limited |
| `READ_PHONE_STATE` | pokrycie sieci dla zasięgu ADS-B | wymaga zgody |

Dodatkowo AeroRadar wystawia `AircraftProvider` — inne aplikacje systemowe
(np. widżet na ekranie blokady) mogą czytać dane o samolotach przez
`content://com.aerophone.aeroradar.provider/aircraft`.

## Architektura integracji

```
┌─────────────────────────────────────────────────────────────┐
│ L8  AeroRadar (priv-app, /system/priv-app)                  │
│     ├── RadarActivity      — radar + lista                  │
│     ├── AircraftDetailActivity — Aero ID                    │
│     ├── RadarService       — foreground service             │
│     └── AircraftProvider   — dane dla innych apek           │
├─────────────────────────────────────────────────────────────┤
│ L6  ARCVM (Android 14)                                      │
│     ├── AdsbClient         — HTTP → API ADS-B               │
│     ├── AdsbParser         — dekodowanie ramek Mode S       │
│     └── CprDecoder         — pozycja z par ramek            │
├─────────────────────────────────────────────────────────────┤
│ L7  Mosty: powiadomienia, lokalizacja, sieć                 │
├─────────────────────────────────────────────────────────────┤
│ L4  Host: shill (sieć), powerd (energia), rilbridged        │
└─────────────────────────────────────────────────────────────┘
```

## Przepływ danych

1. **Lokalizacja** — `RadarService` bierze ostatnią znaną pozycję z
   `LocationManager` (GPS → network fallback).
2. **Pobranie** — `AdsbClient.fetch(lat, lon)` odpytuje API agregatora
   o samoloty w promieniu 150 km, co 5 s.
3. **Parsowanie** — odpowiedź JSON → `List<Aircraft>`; odległość i azymut
   liczone lokalnie (`GeoMath`).
4. **Renderowanie** — `RadarView` rysuje pozycje na okręgu; lista pokazuje
   8 najbliższych.
5. **Alert** — gdy samolot < 5 km i jeszcze nie alertowany →
   powiadomienie „Samolot nad głową".

## Aero ID — skąd się bierze

```
ADS-B (hex 48C12A, callsign LOT3AB)
        │
        ├── baza referencyjna (API): hex → rejestracja SP-LVA, typ B738,
        │                            linia LOT, trasa WAW→FRA
        │
        └── Aero ID = callsign ?: rejestracja ?: hex
                     = "LOT3AB"
```

Priorytet: **callsign → rejestracja → hex**. Callsign jest najbardziej
czytelny („LOT3AB" mówi więcej niż „SP-LVA"), ale nie każdy samolot go nadaje
(np. loty prywatne, wojskowe).

## Konfiguracja

### Endpoint API

Domyślnie `https://api.aeroradar.example/v1/aircraft` — **placeholder**.
Przed wdrożeniem podmień na realny endpoint w `SettingsActivity` albo
w `AdsbClient.DEFAULT_ENDPOINT`.

Oczekiwany format odpowiedzi:

```json
{
  "aircraft": [
    {
      "hex": "48C12A",
      "flight": "LOT3AB",
      "r": "SP-LVA",
      "t": "B738",
      "desc": "Boeing 737-800",
      "ownOp": "LOT Polish Airlines",
      "route": "WAW → FRA",
      "lat": 52.1657,
      "lon": 20.9671,
      "alt_baro": 34000,
      "gs": 450,
      "track": 275,
      "baro_rate": 0,
      "squawk": "1000"
    }
  ]
}
```

### Region i jednostki

Overlay `AeroRadarOverlay` ustawia domyślnie:
- region: Polska (środek 52.0°N, 19.0°E)
- jednostki: metryczne (km, metry) — przełączalne w ustawieniach
- promień: 150 km

## ⚠️ Kwestie prawne — przeczytaj przed wdrożeniem

1. **Dane ADS-B mają warunki użycia dostawcy.** Publiczne agregatory
   (Flightradar24, ADS-B Exchange, OpenSky) mają różne licencje:
   - OpenSky Network — dane badawcze, licencja CC BY-NC (niekomercyjne)
   - ADS-B Exchange — komercyjna, wymaga umowy
   - Flightradar24 — API komercyjne, płatne
   **Nie zakładaj, że „publiczne = darmowe do komercji".**

2. **AeroRadar to nazwa istniejącej aplikacji** (aeroradarlive.com, App Store
   id6814844141). Jeśli budujesz produkt komercyjny, sprawdź znak towarowy
   i rozważ własną nazwę. W tym projekcie „AeroRadar" to robocza nazwa
   wewnętrzna modułu.

3. **Bezpieczeństwo lotnicze.** Aplikacja pokazuje dane ADS-B — **nie jest
   narzędziem nawigacyjnym ani systemem bezpieczeństwa lotniczego**. Ten
   disclaimer musi być widoczny w UI (stopka ekranu szczegółów).

4. **Prywatność.** Lokalizacja użytkownika jest wysyłana do API (lat/lon
   w query string). Jeśli API jest zewnętrzne, to dane osobowe w rozumieniu
   RODO — potrzebna podstawa prawna i informacja w polityce prywatności.

## Testowanie

```bash
# Zbuduj tylko aplikację
./build/build-arcvm.sh --app-only

# Wgraj na urządzenie
adb install -r out/arcvm/AeroRadar.apk   # tylko dla testów (bez uprawnień priv)

# Sprawdź uprawnienia priv-app (po pełnym flashu)
adb shell dumpsys package com.aerophone.aeroradar | grep -E "granted|privapp"

# Uruchom i patrz na logi
adb shell am start -n com.aerophone.aeroradar/.ui.RadarActivity
adb logcat -s AeroRadar/Service AeroRadar/Client
```

## Co jest zrobione, a co nie

| Element | Status |
|---|---|
| Model `Aircraft` + Aero ID | ✅ kompletny |
| Parser ADS-B (DF17/18, callsign, wysokość, prędkość) | ✅ kompletny |
| Dekoder CPR (pozycja z par ramek) | ✅ kompletny |
| `AdsbClient` (HTTP + JSON) | ✅ kompletny, endpoint do podmiany |
| `RadarService` (foreground, alerty) | ✅ kompletny |
| `RadarActivity` (radar + lista) | ✅ kompletny |
| `RadarView` (Canvas: pierścienie, pozycje, etykiety) | ✅ kompletny |
| `AircraftDetailActivity` (Aero ID) | ✅ kompletny |
| `SettingsActivity` (promień, jednostki, endpoint) | ✅ kompletny |
| `OverheadActivity` („co nade mną?") | ✅ kompletny |
| `AircraftProvider` (ContentProvider, tylko odczyt) | ✅ kompletny |
| `BootReceiver` (restart po boocie) | ✅ kompletny |
| Zasoby: layouty, stringi, style, ikony wektorowe | ✅ kompletne |
| Testy jednostkowe parsera | ❌ do dodania |

Aplikacja ma komplet źródeł i zasobów potrzebnych do zbudowania APK przez
Soong. **Nie zbudowałem jej** — w sandboxie nie ma drzewa AOSP ani toolchainu
Androida, więc nie mogę potwierdzić, że `aapt2`/`kotlinc` przejdą bez błędów.
To pierwsza rzecz do sprawdzenia na maszynie budującej.
