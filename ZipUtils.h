#pragma once

namespace TdmSync {

typedef std::unique_ptr<std::remove_pointer<unzFile>::type, int (*)(unzFile)> unzFileUniquePtr;
class unzFileHolder : public unzFileUniquePtr {
public:
    unzFileHolder(unzFile zf) : unzFileUniquePtr(zf, unzClose) {}
    unzFileHolder(const char *path) : unzFileUniquePtr(unzOpen(path), unzClose) { TdmSyncAssertF(get(), "Failed to open zip file \"%s\"", path); }
    operator unzFile() const { return get(); }
};

typedef std::unique_ptr<std::remove_pointer<zipFile>::type, int (*)(zipFile)> zipFileUniquePtr;
int zipCloseNoComment(zipFile zf) { return zipClose(zf, NULL); }
class zipFileHolder : public zipFileUniquePtr {
public:
    zipFileHolder(zipFile zf) : zipFileUniquePtr(zf, zipCloseNoComment) {}
    zipFileHolder(const char *path) : zipFileUniquePtr(zipOpen(path, 0), zipCloseNoComment) { TdmSyncAssertF(get(), "Failed to open zip file \"%s\"", path); }
    operator zipFile() const { return get(); }
};

class MinizipError : public BaseError {
public:
    MinizipError(int errcode) : BaseError("Minizip error " + std::to_string(errcode)) {}
};
#define SAFE_CALL(...) \
    do { \
        int mz_errcode = __VA_ARGS__; \
        if (mz_errcode != 0) throw MinizipError(mz_errcode); \
    } while (0)


//note: file must be NOT opened
void unzGetCurrentFilePosition(unzFile zf, uint32_t *localHeaderStart, uint32_t *fileDataStart, uint32_t *fileDataEnd) {
    unz_file_info info;
    SAFE_CALL(unzGetCurrentFileInfo(zf, &info, NULL, 0, NULL, 0, NULL, 0));
    SAFE_CALL(unzOpenCurrentFile(zf));
    int64_t pos = unzGetCurrentFileZStreamPos64(zf);
    int localHeaderSize = 30 + info.size_filename + info.size_file_extra;
    if (localHeaderStart)
        *localHeaderStart = pos - localHeaderSize;
    if (fileDataStart)
        *fileDataStart = pos;
    if (fileDataEnd)
        *fileDataEnd = pos + info.compressed_size;
    SAFE_CALL(unzCloseCurrentFile(zf));
}

}