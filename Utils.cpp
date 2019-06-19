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

bool IfFileExists(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}
void RemoveFile(const std::string &path) {
    int res = remove(path.c_str());
    TdmSyncAssertF(res == 0, "Failed to remove file %s (error %d)\n", path.c_str(), res);
}
void RenameFile(const std::string &oldPath, const std::string &newPath) {
    int res = rename(oldPath.c_str(), newPath.c_str());
    TdmSyncAssertF(res == 0, "Failed to rename file %s to %s\n", oldPath.c_str(), newPath.c_str(), res);
}

}
