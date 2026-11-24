// rilbridged — demon telefonii hosta (L4).
//
// Rola: jest jedynym właścicielem modemu po stronie hosta. Otwiera urządzenie
// QMI/MBIM, tłumaczy żądania z protokołu rilbridge (vsock) na wywołania QMI,
// i rozgłasza zdarzenia modemu do podłączonych klientów (ARCVM).
//
// Dlaczego demon, a nie biblioteka w gościu:
//   - modem jest zasobem hosta; gość nie może go otworzyć bez passthrough
//   - host potrzebuje stanu modemu niezależnie od VM (połączenia alarmowe,
//     dane dla update_engine, telemetria)
//   - restart ARCVM nie może zrywać połączenia alarmowego
//
// Wątki:
//   main        — akceptuje połączenia vsock, czyta Envelope
//   qmi_reader  — czyta asynchronicznie z /dev/qmi0, mapuje na zdarzenia
//   watchdog    — pilnuje heartbeatów od gościa i stanu modemu
//
// Budowanie: patrz telephony/host/Android.bp (Soong) — moduł cc_binary "rilbridged"

#include <arpa/inet.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "qmi_backend.h"
#include "rilbridge.pb.h"

namespace aerophone {
namespace telephony {

// ---------------------------------------------------------------------------
// Konfiguracja
// ---------------------------------------------------------------------------
struct Config {
  uint32_t vsock_port = 5000;          // BOARD_RILBRIDGE_VSOCK_PORT
  uint32_t max_clients = 4;            // BOARD_RILBRIDGE_MAX_CLIENTS
  std::string modem_device = "/dev/qmi0";
  std::string backend = "qmi";         // "qmi" | "mbim"
  int heartbeat_timeout_ms = 30000;
  int emergency_retry_ms = 2000;
};

// ModemBackend (interfejs + QmiBackend/MbimBackend) jest w qmi_backend.h —
// wydzielony, żeby backendy były wymienne i testowalne bez modemu.

// ---------------------------------------------------------------------------
// Sesja klienta (jedno połączenie vsock z ARCVM)
// ---------------------------------------------------------------------------
class ClientSession {
 public:
  ClientSession(int fd, uint32_t client_id) : fd_(fd), client_id_(client_id) {}
  ~ClientSession() { if (fd_ >= 0) ::close(fd_); }

  int fd() const { return fd_; }
  uint32_t client_id() const { return client_id_; }

  bool Send(const Envelope& env) {
    std::string buf;
    if (!env.SerializeToString(&buf)) return false;
    // Ramka: 4 bajty długości (BE) + payload
    uint32_t len = htonl(static_cast<uint32_t>(buf.size()));
    std::lock_guard<std::mutex> lk(write_mu_);
    if (::send(fd_, &len, sizeof(len), MSG_NOSIGNAL) != sizeof(len)) return false;
    size_t sent = 0;
    while (sent < buf.size()) {
      ssize_t n = ::send(fd_, buf.data() + sent, buf.size() - sent, MSG_NOSIGNAL);
      if (n <= 0) return false;
      sent += static_cast<size_t>(n);
    }
    return true;
  }

  bool Receive(Envelope* env) {
    uint32_t len_be = 0;
    if (!ReadAll(&len_be, sizeof(len_be))) return false;
    uint32_t len = ntohl(len_be);
    if (len == 0 || len > kMaxFrame) return false;
    std::string buf(len, '\0');
    if (!ReadAll(buf.data(), len)) return false;
    return env->ParseFromString(buf);
  }

  void TouchHeartbeat() {
    last_heartbeat_ms_.store(NowMs());
  }
  int64_t LastHeartbeatMs() const { return last_heartbeat_ms_.load(); }

 private:
  static constexpr uint32_t kMaxFrame = 1u << 20;  // 1 MiB

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

  static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  int fd_;
  uint32_t client_id_;
  std::mutex write_mu_;
  std::atomic<int64_t> last_heartbeat_ms_{0};
};

// ---------------------------------------------------------------------------
// RilBridge — rdzeń: przyjmuje klientów, dispatchuje żądania, rozgłasza zdarzenia
// ---------------------------------------------------------------------------
class RilBridge {
 public:
  RilBridge(Config cfg, std::unique_ptr<ModemBackend> backend)
      : cfg_(std::move(cfg)), backend_(std::move(backend)) {}

