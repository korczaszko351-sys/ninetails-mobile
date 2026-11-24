// dbus_service.cc — usługa D-Bus telefonii hosta (L4).
//
// Rola: wystawia stan telefonii powłoce (Ash) i ustawieniom przez D-Bus.
// rilbridged mówi protokołem rilbridge do gościa; ta usługa mówi D-Busem
// do reszty systemu hosta.
//
// Interfejs: org.chromium.AeroPhoneTelephony
//   metody:  GetState() → (s state, i signalLevel, s operator, b roaming)
//            GetCallState() → (s state, s number)
//            Dial(s number), Hangup()
//   sygnały: StateChanged(...), CallStateChanged(...)
//
// Dlaczego osobna usługa, a nie D-Bus w rilbridged:
//   - rilbridged działa jako root z dostępem do modemu (minimalne uprawnienia)
//   - usługa D-Bus działa jako użytkownik systemowy, widoczna dla powłoki
//   - rozdzielenie = mniejsza powierzchnia ataku

#include <dbus/dbus.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kBusName = "org.chromium.AeroPhoneTelephony";
constexpr const char* kObjectPath = "/org/chromium/AeroPhoneTelephony";
constexpr const char* kInterface = "org.chromium.AeroPhoneTelephony";

// Stan telefonii — w pełnej implementacji czytany z rilbridged
// (przez gniazdo UNIX albo wspólną pamięć). Tu: stan zastępczy.
struct TelephonyState {
  std::string state = "idle";
  int signal_level = 0;
  std::string operator_name = "";
  bool roaming = false;
};

TelephonyState g_state;

// ---------------------------------------------------------------------------
// Obsługa metod
// ---------------------------------------------------------------------------
DBusHandlerResult HandleMessage(DBusConnection* conn, DBusMessage* msg, void* /*user*/) {
  if (dbus_message_is_method_call(msg, kInterface, "GetState")) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);

    const char* st = g_state.state.c_str();
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &st);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &g_state.signal_level);
    const char* op = g_state.operator_name.c_str();
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &op);
    dbus_bool_t roam = g_state.roaming ? TRUE : FALSE;
    dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &roam);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  if (dbus_message_is_method_call(msg, kInterface, "Dial")) {
    const char* number = nullptr;
    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &number,
                               DBUS_TYPE_INVALID)) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    fprintf(stderr, "[telephony-dbus] Dial(%s)\n", number);
    // TODO: przekaż do rilbridged (UNIX socket / D-Bus do rilbridged)
    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  if (dbus_message_is_method_call(msg, kInterface, "Hangup")) {
    fprintf(stderr, "[telephony-dbus] Hangup()\n");
    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

}  // namespace

int main() {
  DBusError err;
  dbus_error_init(&err);

  DBusConnection* conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
  if (!conn) {
    fprintf(stderr, "[telephony-dbus] nie mogę połączyć się z system bus: %s\n",
            err.message);
    dbus_error_free(&err);
    return 1;
  }

  int ret = dbus_bus_request_name(conn, kBusName, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "[telephony-dbus] request_name: %s\n", err.message);
    dbus_error_free(&err);
    return 1;
  }
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    fprintf(stderr, "[telephony-dbus] nazwa zajęta\n");
    return 1;
  }

  static const DBusObjectPathVTable vtable = {
      .message_function = HandleMessage,
  };
  if (!dbus_connection_register_object_path(conn, kObjectPath, &vtable, nullptr)) {
    fprintf(stderr, "[telephony-dbus] register_object_path nie powiodło się\n");
    return 1;
  }

  fprintf(stderr, "[telephony-dbus] wystartował jako %s\n", kBusName);

  while (dbus_connection_read_write_dispatch(conn, -1)) {
    // pętla zdarzeń
  }

  dbus_connection_unref(conn);
  return 0;
}
