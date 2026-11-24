// mbim_backend.cc — backend modemu MBIM (Mobile Broadband Interface Model).
//
// STATUS: szkielet. MBIM to standard USB-IF dla modemów LTE/5G —
// uniwersalny, w przeciwieństwie do QMI (Qualcomm-specific).
//
// Używany, gdy modem nie jest Qualcomma (np. MediaTek, Samsung Exynos,
// moduły Quectel w trybie MBIM).
//
// Mapowanie RIL → MBIM:
//   Dial      → MBIM_CID_MS_ACTIVATE / MBIM_CID_CALL_MANAGE
//   SMS       → MBIM_CID_SMS_SEND
//   DataCall  → MBIM_CID_CONNECT
//   Signal    → MBIM_CID_SIGNAL_STATE
//   SIM       → MBIM_CID_SUBSCRIBER_READY_STATUS / MBIM_CID_UICC_*

#include "qmi_backend.h"

#include <android-base/logging.h>

namespace aerophone {
namespace telephony {

MbimBackend::MbimBackend() = default;
MbimBackend::~MbimBackend() { Close(); }

bool MbimBackend::Open(const std::string& device) {
  device_ = device;
  // TODO: libmbim — mbim_device_open() + subskrypcja indications
  LOG(INFO) << "MbimBackend: otwieram " << device << " (szkielet — brak libmbim)";
  state_ = "mbim:closed";
  return true;
}

void MbimBackend::Close() { state_ = "mbim:closed"; }

Error::Code MbimBackend::Dial(const DialRequest& req, int32_t* call_id) {
  *call_id = -1;
  return Error::NOT_SUPPORTED;
}

Error::Code MbimBackend::Answer(const AnswerRequest& req) { return Error::NOT_SUPPORTED; }
Error::Code MbimBackend::Hangup(const HangupRequest& req) { return Error::NOT_SUPPORTED; }

Error::Code MbimBackend::SendSms(const SendSmsRequest& req, int32_t* msg_ref) {
  *msg_ref = -1;
  return Error::NOT_SUPPORTED;
}

Error::Code MbimBackend::SendUssd(const SendUssdRequest& req, std::string* resp) {
  resp->clear();
  return Error::NOT_SUPPORTED;
}

Error::Code MbimBackend::SetRadioState(const SetRadioStateRequest& req) {
  return Error::NOT_SUPPORTED;
}

Error::Code MbimBackend::DataCall(const DataCallRequest& req) { return Error::NOT_SUPPORTED; }

Error::Code MbimBackend::SimOp(const SimRequest& req, std::string* out) {
  out->clear();
  return Error::NOT_SUPPORTED;
}

Error::Code MbimBackend::QuerySignal(const SignalQuery& req, SignalUpdateEvent* out) {
  return Error::NOT_SUPPORTED;
}

Error::Code MbimBackend::ScanNetworks(const NetworkScanRequest& req,
                                      std::vector<std::string>* out) {
  out->clear();
  return Error::NOT_SUPPORTED;
}

void MbimBackend::SetEventCallback(EventCallback cb) { event_cb_ = std::move(cb); }

std::string MbimBackend::StateString() const { return state_; }

// ---------------------------------------------------------------------------
// Fabryka backendów
// ---------------------------------------------------------------------------
std::unique_ptr<ModemBackend> CreateBackend(const std::string& name) {
  if (name == "qmi") return std::make_unique<QmiBackend>();
  if (name == "mbim") return std::make_unique<MbimBackend>();
  return nullptr;
}

}  // namespace telephony
}  // namespace aerophone