  bool Start() {
    if (!backend_->Open(cfg_.modem_device)) {
      Log("nie mogę otworzyć modemu: " + cfg_.modem_device);
      return false;
    }

    // Backend woła nas przy każdej zmianie stanu modemu → rozgłaszamy do klientów
    backend_->SetEventCallback([this](const Envelope& env) { Broadcast(env); });

    listen_fd_ = ::socket(AF_VSOCK, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      Log("socket(AF_VSOCK) nie powiodło się");
      return false;
    }

    sockaddr_vm addr{};
    addr.svm_family = AF_VSOCK;
    addr.svm_cid = VMADDR_CID_ANY;      // nasłuchuj na wszystkich CID
    addr.svm_port = cfg_.vsock_port;

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      Log("bind na vsock:" + std::to_string(cfg_.vsock_port) + " nie powiodło się");
      return false;
    }
    if (::listen(listen_fd_, static_cast<int>(cfg_.max_clients)) < 0) {
      Log("listen nie powiodło się");
      return false;
    }

    running_.store(true);
    accept_thread_ = std::thread([this] { AcceptLoop(); });
    watchdog_thread_ = std::thread([this] { WatchdogLoop(); });

    Log("rilbridged wystartował na vsock:" + std::to_string(cfg_.vsock_port) +
        " (backend=" + cfg_.backend + ")");
    return true;
  }

  void Stop() {
    running_.store(false);
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    if (watchdog_thread_.joinable()) watchdog_thread_.join();
    backend_->Close();
    Log("rilbridged zatrzymany");
  }

 private:
  // -------------------------------------------------------------------------
  // Pętla akceptacji klientów
  // -------------------------------------------------------------------------
  void AcceptLoop() {
    while (running_.load()) {
      sockaddr_vm peer{};
      socklen_t peer_len = sizeof(peer);
      int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
      if (fd < 0) {
        if (!running_.load()) break;
        continue;
      }

      uint32_t cid = next_client_id_.fetch_add(1);
      if (cid >= cfg_.max_clients) {
        Log("odrzucam klienta: przekroczono limit " + std::to_string(cfg_.max_clients));
        ::close(fd);
        continue;
      }

      auto session = std::make_shared<ClientSession>(fd, cid);
      session->TouchHeartbeat();
      {
        std::lock_guard<std::mutex> lk(clients_mu_);
        clients_[cid] = session;
      }
      Log("klient podłączony: id=" + std::to_string(cid) +
          " cid=" + std::to_string(peer.svm_cid));

      std::thread([this, session] { ClientLoop(session); }).detach();
    }
  }

  // -------------------------------------------------------------------------
  // Pętla pojedynczego klienta
  // -------------------------------------------------------------------------
  void ClientLoop(std::shared_ptr<ClientSession> session) {
    Envelope env;
    while (running_.load() && session->Receive(&env)) {
      session->TouchHeartbeat();
      HandleRequest(session, env);
    }
    {
      std::lock_guard<std::mutex> lk(clients_mu_);
      clients_.erase(session->client_id());
    }
    Log("klient rozłączony: id=" + std::to_string(session->client_id()));
  }

  // -------------------------------------------------------------------------
  // Dispatch żądań
  // -------------------------------------------------------------------------
  void HandleRequest(std::shared_ptr<ClientSession> session, const Envelope& env) {
    Envelope reply;
    reply.set_protocol_version(1);
    reply.set_request_id(env.request_id());
    reply.set_client_id(session->client_id());
    reply.set_timestamp_ms(NowMs());

    Error::Code code = Error::OK;
    std::string detail;

    switch (env.payload_case()) {
      case Envelope::kDial: {
        int32_t call_id = -1;
        code = backend_->Dial(env.dial(), &call_id);
        if (code == Error::OK) detail = std::to_string(call_id);
        break;
      }
      case Envelope::kAnswer:
        code = backend_->Answer(env.answer());
        break;
      case Envelope::kHangup:
        code = backend_->Hangup(env.hangup());
        break;
      case Envelope::kSendSms: {
        int32_t ref = -1;
        code = backend_->SendSms(env.send_sms(), &ref);
        if (code == Error::OK) detail = std::to_string(ref);
        break;
      }
      case Envelope::kSendUssd:
        code = backend_->SendUssd(env.send_ussd(), &detail);
        break;
      case Envelope::kSetRadioState:
        code = backend_->SetRadioState(env.set_radio_state());
        break;
      case Envelope::kDataCall:
        code = backend_->DataCall(env.data_call());
        break;
      case Envelope::kSim:
        code = backend_->SimOp(env.sim(), &detail);
        break;
      case Envelope::kSignalQuery: {
        SignalUpdateEvent sig;
        code = backend_->QuerySignal(env.signal_query(), &sig);
        if (code == Error::OK) sig.SerializeToString(&detail);
        break;
      }
      case Envelope::kNetworkScan: {
        std::vector<std::string> nets;
        code = backend_->ScanNetworks(env.network_scan(), &nets);
        for (const auto& n : nets) { detail += n; detail += '\n'; }
        break;
      }
      case Envelope::kHeartbeat:
        // heartbeat od gościa — nic do zrobienia, TouchHeartbeat już zadziałał
        return;
      default:
        code = Error::NOT_SUPPORTED;
        detail = "nieznany typ żądania";
        break;
    }

    if (code == Error::OK) {
      auto* resp = reply.mutable_response();
      resp->set_request_id(env.request_id());
      resp->set_success(true);
      resp->set_data(detail);
    } else {
      auto* err = reply.mutable_error();
      err->set_request_id(env.request_id());
      err->set_code(code);
      err->set_message(detail.empty() ? ErrorCodeName(code) : detail);
      err->set_retryable(code == Error::MODEM_BUSY || code == Error::TIMEOUT);
    }

    session->Send(reply);
  }

