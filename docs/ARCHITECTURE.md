# Architektura Ninetails Mobile — mapowanie na warstwy L0–L8

Ten dokument wiąże kod w repozytorium z architekturą warstwową opisaną
w dokumencie projektu (warstwy L0–L8, Chromium OS jako host + Android w ARCVM).

## Mapa warstw → katalogi

| Warstwa | Odpowiedzialność | Gdzie w repo |
|---|---|---|
| **L0** Sprzęt | SoC ARM64, modem, PMIC, TPM | `device/aerophone/BoardConfig.mk` |
| **L1** Firmware/rozruch | bootloader, vboot, A/B firmware | `build/make-image.sh` (vbmeta), `keys/` |
| **L2** Kernel hosta | dm-verity, sterowniki, KVM | `build/build-host.sh` (kernel), `device/aerophone/` |
| **L3** Wirtualizacja | crosvm, concierge, cicerone, virtio | `device/aerophone/BoardConfig.mk` (ARCVM), upstream |
| **L4** Usługi hosta | systemd, D-Bus, shill, powerd, update_engine, **rilbridged** | `telephony/host/` |
| **L5** Powłoka/UI | Chrome/Ash, Ozone, launcher | upstream + overlay |
| **L6** Android w ARCVM | framework, system_server, ART, **HAL radiowy** | `telephony/guest/`, `apps/AeroRadar/` |
| **L7** Mosty | powiadomienia, schowek, tożsamość, **most RIL** | `telephony/proto/` |
| **L8** Aplikacje | AeroRadar, PWA, systemowe | `apps/AeroRadar/` |

## Przepływ: połączenie przychodzące

```
L0  Modem odbiera połączenie (sieć GSM/LTE)
     │  QMI indication
L2  Kernel hosta → /dev/qmi0
     │
L4  rilbridged (host) czyta QMI, buduje IncomingCallEvent
     │  protobuf na virtio-vsock (CID 3, port 5000)
L3  crosvm przekazuje ramkę do gościa
     │
L6  RadioBridge (HAL w ARCVM) odbiera, woła onIncomingCall
     │  Binder
L6  TelephonyRegistry → InCallService
     │
L7  Most powiadomień: ARC → Ash
     │
L5  Powłoka pokazuje ekran połączenia
```

**Kluczowe:** połączenie alarmowe nie może zależeć od stanu ARCVM.
Dlatego `rilbridged` działa na hoście i ma własną ścieżkę alarmową —
jeśli ARCVM padnie, host nadal może wybrać 112.

## Przepływ: uruchomienie aplikacji Android

```
L5  Użytkownik klika ikonę AeroRadar
     │
L4  concierge (host) odbiera żądanie uruchomienia
     │
L3  crosvm: ARCVM już działa (albo startuje, ~3 s)
     │
L6  Android: ActivityManager → RadarActivity
     │
L7  Potok graficzny: SurfaceFlinger → virtio-wayland → Ash
     │
L5  Okno aplikacji na ekranie
```

## Przepływ: aktualizacja OTA

```
L4  update_engine (host) pobiera payload
     │
L1  Zapis do nieaktywnego slotu (B)
     │  weryfikacja podpisu + sum
L1  Przełączenie slotu przy restarcie
     │
L2  Nowy kernel + rootfs
     │
L3  Nowy obraz ARCVM (w tym samym payloadzie!)
     │
L6  Android startuje z nowego obrazu
```

**Dlaczego ARCVM w tym samym payloadzie:** host i gość mają wspólny kontrakt
(protokół rilbridge, wersja AIDL). Rozjazd wersji = most RIL przestaje działać.
Dlatego `AB_OTA_PARTITIONS` w `BoardConfig.mk` zawiera `arcvm`.

## Decyzje projektowe i ich uzasadnienie

### Dlaczego Chromium OS jako host, a nie AOSP?

| Kryterium | Chromium OS host | AOSP host |
|---|---|---|
| Verified boot | gotowy (vboot) | trzeba zbudować |
| Aktualizacje A/B | gotowy (update_engine) | trzeba zbudować |
| Sandbox aplikacji | gotowy (minijail) | gotowy (SELinux) |
| Powłoka desktop/tablet | gotowa (Ash) | trzeba zbudować |
| Telefonia | **brak — budujemy** | gotowa (RIL) |
| Aplikacje Android | przez ARCVM | natywnie |

Bilans: Chromium OS daje 4 z 6 rzeczy gotowych, AOSP daje 2 z 6.
Telefonia to jedyna rzecz, którą trzeba zbudować od zera — i to jest
dokładnie zakres `telephony/`.

### Dlaczego protobuf na vsock, a nie Binder?

- Binder nie przechodzi granicy VM bez warstwy proxy (binderfs + kontekst)
- protobuf daje wersjonowanie schematu (pole `protocol_version`)
- vsock jest natywny dla crosvm — zero konfiguracji sieci w gościu
- Koszt: ~50 µs narzutu na żądanie (vsock + serializacja) — akceptowalne
  dla telefonii (latencja sieci to i tak 20–100 ms)

### Dlaczego modem zostaje na hoście?

Trzy powody:
1. **Połączenia alarmowe** muszą działać niezależnie od ARCVM
2. **update_engine** potrzebuje danych komórkowych do pobrania OTA
3. **Zarządzanie energią** — `powerd` musi widzieć stan modemu, żeby
   decydować o uśpieniu SoC

Passthrough modemu do VM wyklucza wszystkie trzy.

## Czego brakuje (uczciwie)

To szkielet, nie produkt. Poniżej realna lista braków:

### Krytyczne (bez tego nie ma telefonu)

1. **Backend QMI** — `qmi_backend.cc` to stub. Trzeba zaimplementować
   mapowanie każdego żądania na QMI (libqmi lub własny klient).
2. **IMS / VoLTE / VoWiFi** — najtrudniejsza część. Wymaga stosu IMS
   (np. oparty na `libims` albo komercyjny) i konfiguracji operatora.
3. **eSIM / LPA** — `SimRequest` ma operacje, ale backend ich nie realizuje.
   Potrzebny LPA (Local Profile Assistant) i GSMA SGP.22.
4. **Zasoby AeroRadar** — layouty, stringi, ikony. Bez nich APK się nie zbuduje.

### Ważne (bez tego produkt jest słaby)

5. **`RadarView`** — rysowanie radaru na Canvas.
6. **`AircraftDetailActivity`** — ekran Aero ID.
7. **`AircraftProvider`** — implementacja ContentProvider.
8. **Testy** — parser ADS-B, CPR, mapowanie RIL.
9. **SELinux policy** — `device/aerophone/sepolicy/` jest pusty; trzeba
   napisać reguły dla `rilbridged` i `RadioBridge`.

### Do produkcji

10. **Klucze w HSM** — devkit używa kluczy w plikach.
11. **Zgodność GMS/Play** — CDD/CTS, umowa z Google (jeśli GMS).
12. **Certyfikacja operatorów** — każdy operator ma własne wymagania IMS.
13. **Optymalizacja energii** — profil zasilania dla trybu czuwania z radarem.

## Następne kroki (kolejność)

1. Zasoby AeroRadar → pierwszy działający APK
2. Backend QMI → pierwsze połączenie/SMS na devkicie
3. SELinux policy → build przechodzi w trybie enforcing
4. Testy parsera → pewność, że Aero ID nie kłamie
5. IMS → VoLTE (najdłuższy etap, licz w miesiącach)
