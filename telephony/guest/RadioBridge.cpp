// RadioBridge.cpp — implementacja HAL radiowego w ARCVM (gość).
//
// Rola: tłumaczy wywołania AIDL (od frameworku Androida) na Envelope
// protokołu rilbridge i wysyła je przez vsock do rilbridged na hoście.
// Zdarzenia z hosta odbiera w osobnym wątku i rozgłasza do callbacków.
//
// Ścieżka w AOSP: hardware/aerophone/telephony/RadioBridge.cpp

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <arpa/inet.h>   // htonl / ntohl
#include <linux/vm_sockets.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "aidl/android/hardware/radio/aerophone/BnRadioBridge.h"
#include "rilbridge.pb.h"

namespace aidl::android::hardware::radio::aerophone {

using ::aerophone::rilbridge::Envelope;

class RadioBridge : public BnRadioBridge {
 public:
  RadioBridge() = default;
  ~RadioBridge() override { Disconnect(); }

  // -------------------------------------------------------------------------
  // Inicjalizacja połączenia z hostem
  // -------------------------------------------------------------------------
  bool Connect(uint32_t cid = 3, uint32_t port = 5000) {
    fd_ = ::socket(AF_VSOCK, SOCK_STREAM, 0);
    if (fd_ < 0) {
      LOG(ERROR) << "socket(AF_VSOCK) nie powiodło się";
      return false;
    }

    sockaddr_vm addr{};
    addr.svm_family = AF_VSOCK;
    addr.svm_cid = cid;          // CID hosta (3 = domyślny w crosvm)
    addr.svm_port = port;

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      LOG(ERROR) << "connect do vsock " << cid << ":" << port << " nie powiodło się";
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    running_.store(true);
    reader_thread_ = std::thread([this] { ReaderLoop(); });
    heartbeat_thread_ = std::thread([this] { HeartbeatLoop(); });

    LOG(INFO) << "RadioBridge podłączony do hosta (vsock " << cid << ":" << port << ")";
    return true;
  }

  void Disconnect() {
    running_.store(false);
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
    if (reader_thread_.joinable()) reader_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
  }

