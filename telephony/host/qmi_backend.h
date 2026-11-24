// qmi_backend.h — deklaracja backendu QMI.
//
// Wydzielone z rilbridged.cc, żeby backendy (QMI, MBIM) były wymienne
// i testowalne bez modemu (mock w testach).

#ifndef AEROPHONE_TELEPHONY_QMI_BACKEND_H_
#define AEROPHONE_TELEPHONY_QMI_BACKEND_H_

#include <cstdint>
#include <functional>   // std::function (EventCallback)
#include <memory>
#include <string>
#include <vector>

#include "rilbridge.pb.h"

namespace aerophone {
namespace telephony {

// Typy protokołu (Envelope, Error, *Request, *Event) są generowane przez protoc
// w przestrzeni nazw aerophone::rilbridge (zgodnie z `package aerophone.rilbridge;`
// w rilbridge.proto). Wciągamy je do tej przestrzeni, żeby deklaracje backendów
// i ich implementacje nie musiały kwalifikować każdego typu osobno.
using namespace ::aerophone::rilbridge;

// Interfejs backendu — wspólny dla QMI i MBIM.
class ModemBackend {
 public:
  virtual ~ModemBackend() = default;

  virtual bool Open(const std::string& device) = 0;
  virtual void Close() = 0;

  virtual Error::Code Dial(const DialRequest& req, int32_t* call_id) = 0;
  virtual Error::Code Answer(const AnswerRequest& req) = 0;
  virtual Error::Code Hangup(const HangupRequest& req) = 0;
  virtual Error::Code SendSms(const SendSmsRequest& req, int32_t* msg_ref) = 0;
  virtual Error::Code SendUssd(const SendUssdRequest& req, std::string* resp) = 0;
  virtual Error::Code SetRadioState(const SetRadioStateRequest& req) = 0;
  virtual Error::Code DataCall(const DataCallRequest& req) = 0;
  virtual Error::Code SimOp(const SimRequest& req, std::string* out) = 0;
  virtual Error::Code QuerySignal(const SignalQuery& req, SignalUpdateEvent* out) = 0;
  virtual Error::Code ScanNetworks(const NetworkScanRequest& req,
                                   std::vector<std::string>* out) = 0;

  using EventCallback = std::function<void(const Envelope&)>;
  virtual void SetEventCallback(EventCallback cb) = 0;

  virtual std::string StateString() const = 0;
};

// Backend QMI (Qualcomm).
class QmiBackend : public ModemBackend {
 public:
  QmiBackend();
  ~QmiBackend() override;

  bool Open(const std::string& device) override;
  void Close() override;

  Error::Code Dial(const DialRequest& req, int32_t* call_id) override;
  Error::Code Answer(const AnswerRequest& req) override;
  Error::Code Hangup(const HangupRequest& req) override;
  Error::Code SendSms(const SendSmsRequest& req, int32_t* msg_ref) override;
  Error::Code SendUssd(const SendUssdRequest& req, std::string* resp) override;
  Error::Code SetRadioState(const SetRadioStateRequest& req) override;
  Error::Code DataCall(const DataCallRequest& req) override;
  Error::Code SimOp(const SimRequest& req, std::string* out) override;
  Error::Code QuerySignal(const SignalQuery& req, SignalUpdateEvent* out) override;
  Error::Code ScanNetworks(const NetworkScanRequest& req,
                           std::vector<std::string>* out) override;

  void SetEventCallback(EventCallback cb) override;
  std::string StateString() const override;

 private:
  std::string device_;
  std::string state_ = "qmi:init";
  EventCallback event_cb_;
};

// Backend MBIM (uniwersalny, dla modemów nie-Qualcomm).
class MbimBackend : public ModemBackend {
 public:
  MbimBackend();
  ~MbimBackend() override;

  bool Open(const std::string& device) override;
  void Close() override;

  Error::Code Dial(const DialRequest& req, int32_t* call_id) override;
  Error::Code Answer(const AnswerRequest& req) override;
  Error::Code Hangup(const HangupRequest& req) override;
  Error::Code SendSms(const SendSmsRequest& req, int32_t* msg_ref) override;
  Error::Code SendUssd(const SendUssdRequest& req, std::string* resp) override;
  Error::Code SetRadioState(const SetRadioStateRequest& req) override;
  Error::Code DataCall(const DataCallRequest& req) override;
  Error::Code SimOp(const SimRequest& req, std::string* out) override;
  Error::Code QuerySignal(const SignalQuery& req, SignalUpdateEvent* out) override;
  Error::Code ScanNetworks(const NetworkScanRequest& req,
                           std::vector<std::string>* out) override;

  void SetEventCallback(EventCallback cb) override;
  std::string StateString() const override;

 private:
  std::string device_;
  std::string state_ = "mbim:init";
  EventCallback event_cb_;
};

// Fabryka: wybiera backend po nazwie ("qmi" | "mbim").
std::unique_ptr<ModemBackend> CreateBackend(const std::string& name);

}  // namespace telephony
}  // namespace aerophone

#endif  // AEROPHONE_TELEPHONY_QMI_BACKEND_H_
