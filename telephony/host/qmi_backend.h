// qmi_backend.h — interfejs backendu QMI (Qualcomm MSM Interface).
//
// Backend QMI realizuje żądania protokołu rilbridge na urządzeniu modemowym
// (/dev/qmi0) przy użyciu QMI. W drzewie AOSP/Chromium OS korzysta z libqmi
// albo własnego klienta QMI (QMI to binarny protokół Qualcomma).
//
// Ten plik definiuje kontrakt: co backend musi umieć. Implementacja
// (qmi_backend.cc) mapuje każde żądanie na odpowiednią wiadomość QMI.

#ifndef AEROPHONE_TELEPHONY_HOST_QMI_BACKEND_H_
#define AEROPHONE_TELEPHONY_HOST_QMI_BACKEND_H_

#include <cstdint>
#include <memory>
#include <string>

#include "rilbridge.pb.h"

namespace aerophone {
namespace telephony {

// Typy protokołu (Envelope, Error, *Request, *Event) są generowane przez protoc
// w przestrzeni nazw aerophone::rilbridge (zgodnie z `package aerophone.rilbridge;`
// w rilbridge.proto). Wciągamy je do tej przestrzeni, żeby deklaracje backendów
// i ich implementacje nie musiały kwalifikować każdego typu osobno.
using namespace ::aerophone::rilbridge;

// Interfejs backendu modemu.
//
// Cykl życia:
//   Open()  — otwiera urządzenie, negocjuje wersję QMI
//   Handle() — obsługuje jedno żądanie z protokołu rilbridge
//   Close() — zamyka urządzenie
class ModemBackend {
 public:
  virtual ~ModemBackend() = default;

  // Otwiera urządzenie modemowe. Zwraca false, jeśli się nie udało.
  virtual bool Open(const std::string& device) = 0;

  // Zamyka urządzenie. Bezpieczne do wielokrotnego wywołania.
  virtual void Close() = 0;

  // Czy backend jest gotowy do pracy.
  virtual bool IsOpen() const = 0;

  // Nazwa backendu ("qmi", "mbim") — do logów i diagnostyki.
  virtual const char* Name() const = 0;

  // Obsługuje jedno żądanie. Wypełnia `reply` (response albo error).
  // Zwraca false tylko przy błędzie transportu (wtedy połączenie pada).
  virtual bool Handle(const Envelope& request, Envelope* reply) = 0;

  // Wywoływane, gdy modem zgłasza zdarzenie asynchroniczne (połączenie
  // przychodzące, SMS, zmiana stanu SIM). Implementacja wysyła je do klienta.
  virtual void SetEventSink(std::function<void(const Envelope&)> sink) = 0;
};

// Fabryki backendów.
std::unique_ptr<ModemBackend> CreateQmiBackend();
std::unique_ptr<ModemBackend> CreateMbimBackend();

}  // namespace telephony
}  // namespace aerophone

#endif  // AEROPHONE_TELEPHONY_HOST_QMI_BACKEND_H_
