// rilbridged.cc — demon mostu RIL (host ↔ ARCVM).
//
// Rola: siedzi na hoście (Chromium OS), rozmawia z modemem przez backend
// (QMI/MBIM) i wystawia protokół rilbridge na virtio-vsock. Gość (ARCVM)
// łączy się przez RadioBridge (HAL radiowy) i tak telefon działa w Androidzie.
//
// Dlaczego na hoście, a nie w gościu:
//   1. Połączenia alarmowe muszą działać, gdy ARCVM padnie
//   2. update_engine potrzebuje danych komórkowych do pobrania OTA
//   3. powerd musi widzieć stan modemu, żeby zarządzać energią
//
// Użycie:
//   rilbridged [--port N] [--modem PATH] [--backend qmi|mbim]

#include <arpa/inet.h>   // htonl / ntohl

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>        // fprintf
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <linux/vm_sockets.h>
#include <sys/socket.h>
#include <unistd.h>

#include "qmi_backend.h"
#include "rilbridge.pb.h"

namespace aerophone {
namespace telephony {

namespace {

// ---------------------------------------------------------------------------
// Konfiguracja domyślna
// ---------------------------------------------------------------------------
constexpr uint32_t kDefaultVsockPort = 5000;
constexpr const char* kDefaultModem = "/dev/qmi0";
constexpr const char* kDefaultBackend = "qmi";
constexpr uint32_t kProtocolVersion = 1;
constexpr size_t kMaxFrame = 1u << 20;   // 1 MiB — górny limit ramki

// ---------------------------------------------------------------------------
// Pomocnicze: czas
// ---------------------------------------------------------------------------
int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Ramkowanie: [4 B długość BE][payload protobuf]
// ---------------------------------------------------------------------------
bool WriteFrame(int fd, const Envelope& env) {
  std::string buf;
  if (!env.SerializeToString(&buf)) return false;

  uint32_t len = htonl(static_cast<uint32_t>(buf.size()));
  if (::send(fd, &len, sizeof(len), MSG_NOSIGNAL) != sizeof(len)) return false;
  if (::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL) !=
      static_cast<ssize_t>(buf.size())) {
    return false;
  }
  return true;
}

bool ReadAll(int fd, void* dst, size_t n) {
  size_t got = 0;
  auto* p = static_cast<char*>(dst);
  while (got < n) {
    ssize_t r = ::recv(fd, p + got, n - got, 0);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

bool ReadFrame(int fd, Envelope* env) {
  uint32_t len_be = 0;
  if (!ReadAll(fd, &len_be, sizeof(len_be))) return false;
  uint32_t len = ntohl(len_be);
  if (len == 0 || len > kMaxFrame) return false;

  std::string buf(len, '\0');
  if (!ReadAll(fd, buf.data(), len)) return false;
  return env->ParseFromString(buf);
}

// ---------------------------------------------------------------------------
// Odpowiedź błędu
// ---------------------------------------------------------------------------
Envelope MakeError(const Envelope& req, int code, const std::string& message) {
  Envelope reply;
  reply.set_protocol_version(kProtocolVersion);
  reply.set_request_id(req.request_id());
  reply.set_client_id(req.client_id());
  reply.set_timestamp_ms(NowMs());
  auto* err = reply.mutable_error();
  err->set_code(code);
  err->set_message(message);
  return reply;
}

// ---------------------------------------------------------------------------
// Sesja jednego klienta (ARCVM albo narzędzie diagnostyczne)
// ---------------------------------------------------------------------------
class ClientSession {
 public:
  ClientSession(int fd, ModemBackend* backend)
      : fd_(fd), backend_(backend) {}

  ~ClientSession() {
    if (fd_ >= 0) ::close(fd_);
  }

  void Run() {
    // Backend wysyła zdarzenia asynchroniczne do tego klienta
    backend_->SetEventSink([this](const Envelope& ev) {
      std::lock_guard<std::mutex> lk(write_mu_);
      if (fd_ >= 0) WriteFrame(fd_, ev);
    });

    Envelope req;
    while (ReadFrame(fd_, &req)) {
      // Heartbeat — nie wymaga backendu
      if (req.has_heartbeat()) {
        Envelope reply;
        reply.set_protocol_version(kProtocolVersion);
        reply.set_request_id(req.request_id());
        reply.set_client_id(req.client_id());
        reply.set_timestamp_ms(NowMs());
        reply.mutable_heartbeat()->set_uptime_ms(NowMs());
        std::lock_guard<std::mutex> lk(write_mu_);
        if (!WriteFrame(fd_, reply)) break;
        continue;
      }

      // Wersja protokołu — rozjazd = rozłączenie (lepiej głośno niż cicho)
      if (req.protocol_version() != kProtocolVersion) {
        std::lock_guard<std::mutex> lk(write_mu_);
        WriteFrame(fd_, MakeError(req, 2, "niezgodna wersja protokołu"));
        break;
      }

      Envelope reply;
      if (!backend_->Handle(req, &reply)) {
        std::lock_guard<std::mutex> lk(write_mu_);
        WriteFrame(fd_, MakeError(req, 1, "błąd backendu modemu"));
        break;
      }

      std::lock_guard<std::mutex> lk(write_mu_);
      if (!WriteFrame(fd_, reply)) break;
    }

    backend_->SetEventSink(nullptr);
  }

 private:
  int fd_;
  ModemBackend* backend_;
  std::mutex write_mu_;
};

// ---------------------------------------------------------------------------
// Nasłuch na vsock
// ---------------------------------------------------------------------------
int ListenVsock(uint32_t port) {
  int fd = ::socket(AF_VSOCK, SOCK_STREAM, 0);
  if (fd < 0) {
    fprintf(stderr, "rilbridged: socket(AF_VSOCK) nie powiodło się\n");
    return -1;
  }

  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_vm addr{};
  addr.svm_family = AF_VSOCK;
  addr.svm_cid = VMADDR_CID_ANY;   // nasłuch na wszystkich CID
  addr.svm_port = port;

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    fprintf(stderr, "rilbridged: bind na vsock:%u nie powiodło się\n", port);
    ::close(fd);
    return -1;
  }

  if (::listen(fd, 4) < 0) {
    fprintf(stderr, "rilbridged: listen nie powiodło się\n");
    ::close(fd);
    return -1;
  }

  return fd;
}

void PrintHelp() {
  printf("rilbridged — most RIL host↔ARCVM\n");
  printf("  --port N       port vsock (domyślnie %u)\n", kDefaultVsockPort);
  printf("  --modem PATH   urządzenie modemu (domyślnie %s)\n", kDefaultModem);
  printf("  --backend NAME qmi|mbim (domyślnie %s)\n", kDefaultBackend);
}

}  // namespace
}  // namespace telephony
}  // namespace aerophone

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  using namespace aerophone::telephony;

  uint32_t port = kDefaultVsockPort;
  std::string modem = kDefaultModem;
  std::string backend_name = kDefaultBackend;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (arg == "--modem" && i + 1 < argc) {
      modem = argv[++i];
    } else if (arg == "--backend" && i + 1 < argc) {
      backend_name = argv[++i];
    } else {
      fprintf(stderr, "rilbridged: nieznany argument: %s\n", arg.c_str());
      PrintHelp();
      return 2;
    }
  }

  // Backend modemu
  std::unique_ptr<ModemBackend> backend;
  if (backend_name == "mbim") {
    backend = CreateMbimBackend();
  } else {
    backend = CreateQmiBackend();
  }

  if (!backend->Open(modem)) {
    fprintf(stderr, "rilbridged: nie mogę otworzyć modemu %s (backend %s)\n",
            modem.c_str(), backend->Name());
    return 1;
  }

  printf("rilbridged: modem %s otwarty (backend %s)\n", modem.c_str(), backend->Name());

  int listen_fd = ListenVsock(port);
  if (listen_fd < 0) {
    backend->Close();
    return 1;
  }

  printf("rilbridged: nasłuch na vsock:%u (protokół v%u)\n", port, kProtocolVersion);

  // Pętla akceptacji — jeden wątek na klienta
  std::vector<std::thread> sessions;
  while (true) {
    int client_fd = ::accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
      fprintf(stderr, "rilbridged: accept nie powiodło się\n");
      break;
    }
    printf("rilbridged: klient podłączony (fd=%d)\n", client_fd);
    sessions.emplace_back([client_fd, backend = backend.get()] {
      ClientSession session(client_fd, backend);
      session.Run();
    });
  }

  for (auto& t : sessions) {
    if (t.joinable()) t.join();
  }
  ::close(listen_fd);
  backend->Close();
  return 0;
}
