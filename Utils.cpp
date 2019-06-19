#include "Utils.h"
#include "tsassert.h"

namespace TdmSync {

StdioFileHolder::~StdioFileHolder()
{}
StdioFileHolder::StdioFileHolder(FILE *f)
    : StdioFileUniquePtr(f, fclose)
{}
StdioFileHolder::StdioFileHolder(const char *path, const char *mode)
    : StdioFileUniquePtr(fopen(path, mode), fclose)
{
    TdmSyncAssertF(get(), "Failed to open file \"%s\"", path);
}

bool IfExists(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

}
