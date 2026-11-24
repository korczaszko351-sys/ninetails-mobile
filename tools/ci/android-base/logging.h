// tools/ci/android-base/logging.h
//
// SHIM DLA CI — NIE JEST CZĘŚCIĄ PRODUKTU.
//
// W drzewie AOSP i Chromium OS nagłówek <android-base/logging.h> pochodzi
// z biblioteki libbase (system/core/libbase). Ten plik podstawia zgodny
// interfejs, żeby źródła mostu RIL dało się skompilować na zwykłym
// runnerze GitHub Actions, bez pobierania całego drzewa AOSP.
//
// Podmieniane są wyłącznie makra logowania używane przez rilbridged.cc,
// qmi_backend.cc, mbim_backend.cc i dbus_service.cc: LOG(INFO), LOG(ERROR),
// LOG(WARNING), LOG(DEBUG) oraz CHECK/PCHECK.
//
// W prawdziwym buildzie ten katalog NIE jest na ścieżce include —
// kompilator bierze <android-base/logging.h> z libbase.

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace android {
namespace base {

enum LogSeverity {
    VERBOSE,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL,
};

// Prosty logger na stderr — w CI wystarczy, żeby zobaczyć, co robi most.
inline void LogMessage(LogSeverity sev, const char* file, int line,
                       const std::string& msg) {
    const char* name = "INFO";
    switch (sev) {
        case VERBOSE: name = "VERBOSE"; break;
        case DEBUG:   name = "DEBUG";   break;
        case INFO:    name = "INFO";    break;
        case WARNING: name = "WARNING"; break;
        case ERROR:   name = "ERROR";   break;
        case FATAL:   name = "FATAL";   break;
    }
    std::fprintf(stderr, "[%s] %s:%d %s\n", name, file, line, msg.c_str());
    std::fflush(stderr);
}

// LOG(INFO) << "tekst" — operator<< dopisuje do bufora, a destruktor
// wypisuje całość. Uproszczone, ale wystarczające dla CI.
class LogStream {
  public:
    LogStream(LogSeverity sev, const char* file, int line)
        : sev_(sev), file_(file), line_(line) {}

    ~LogStream() { LogMessage(sev_, file_, line_, buf_); }

    LogStream& operator<<(const std::string& v) { buf_ += v; return *this; }
    LogStream& operator<<(const char* v) { buf_ += (v ? v : "(null)"); return *this; }
    LogStream& operator<<(bool v) { buf_ += (v ? "true" : "false"); return *this; }

    template <typename T>
    LogStream& operator<<(const T& v) {
        buf_ += std::to_string(v);
        return *this;
    }

  private:
    LogSeverity sev_;
    const char* file_;
    int line_;
    std::string buf_;
};

}  // namespace base
}  // namespace android

// --- makra zgodne z libbase ------------------------------------------------

#define LOG(severity) \
    ::android::base::LogStream(::android::base::severity, __FILE__, __LINE__)

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ::android::base::LogMessage(::android::base::FATAL, __FILE__,   \
                                        __LINE__,                           \
                                        "CHECK failed: " #cond);            \
            ::std::abort();                                                 \
        }                                                                   \
    } while (0)

#define PCHECK(cond)                                                        \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ::android::base::LogMessage(                                    \
                ::android::base::FATAL, __FILE__, __LINE__,                 \
                ::std::string("PCHECK failed: " #cond " errno=") +          \
                    ::std::strerror(errno));                                \
            ::std::abort();                                                 \
        }                                                                   \
    } while (0)
