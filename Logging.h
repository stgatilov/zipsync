#pragma once

#include <stdarg.h>
#include <stdexcept>

namespace ZipSync {

//how severe logged message is
enum Severity {
    sevVerbose = 1,
    sevDebug,
    sevInfo,
    sevWarning,
    sevError,   //throws exception
    sevFatal,   //terminates program immediately
};

//some messages are assigned nonzero "code"
//it allows intercepting them in error exceptions and in tests
enum LogCode {
    lcGeneric = 0,
    lcAssertFailed = 1,
    lcMinizipError = 2,
};

//thrown when message with "error" severity is posted
class ErrorException : public std::runtime_error {
    int _code;
public:
    ErrorException(const char *message, int code = lcGeneric);
    int code() const { return _code; }
};

//base class of Logger
class Logger {
public:
    virtual void PostMessage(int code, Severity severity, const char *message) = 0;

    void logf(Severity severity, int code, const char *format, ...);
    void logv(Severity severity, int code, const char *format, va_list args);

    void verbosef   (int code, const char *format, ...);
    void debugf     (int code, const char *format, ...);
    void infof      (int code, const char *format, ...);
    void warningf   (int code, const char *format, ...);
    void errorf     (int code, const char *format, ...);
    void fatalf     (int code, const char *format, ...);

    void verbosef   (const char *format, ...);
    void debugf     (const char *format, ...);
    void infof      (const char *format, ...);
    void warningf   (const char *format, ...);
    void errorf     (const char *format, ...);
    void fatalf     (const char *format, ...);
};

std::string formatMessage(const char *format, ...);
std::string assertFailedMessage(const char *code, const char *file, int line);

#define ZipSyncAssert(cond) if (!(cond)) throw ErrorException(assertFailedMessage(#cond, __FILE__, __LINE__).c_str(), lcAssertFailed);
#define ZipSyncAssertF(cond, ...) if (!(cond)) throw ErrorException(formatMessage(__VA_ARGS__).c_str(), lcAssertFailed);

}
