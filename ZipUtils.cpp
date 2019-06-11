#include "ZipUtils.h"


namespace TdmSync {

int zipCloseNoComment(zipFile zf) {
    return zipClose(zf, NULL);
}

UnzFileHolder::~UnzFileHolder()
{}
UnzFileHolder::UnzFileHolder(unzFile zf)
    : UnzFileUniquePtr(zf, unzClose)
{}
UnzFileHolder::UnzFileHolder(const char *path)
    : UnzFileUniquePtr(unzOpen(path), unzClose)
{
    TdmSyncAssertF(get(), "Failed to open zip file \"%s\"", path);
}

ZipFileHolder::~ZipFileHolder()
{}
ZipFileHolder::ZipFileHolder(zipFile zf)
    : ZipFileUniquePtr(zf, zipCloseNoComment) 
{}
ZipFileHolder::ZipFileHolder(const char *path)
    : ZipFileUniquePtr(zipOpen(path, 0), zipCloseNoComment)
{
    TdmSyncAssertF(get(), "Failed to open zip file \"%s\"", path);
}

MinizipError::MinizipError(int errcode)
    : BaseError("Minizip error " + std::to_string(errcode))
{}
MinizipError::~MinizipError()
{}

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