  // -------------------------------------------------------------------------
  // Rozgłaszanie zdarzeń do wszystkich klientów
  // -------------------------------------------------------------------------
  void Broadcast(const Envelope& env) {
    std::lock_guard<std::mutex> lk(clients_mu_);
    for (auto& [id, session] : clients_) {
      session->Send(env);
    }
  }

  // -------------------------------------------------------------------------
  // Watchdog: heartbeat gościa + stan modemu
  // -------------------------------------------------------------------------
  void WatchdogLoop() {
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(5));

      int64_t now = NowMs();
      std::vector<std::shared_ptr<ClientSession>> dead;
      {
        std::lock_guard<std::mutex> lk(clients_mu_);
        for (auto& [id, s] : clients_) {
          if (now - s->LastHeartbeatMs() > cfg_.heartbeat_timeout_ms) {
            dead.push_back(s);
          }
        }
      }
      for (auto& s : dead) {
        Log("klient nie odpowiada (brak heartbeat): id=" + std::to_string(s->client_id()));
        // Nie zrywamy połączenia alarmowego — tylko logujemy.
        // Gość po restarcie podłączy się ponownie.
      }

      // Heartbeat do gościa (gość wie, że host żyje)
      Envelope hb;
      hb.set_protocol_version(1);
      hb.set_timestamp_ms(now);
      auto* h = hb.mutable_heartbeat();
      h->set_uptime_ms(now);
      h->set_modem_state(backend_->StateString());
      Broadcast(hb);
    }
  }

  static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  static const char* ErrorCodeName(Error::Code c) {
    switch (c) {
      case Error::OK: return "ok";
      case Error::RADIO_NOT_AVAILABLE: return "radio niedostępne";
      case Error::MODEM_BUSY: return "modem zajęty";
      case Error::SIM_ERROR: return "błąd SIM";
      case Error::NETWORK_REJECTED: return "sieć odrzuciła";
      case Error::INVALID_ARGUMENT: return "nieprawidłowy argument";
      case Error::TIMEOUT: return "timeout";
      case Error::NOT_SUPPORTED: return "nieobsługiwane";
      case Error::EMERGENCY_ONLY: return "tylko połączenia alarmowe";
      default: return "błąd wewnętrzny";
    }
  }

  static void Log(const std::string& msg) {
    // W produkcji: syslog/journald. Tu stderr → journald przez systemd.
    fprintf(stderr, "[rilbridged] %s\n", msg.c_str());
  }

  Config cfg_;
  std::unique_ptr<ModemBackend> backend_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> next_client_id_{0};
  std::mutex clients_mu_;
  std::unordered_map<uint32_t, std::shared_ptr<ClientSession>> clients_;
  std::thread accept_thread_;
  std::thread watchdog_thread_;
};

}  // namespace telephony
}  // namespace aerophone

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  using namespace aerophone::telephony;

  Config cfg;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) cfg.vsock_port = std::stoul(argv[++i]);
    else if (a == "--modem" && i + 1 < argc) cfg.modem_device = argv[++i];
    else if (a == "--backend" && i + 1 < argc) cfg.backend = argv[++i];
    else if (a == "--help") {
      fprintf(stdout,
              "rilbridged — most RIL host↔ARCVM\n"
              "  --port N       port vsock (domyślnie 5000)\n"
              "  --modem PATH   urządzenie modemu (domyślnie /dev/qmi0)\n"
              "  --backend NAME qmi|mbim (domyślnie qmi)\n");
      return 0;
    }
  }

  // Fabryka backendu — w pełnej implementacji: QmiBackend / MbimBackend
  std::unique_ptr<ModemBackend> backend = CreateBackend(cfg.backend);
  if (!backend) {
    fprintf(stderr, "[rilbridged] nieznany backend: %s\n", cfg.backend.c_str());
    return 1;
  }

  RilBridge bridge(cfg, std::move(backend));
  if (!bridge.Start()) return 1;

  // Czekaj na SIGTERM (systemd)
  pause();
  bridge.Stop();
  return 0;
}
