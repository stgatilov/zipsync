#ifndef _TDM_SYNC_ASSERT_H_923439
#define _TDM_SYNC_ASSERT_H_923439

#include <string>

namespace TdmSync {
//base exception thrown by tdmsync when something fails
struct BaseError : public std::runtime_error {
    BaseError(const std::string &message) : std::runtime_error(message) {}
};

std::string assertFailedMessage(const char *code, const char *file, int line);
std::string formatMessage(const char *format, ...);
#define TdmSyncAssert(cond) if (!(cond)) throw BaseError(assertFailedMessage(#cond, __FILE__, __LINE__));
#define TdmSyncAssertF(cond, ...) if (!(cond)) throw BaseError(formatMessage(__VA_ARGS__));
}

#endif
