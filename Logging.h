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
    lcAssertFailed,             //ZipSyncAssert has failed
    lcCantOpenFile,             //unexpected fail when opening file
    lcMinizipError,             //unexpected error from minizip function
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
    virtual void Message(int code, Severity severity, const char *message) = 0;

    void logf(Severity severity, int code, const char *format, ...);
    void logv(Severity severity, int code, const char *format, va_list args);

                 void verbosef   (int code, const char *format, ...);
                 void debugf     (int code, const char *format, ...);
                 void infof      (int code, const char *format, ...);
                 void warningf   (int code, const char *format, ...);
    [[noreturn]] void errorf     (int code, const char *format, ...);
    [[noreturn]] void fatalf     (int code, const char *format, ...);

                 void verbosef   (const char *format, ...);
                 void debugf     (const char *format, ...);
                 void infof      (const char *format, ...);
                 void warningf   (const char *format, ...);
    [[noreturn]] void errorf     (const char *format, ...);
    [[noreturn]] void fatalf     (const char *format, ...);
};

//global instance of logger, used for everything
extern Logger *g_logger;

std::string formatMessage(const char *format, ...);
std::string assertFailedMessage(const char *code, const char *file, int line);

#define ZipSyncAssert(cond) if (!(cond)) g_logger->errorf(lcAssertFailed, assertFailedMessage(#cond, __FILE__, __LINE__).c_str());
#define ZipSyncAssertF(cond, ...) if (!(cond)) g_logger->errorf(lcAssertFailed, __VA_ARGS__);

}
