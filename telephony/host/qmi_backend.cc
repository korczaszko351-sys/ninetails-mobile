// qmi_backend.cc — backend modemu QMI (Qualcomm MSM Interface).
//
// STATUS: szkielet. Każda metoda ma kontrakt i miejsce na implementację.
// Do zrobienia: podłączyć libqmi (QmiClient) albo własny klient QMUX.
//
// QMI to protokół binarny Qualcomma nad /dev/qmi0 (albo multiplexowany
// przez QMUX na /dev/smdcntl0). Wiadomości mają nagłówek TLV.
//
// Mapowanie RIL → QMI:
//   Dial        → QMI_VOICE_DIAL_CALL
//   Answer      → QMI_VOICE_ANSWER_CALL
//   Hangup      → QMI_VOICE_END_CALL
//   SendSms     → QMI_WMS_RAW_SEND
//   DataCall    → QMI_WDS_START_NETWORK_INTERFACE
//   Signal      → QMI_NAS_GET_SIGNAL_INFO / QMI_NAS_INDICATION_REGISTER
//   SIM         → QMI_UIM_READ_TRANSPARENT / QMI_UIM_GET_CARD_STATUS

#include "qmi_backend.h"

#include <android-base/logging.h>

namespace aerophone {
namespace telephony {

QmiBackend::QmiBackend() = default;
QmiBackend::~QmiBackend() { Close(); }

bool QmiBackend::Open(const std::string& device) {
  device_ = device;
  // TODO: qmi_client_init(device, QMI_SERVICE_VOICE, ...)
  //       + rejestracja indication handlerów dla VOICE/WMS/NAS/UIM
  LOG(INFO) << "QmiBackend: otwieram " << device << " (szkielet — brak libqmi)";
  state_ = "qmi:closed";
  return true;
}

void QmiBackend::Close() {
  // TODO: qmi_client_release()
  state_ = "qmi:closed";
}

Error::Code QmiBackend::Dial(const DialRequest& req, int32_t* call_id) {
  // TODO: QMI_VOICE_DIAL_CALL z numerem i typem (emergency → service type EMERGENCY)
  LOG(INFO) << "QmiBackend::Dial " << req.address()
            << (req.emergency() ? " [ALARMOWE]" : "");
  *call_id = -1;
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::Answer(const AnswerRequest& req) {
  // TODO: QMI_VOICE_ANSWER_CALL
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::Hangup(const HangupRequest& req) {
  // TODO: QMI_VOICE_END_CALL
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::SendSms(const SendSmsRequest& req, int32_t* msg_ref) {
  // TODO: QMI_WMS_RAW_SEND (PDU już zakodowany przez framework)
  *msg_ref = -1;
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::SendUssd(const SendUssdRequest& req, std::string* resp) {
  // TODO: QMI_VOICE_ASYNC_USSD
  resp->clear();
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::SetRadioState(const SetRadioStateRequest& req) {
  // TODO: QMI_DMS_SET_OPERATING_MODE
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::DataCall(const DataCallRequest& req) {
  // TODO: QMI_WDS_START_NETWORK_INTERFACE / STOP_NETWORK_INTERFACE
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::SimOp(const SimRequest& req, std::string* out) {
  // TODO: QMI_UIM_* (GET_CARD_STATUS, READ_TRANSPARENT, VERIFY_PIN)
  //       eSIM: QMI_UIM_* + LPA (osobny demon)
  out->clear();
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::QuerySignal(const SignalQuery& req, SignalUpdateEvent* out) {
  // TODO: QMI_NAS_GET_SIGNAL_INFO → mapowanie na RSSI/RSRP/RSRQ/SINR
  return Error::NOT_SUPPORTED;
}

Error::Code QmiBackend::ScanNetworks(const NetworkScanRequest& req,
                                     std::vector<std::string>* out) {
  // TODO: QMI_NAS_NETWORK_SCAN
  out->clear();
  return Error::NOT_SUPPORTED;
}

void QmiBackend::SetEventCallback(EventCallback cb) {
  event_cb_ = std::move(cb);
}

std::string QmiBackend::StateString() const { return state_; }

}  // namespace telephony
}  // namespace aerophone
