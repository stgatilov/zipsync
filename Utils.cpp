#include "Utils.h"
#include "ZSAssert.h"


namespace ZipSync {

StdioFileHolder::~StdioFileHolder()
{}
StdioFileHolder::StdioFileHolder(FILE *f)
    : StdioFileUniquePtr(f, fclose)
{}
StdioFileHolder::StdioFileHolder(const char *path, const char *mode)
    : StdioFileUniquePtr(fopen(path, mode), fclose)
{
    ZipSyncAssertF(get(), "Failed to open file \"%s\"", path);
}

}
