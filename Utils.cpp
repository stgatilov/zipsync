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

}
