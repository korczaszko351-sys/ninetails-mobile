// dbus_service.cc — usługa D-Bus telefonii hosta.
//
// Wystawia interfejs com.aerophone.Telephony na systemowej szynie D-Bus,
// żeby powłoka (Ash/Chrome) i ustawienia mogły:
//   - odczytać stan modemu (zasięg, operator, roaming)
//   - włączyć/wyłączyć radio
//   - wybrać numer alarmowy (ścieżka niezależna od ARCVM)
//
// To osobna binarka od rilbridged (własny main) — most RIL i usługa D-Bus
// to dwa procesy o różnych cyklach życia.

#include <dbus/dbus.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kBusName = "com.aerophone.Telephony";
constexpr const char* kObjectPath = "/com/aerophone/Telephony";
constexpr const char* kInterface = "com.aerophone.Telephony";

// ---------------------------------------------------------------------------
// Odpowiedź na metodę: stan modemu (na razie statyczny — do podłączenia
// z rilbridged przez wspólną pamięć albo drugą szynę).
// ---------------------------------------------------------------------------
DBusHandlerResult HandleGetModemState(DBusConnection* conn, DBusMessage* msg) {
  DBusMessage* reply = dbus_message_new_method_return(msg);
  if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;

  const char* state = "ready";
  const char* operator_name = "AeroPhone";
  dbus_bool_t roaming = FALSE;
  dbus_int32_t signal_level = 0;

  dbus_message_append_args(reply,
                           DBUS_TYPE_STRING, &state,
                           DBUS_TYPE_STRING, &operator_name,
                           DBUS_TYPE_BOOLEAN, &roaming,
                           DBUS_TYPE_INT32, &signal_level,
                           DBUS_TYPE_INVALID);

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// ---------------------------------------------------------------------------
// Wybór numeru alarmowego — ścieżka hosta, niezależna od ARCVM.
// ---------------------------------------------------------------------------
DBusHandlerResult HandleDialEmergency(DBusConnection* conn, DBusMessage* msg) {
  const char* number = nullptr;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &number,
                             DBUS_TYPE_INVALID)) {
    DBusMessage* err = dbus_message_new_error(
        msg, DBUS_ERROR_INVALID_ARGS, "oczekiwano numeru (string)");
    dbus_connection_send(conn, err, nullptr);
    dbus_message_unref(err);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  fprintf(stderr, "telephony-dbus: żądanie połączenia alarmowego: %s\n", number);

  // UWAGA: tu musi trafić wywołanie backendu modemu (QMI voice call).
  // Ścieżka alarmowa celowo NIE przechodzi przez ARCVM.
  DBusMessage* reply = dbus_message_new_method_return(msg);
  dbus_bool_t accepted = TRUE;
  dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &accepted, DBUS_TYPE_INVALID);
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// ---------------------------------------------------------------------------
// Dyspozytor metod
// ---------------------------------------------------------------------------
DBusHandlerResult HandleMessage(DBusConnection* conn, DBusMessage* msg, void* /*user*/) {
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  const char* iface = dbus_message_get_interface(msg);
  if (!iface || std::strcmp(iface, kInterface) != 0) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  const char* member = dbus_message_get_member(msg);
  if (!member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (std::strcmp(member, "GetModemState") == 0) {
    return HandleGetModemState(conn, msg);
  }
  if (std::strcmp(member, "DialEmergency") == 0) {
    return HandleDialEmergency(conn, msg);
  }

  DBusMessage* err = dbus_message_new_error(
      msg, DBUS_ERROR_UNKNOWN_METHOD, "nieznana metoda");
  dbus_connection_send(conn, err, nullptr);
  dbus_message_unref(err);
  return DBUS_HANDLER_RESULT_HANDLED;
}

}  // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  DBusError err;
  dbus_error_init(&err);

  DBusConnection* conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
  if (!conn) {
    fprintf(stderr, "telephony-dbus: nie mogę połączyć się z szyną systemową: %s\n",
            err.message ? err.message : "nieznany błąd");
    dbus_error_free(&err);
    return 1;
  }

  int ret = dbus_bus_request_name(conn, kBusName,
                                  DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "telephony-dbus: request_name nie powiodło się: %s\n", err.message);
    dbus_error_free(&err);
    return 1;
  }
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    fprintf(stderr, "telephony-dbus: nazwa %s już zajęta\n", kBusName);
    return 1;
  }

  // DBusObjectPathVTable: unregister_function, message_function oraz cztery
  // pola zarezerwowane (dbus_internal_pad1..4) — wszystkie muszą być NULL.
  static const DBusObjectPathVTable vtable = {
      nullptr,          // unregister_function
      HandleMessage,    // message_function
      nullptr,          // dbus_internal_pad1
      nullptr,          // dbus_internal_pad2
      nullptr,          // dbus_internal_pad3
      nullptr,          // dbus_internal_pad4
  };

  if (!dbus_connection_register_object_path(conn, kObjectPath, &vtable, nullptr)) {
    fprintf(stderr, "telephony-dbus: rejestracja obiektu nie powiodła się\n");
    return 1;
  }

  printf("telephony-dbus: usługa %s zarejestrowana na %s\n", kBusName, kObjectPath);

  // Pętla główna
  while (dbus_connection_read_write_dispatch(conn, -1)) {
    // dispatch robi całą pracę
  }

  dbus_connection_unref(conn);
  return 0;
}
