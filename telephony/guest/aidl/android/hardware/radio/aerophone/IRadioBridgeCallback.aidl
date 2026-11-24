// IRadioBridgeCallback.aidl — zdarzenia z hosta do gościa.
//
// Host (rilbridged) wysyła te zdarzenia przez vsock; HAL w gościu
// przekazuje je do frameworku telekomunikacyjnego Androida.

package android.hardware.radio.aerophone;

import android.hardware.radio.aerophone.CallState;
import android.hardware.radio.aerophone.SimState;
import android.hardware.radio.aerophone.SignalStrength;

@VintfStability
oneway interface IRadioBridgeCallback {
    void onIncomingCall(int callId, in String address, in String displayName,
                        boolean emergency, int slotId);
    void onCallStateChanged(int callId, in CallState state, int cause);
    void onSmsReceived(in String address, in byte[] pdu, in String smsc, long timestampMs);
    void onUssdResponse(in String response, int status);
    void onSignalStrengthChanged(in SignalStrength strength);
    void onDataCallStateChanged(int state, in String apnType, in String iface, in String ip);
    void onSimStateChanged(in SimState state, int slotId);
    void onModemReset(int reason);
}
