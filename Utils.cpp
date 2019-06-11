#include "Utils.h"
#include "tsassert.h"

namespace TdmSync {

StdioFileHolder::StdioFileHolder(FILE *f)
    : StdioFileUniquePtr(f, fclose)
{}
StdioFileHolder::~StdioFileHolder()
{}
StdioFileHolder::StdioFileHolder(const char *path, const char *mode)
    : StdioFileUniquePtr(fopen(path, mode), fclose)
{
    TdmSyncAssertF(get(), "Failed to open file \"%s\"", path);
}

}
