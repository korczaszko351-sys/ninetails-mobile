// IRadioBridge.aidl — interfejs HAL radiowego w ARCVM (gość).
//
// Framework telekomunikacyjny Androida (TelephonyRegistry, Phone) rozmawia
// z tym HAL-em przez standardowy interfejs android.hardware.radio.
// Nasz HAL tłumaczy wywołania na protokół rilbridge (vsock) do hosta.
//
// Ścieżka w AOSP: hardware/aerophone/telephony/aidl/.../IRadioBridge.aidl
// Wersja AIDL: android.hardware.radio 2.0 (Android 14)

package android.hardware.radio.aerophone;

import android.hardware.radio.aerophone.CallState;
import android.hardware.radio.aerophone.SimState;
import android.hardware.radio.aerophone.SignalStrength;

@VintfStability
interface IRadioBridge {
    // --- Połączenia ---------------------------------------------------------
    int dial(in String address, boolean emergency, int slotId);
    void answer(int callId);
    void hangup(int callId, int reason);
    CallState getCallState(int callId);

    // --- SMS / USSD ---------------------------------------------------------
    int sendSms(in String address, in byte[] pdu, in String smsc, int slotId);
    String sendUssd(in String ussd, int slotId);

    // --- Radio --------------------------------------------------------------
    void setRadioState(boolean on);
    boolean getRadioState();

    // --- Dane ---------------------------------------------------------------
    void setupDataCall(in String apn, in String apnType, in String protocol, int slotId);
    void deactivateDataCall(in String apnType);

    // --- SIM ----------------------------------------------------------------
    SimState getSimState(int slotId);
    String getIccId(int slotId);
    String getImsi(int slotId);
    String getOperator(int slotId);
    boolean enterPin(int slotId, in String pin);

    // --- Sygnał -------------------------------------------------------------
    SignalStrength getSignalStrength(int slotId);

    // --- Zdarzenia (host → gość) --------------------------------------------
    // Rejestracja callbacku: host wysyła zdarzenia asynchronicznie.
    void registerCallback(IRadioBridgeCallback callback);
    void unregisterCallback(IRadioBridgeCallback callback);
}