  // =========================================================================
  // IRadioBridge — połączenia
  // =========================================================================
  ndk::ScopedAStatus dial(const std::string& address, bool emergency, int32_t slotId,
                          int32_t* _aidl_return) override {
    Envelope env = NewEnvelope();
    auto* d = env.mutable_dial();
    d->set_address(address);
    d->set_emergency(emergency);
    d->set_slot_id(slotId);

    Envelope reply;
    if (!Transact(env, &reply)) {
      *_aidl_return = -1;
      return ndk::ScopedAStatus::fromServiceSpecificError(1);
    }
    if (reply.has_error()) {
      *_aidl_return = -1;
      return ndk::ScopedAStatus::fromServiceSpecificError(reply.error().code());
    }
    *_aidl_return = std::stoi(reply.response().data());
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus answer(int32_t callId) override {
    Envelope env = NewEnvelope();
    env.mutable_answer()->set_call_id(callId);
    return SimpleTransact(env);
  }

  ndk::ScopedAStatus hangup(int32_t callId, int32_t reason) override {
    Envelope env = NewEnvelope();
    auto* h = env.mutable_hangup();
    h->set_call_id(callId);
    h->set_reason(reason);
    return SimpleTransact(env);
  }

  ndk::ScopedAStatus getCallState(int32_t callId, CallState* _aidl_return) override {
    // Stan połączenia trzymamy lokalnie (aktualizowany zdarzeniami z hosta)
    std::lock_guard<std::mutex> lk(state_mu_);
    auto it = call_states_.find(callId);
    *_aidl_return = (it != call_states_.end()) ? it->second : CallState::DISCONNECTED;
    return ndk::ScopedAStatus::ok();
  }

  // =========================================================================
  // SMS / USSD
  // =========================================================================
  ndk::ScopedAStatus sendSms(const std::string& address, const std::vector<uint8_t>& pdu,
                             const std::string& smsc, int32_t slotId,
                             int32_t* _aidl_return) override {
    Envelope env = NewEnvelope();
    auto* s = env.mutable_send_sms();
    s->set_address(address);
    s->set_pdu(std::string(pdu.begin(), pdu.end()));
    s->set_smsc(smsc);
    s->set_slot_id(slotId);

    Envelope reply;
    if (!Transact(env, &reply) || reply.has_error()) {
      *_aidl_return = -1;
      return ndk::ScopedAStatus::fromServiceSpecificError(
          reply.has_error() ? reply.error().code() : 1);
    }
    *_aidl_return = std::stoi(reply.response().data());
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus sendUssd(const std::string& ussd, int32_t slotId,
                              std::string* _aidl_return) override {
    Envelope env = NewEnvelope();
    auto* u = env.mutable_send_ussd();
    u->set_ussd(ussd);
    u->set_slot_id(slotId);

    Envelope reply;
    if (!Transact(env, &reply) || reply.has_error()) {
      *_aidl_return = "";
      return ndk::ScopedAStatus::fromServiceSpecificError(
          reply.has_error() ? reply.error().code() : 1);
    }
    *_aidl_return = reply.response().data();
    return ndk::ScopedAStatus::ok();
  }

  // =========================================================================
  // Radio
  // =========================================================================
  ndk::ScopedAStatus setRadioState(bool on) override {
    Envelope env = NewEnvelope();
    env.mutable_set_radio_state()->set_state(
        on ? ::aerophone::rilbridge::SetRadioStateRequest::ON
           : ::aerophone::rilbridge::SetRadioStateRequest::OFF);
    return SimpleTransact(env);
  }

  ndk::ScopedAStatus getRadioState(bool* _aidl_return) override {
    std::lock_guard<std::mutex> lk(state_mu_);
    *_aidl_return = radio_on_;
    return ndk::ScopedAStatus::ok();
  }

  // =========================================================================
  // Dane
  // =========================================================================
  ndk::ScopedAStatus setupDataCall(const std::string& apn, const std::string& apnType,
                                   const std::string& protocol, int32_t slotId) override {
    Envelope env = NewEnvelope();
    auto* d = env.mutable_data_call();
    d->set_action(::aerophone::rilbridge::DataCallRequest::SETUP);
    d->set_apn(apn);
    d->set_apn_type(apnType);
    d->set_protocol(protocol);
    d->set_slot_id(slotId);
    return SimpleTransact(env);
  }

  ndk::ScopedAStatus deactivateDataCall(const std::string& apnType) override {
    Envelope env = NewEnvelope();
    auto* d = env.mutable_data_call();
    d->set_action(::aerophone::rilbridge::DataCallRequest::DEACTIVATE);
    d->set_apn_type(apnType);
    return SimpleTransact(env);
  }

  // =========================================================================
  // SIM
  // =========================================================================
  ndk::ScopedAStatus getSimState(int32_t slotId, SimState* _aidl_return) override {
    std::lock_guard<std::mutex> lk(state_mu_);
    *_aidl_return = sim_state_;
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus getIccId(int32_t slotId, std::string* _aidl_return) override {
    return SimQuery(::aerophone::rilbridge::SimRequest::GET_ICCID, slotId, _aidl_return);
  }

  ndk::ScopedAStatus getImsi(int32_t slotId, std::string* _aidl_return) override {
    return SimQuery(::aerophone::rilbridge::SimRequest::GET_IMSI, slotId, _aidl_return);
  }

  ndk::ScopedAStatus getOperator(int32_t slotId, std::string* _aidl_return) override {
    return SimQuery(::aerophone::rilbridge::SimRequest::GET_OPERATOR, slotId, _aidl_return);
  }

  ndk::ScopedAStatus enterPin(int32_t slotId, const std::string& pin,
                              bool* _aidl_return) override {
    Envelope env = NewEnvelope();
    auto* s = env.mutable_sim();
    s->set_op(::aerophone::rilbridge::SimRequest::ENTER_PIN);
    s->set_slot_id(slotId);
    s->set_pin(pin);

    Envelope reply;
    if (!Transact(env, &reply) || reply.has_error()) {
      *_aidl_return = false;
      return ndk::ScopedAStatus::ok();
    }
    *_aidl_return = true;
    return ndk::ScopedAStatus::ok();
  }

  // =========================================================================
  // Sygnał
  // =========================================================================
  ndk::ScopedAStatus getSignalStrength(int32_t slotId,
                                       SignalStrength* _aidl_return) override {
    std::lock_guard<std::mutex> lk(state_mu_);
    *_aidl_return = signal_;
    return ndk::ScopedAStatus::ok();
  }

  // =========================================================================
  // Callbacki
  // =========================================================================
  ndk::ScopedAStatus registerCallback(
      const std::shared_ptr<IRadioBridgeCallback>& callback) override {
    std::lock_guard<std::mutex> lk(cb_mu_);
    callbacks_.push_back(callback);
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus unregisterCallback(
      const std::shared_ptr<IRadioBridgeCallback>& callback) override {
    std::lock_guard<std::mutex> lk(cb_mu_);
    callbacks_.erase(
        std::remove(callbacks_.begin(), callbacks_.end(), callback),
        callbacks_.end());
    return ndk::ScopedAStatus::ok();
  }

 private:
  // -------------------------------------------------------------------------
  // Pomocnicze
  // -------------------------------------------------------------------------
  Envelope NewEnvelope() {
    Envelope env;
    env.set_protocol_version(1);
    env.set_request_id(next_request_id_.fetch_add(1));
    env.set_client_id(0);
    env.set_timestamp_ms(NowMs());
    return env;
  }

  bool Transact(const Envelope& req, Envelope* reply) {
    std::lock_guard<std::mutex> lk(write_mu_);
    if (fd_ < 0) return false;

    std::string buf;
    if (!req.SerializeToString(&buf)) return false;

    uint32_t len = htonl(static_cast<uint32_t>(buf.size()));
    if (::send(fd_, &len, sizeof(len), MSG_NOSIGNAL) != sizeof(len)) return false;
    if (::send(fd_, buf.data(), buf.size(), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(buf.size())) return false;

    // Czekamy na odpowiedź o tym samym request_id (pomijamy zdarzenia)
    for (int i = 0; i < 100; ++i) {
      Envelope env;
      if (!ReadEnvelope(&env)) return false;
      if (env.request_id() == req.request_id() &&
          (env.has_response() || env.has_error())) {
        *reply = env;
        return true;
      }
      // To zdarzenie asynchroniczne — obsłuż i czytaj dalej
      DispatchEvent(env);
    }
    return false;
  }

  ndk::ScopedAStatus SimpleTransact(const Envelope& req) {
    Envelope reply;
    if (!Transact(req, &reply)) {
      return ndk::ScopedAStatus::fromServiceSpecificError(1);
    }
    if (reply.has_error()) {
      return ndk::ScopedAStatus::fromServiceSpecificError(reply.error().code());
    }
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus SimQuery(::aerophone::rilbridge::SimRequest::Op op, int32_t slotId,
                              std::string* out) {
    Envelope env = NewEnvelope();
    auto* s = env.mutable_sim();
    s->set_op(op);
    s->set_slot_id(slotId);

    Envelope reply;
    if (!Transact(env, &reply) || reply.has_error()) {
      *out = "";
      return ndk::ScopedAStatus::fromServiceSpecificError(
          reply.has_error() ? reply.error().code() : 1);
    }
    *out = reply.response().data();
    return ndk::ScopedAStatus::ok();
  }

  bool ReadEnvelope(Envelope* env) {
    uint32_t len_be = 0;
    if (!ReadAll(&len_be, sizeof(len_be))) return false;
    uint32_t len = ntohl(len_be);
    if (len == 0 || len > (1u << 20)) return false;
    std::string buf(len, '\0');
    if (!ReadAll(buf.data(), len)) return false;
    return env->ParseFromString(buf);
  }

  bool ReadAll(void* dst, size_t n) {
    size_t got = 0;
    auto* p = static_cast<char*>(dst);
    while (got < n) {
      ssize_t r = ::recv(fd_, p + got, n - got, 0);
      if (r <= 0) return false;
      got += static_cast<size_t>(r);
    }
    return true;
  }

  // -------------------------------------------------------------------------
  // Wątek czytający: zdarzenia z hosta
  // -------------------------------------------------------------------------
  void ReaderLoop() {
    while (running_.load()) {
      Envelope env;
      if (!ReadEnvelope(&env)) break;
      DispatchEvent(env);
    }
    LOG(INFO) << "RadioBridge: połączenie z hostem zamknięte";
  }

  void DispatchEvent(const Envelope& env) {
    std::lock_guard<std::mutex> lk(cb_mu_);
    for (auto& cb : callbacks_) {
      switch (env.payload_case()) {
        case Envelope::kIncomingCall: {
          const auto& e = env.incoming_call();
          cb->onIncomingCall(e.call_id(), e.address(), e.display_name(),
                             e.emergency(), e.slot_id());
          break;
        }
        case Envelope::kCallState: {
          const auto& e = env.call_state();
          {
            std::lock_guard<std::mutex> sl(state_mu_);
            call_states_[e.call_id()] = MapCallState(e.state());
          }
          cb->onCallStateChanged(e.call_id(), MapCallState(e.state()), e.cause());
          break;
        }
        case Envelope::kSmsReceived: {
          const auto& e = env.sms_received();
          std::vector<uint8_t> pdu(e.pdu().begin(), e.pdu().end());
          cb->onSmsReceived(e.address(), pdu, e.smsc(), e.timestamp_ms());
          break;
        }
        case Envelope::kUssdResponse: {
          const auto& e = env.ussd_response();
          cb->onUssdResponse(e.response(), e.status());
          break;
        }
        case Envelope::kSignalUpdate: {
          const auto& e = env.signal_update();
          SignalStrength s;
          s.level = e.level();
          s.rsrpDbm = e.rsrp_dbm();
          s.networkType = e.network_type();
          s.operatorName = e.operator_name();
          s.roaming = e.roaming();
          {
            std::lock_guard<std::mutex> sl(state_mu_);
            signal_ = s;
          }
          cb->onSignalStrengthChanged(s);
          break;
        }
        case Envelope::kDataCallState: {
          const auto& e = env.data_call_state();
          cb->onDataCallStateChanged(e.state(), e.apn_type(), e.iface(), e.ip_address());
          break;
        }
        case Envelope::kSimState: {
          const auto& e = env.sim_state();
          {
            std::lock_guard<std::mutex> sl(state_mu_);
            sim_state_ = MapSimState(e.state());
          }
          cb->onSimStateChanged(MapSimState(e.state()), e.slot_id());
          break;
        }
        case Envelope::kModemReset: {
          cb->onModemReset(env.modem_reset().reason());
          break;
        }
        default:
          break;
      }
    }
  }

  static CallState MapCallState(::aerophone::rilbridge::CallStateEvent::State s) {
    using S = ::aerophone::rilbridge::CallStateEvent::State;
    switch (s) {
      case S::ACTIVE: return CallState::ACTIVE;
      case S::HOLDING: return CallState::HOLDING;
      case S::DIALING: return CallState::DIALING;
      case S::ALERTING: return CallState::ALERTING;
      case S::INCOMING: return CallState::INCOMING;
      case S::WAITING: return CallState::WAITING;
      default: return CallState::DISCONNECTED;
    }
  }

  static SimState MapSimState(::aerophone::rilbridge::SimStateEvent::State s) {
    using S = ::aerophone::rilbridge::SimStateEvent::State;
    switch (s) {
      case S::ABSENT: return SimState::ABSENT;
      case S::PIN_REQUIRED: return SimState::PIN_REQUIRED;
      case S::PUK_REQUIRED: return SimState::PUK_REQUIRED;
      case S::READY: return SimState::READY;
      default: return SimState::ERROR;
    }
  }

  // -------------------------------------------------------------------------
  // Heartbeat do hosta
  // -------------------------------------------------------------------------
  void HeartbeatLoop() {
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(10));
      if (fd_ < 0) break;
      Envelope env = NewEnvelope();
      env.mutable_heartbeat()->set_uptime_ms(NowMs());
      std::lock_guard<std::mutex> lk(write_mu_);
      std::string buf;
      if (!env.SerializeToString(&buf)) continue;
      uint32_t len = htonl(static_cast<uint32_t>(buf.size()));
      ::send(fd_, &len, sizeof(len), MSG_NOSIGNAL);
      ::send(fd_, buf.data(), buf.size(), MSG_NOSIGNAL);
    }
  }

  static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  int fd_ = -1;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> next_request_id_{1};
  std::mutex write_mu_;
  std::mutex state_mu_;
  std::mutex cb_mu_;
  std::thread reader_thread_;
  std::thread heartbeat_thread_;
  std::vector<std::shared_ptr<IRadioBridgeCallback>> callbacks_;
  std::unordered_map<int32_t, CallState> call_states_;
  SignalStrength signal_;
  SimState sim_state_ = SimState::ABSENT;
  bool radio_on_ = false;
};

}  // namespace aidl::android::hardware::radio::aerophone

// ---------------------------------------------------------------------------
// main — rejestracja usługi w servicemanager
// ---------------------------------------------------------------------------
int main() {
  using namespace aidl::android::hardware::radio::aerophone;

  ABinderProcess_setThreadPoolMaxThreadCount(4);
  ABinderProcess_startThreadPool();

  auto bridge = ndk::SharedRefBase::make<RadioBridge>();
  if (!bridge->Connect(3, 5000)) {
    LOG(ERROR) << "nie mogę połączyć się z rilbridged na hoście";
    return 1;
  }

  const std::string instance = std::string() + RadioBridge::descriptor + "/default";
  binder_status_t status =
      AServiceManager_addService(bridge->asBinder().get(), instance.c_str());
  if (status != STATUS_OK) {
    LOG(ERROR) << "addService nie powiodło się: " << status;
    return 1;
  }

  LOG(INFO) << "RadioBridge zarejestrowany jako " << instance;
  ABinderProcess_joinThreadPool();
  return 0;
}
