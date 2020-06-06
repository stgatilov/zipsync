#include "Logging.h"
#include <stdio.h>

namespace ZipSync {

class LoggerConsole : public Logger {
public:
    virtual void Message(int code, Severity severity, const char *message) override {
        printf("%s%s\n",
            severity == sevFatal ? "FATAL: " :
            severity == sevError ? "ERROR: " :
            severity == sevWarning ? "Warning: " : "",
            message
        );
    }
};

Logger *g_logger = new LoggerConsole();

ErrorException::ErrorException(const char *message, int code) :
    std::runtime_error(message), _code(code)
{}

void Logger::logv(Severity severity, int code, const char *format, va_list args) {
    char buff[16<<10];
    vsnprintf(buff, sizeof(buff), format, args);
    buff[sizeof(buff)-1] = 0;
    Message(code, severity, buff);
    if (severity == sevFatal)
        std::terminate();
    if (severity == sevError)
        throw ErrorException(buff, code);
}
void Logger::logf(Severity severity, int code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    logv(severity, code, format, args);
    va_end(args);
}

#define LOGHELPER(severity) va_list args; va_start(args, format); logv(severity, code, format, args); va_end(args);
void Logger::verbosef   (int code, const char *format, ...) { LOGHELPER(sevVerbose); }
void Logger::debugf     (int code, const char *format, ...) { LOGHELPER(sevDebug); }
void Logger::infof      (int code, const char *format, ...) { LOGHELPER(sevInfo); }
void Logger::warningf   (int code, const char *format, ...) { LOGHELPER(sevWarning); }
void Logger::errorf     (int code, const char *format, ...) { LOGHELPER(sevError); }
void Logger::fatalf     (int code, const char *format, ...) { LOGHELPER(sevFatal); }
#undef LOGHELPER
#define LOGHELPER(severity) va_list args; va_start(args, format); logv(severity, lcGeneric, format, args); va_end(args);
void Logger::verbosef   (const char *format, ...) { LOGHELPER(sevVerbose); }
void Logger::debugf     (const char *format, ...) { LOGHELPER(sevDebug); }
void Logger::infof      (const char *format, ...) { LOGHELPER(sevInfo); }
void Logger::warningf   (const char *format, ...) { LOGHELPER(sevWarning); }
void Logger::errorf     (const char *format, ...) { LOGHELPER(sevError); }
void Logger::fatalf     (const char *format, ...) { LOGHELPER(sevFatal); }
#undef LOGHELPER

std::string formatMessage(const char *format, ...) {
    char buff[16<<10];
    va_list args;
    va_start(args, format);
    vsnprintf(buff, sizeof(buff), format, args);
    buff[sizeof(buff)-1] = 0;
    va_end(args);
    return buff;
}

std::string assertFailedMessage(const char *code, const char *file, int line) {
    char buff[256];
    sprintf(buff, "Assertion %s failed in %s on line %d", code, file, line);
    return buff;
}

}
