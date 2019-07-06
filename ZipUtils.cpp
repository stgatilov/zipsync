#include "ZipUtils.h"
#include "Utils.h"


namespace ZipSync {

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
    ZipSyncAssertF(get(), "Failed to open zip file \"%s\"", path);
}

ZipFileHolder::~ZipFileHolder()
{}
ZipFileHolder::ZipFileHolder(zipFile zf)
    : ZipFileUniquePtr(zf, zipCloseNoComment) 
{}
ZipFileHolder::ZipFileHolder(const char *path)
    : ZipFileUniquePtr(zipOpen(path, 0), zipCloseNoComment)
{
    ZipSyncAssertF(get(), "Failed to open zip file \"%s\"", path);
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

bool unzLocateFileAtBytes(unzFile zf, const char *filename, uint32_t from, uint32_t to) {
    SAFE_CALL(unzGoToFirstFile(zf));
    while (1) {
        char currFilename[SIZE_PATH];
        SAFE_CALL(unzGetCurrentFileInfo(zf, NULL, currFilename, sizeof(currFilename), NULL, 0, NULL, 0));
        if (strcmp(filename, currFilename) == 0) {
            uint32_t currFrom, currTo;
            unzGetCurrentFilePosition(zf, &currFrom, NULL, &currTo);
            if (currFrom == from && currTo == to)
                return true;    //hit
            //miss: only happens if two files have same name (e.g. tdm_update_2.06_to_2.07.zip)
            currFrom = currFrom; //noop
        }
        int res = unzGoToNextFile(zf);
        if (res == UNZ_END_OF_LIST_OF_FILE)
            return false;   //not found
        SAFE_CALL(res);
    }
}


int CompressionLevelFromGpFlags(int flags) {
    int compressionLevel = Z_DEFAULT_COMPRESSION;
    if (flags == 2)
        compressionLevel = Z_BEST_COMPRESSION;  //minizip: 8,9
    if (flags == 4)
        compressionLevel = 2;                   //minizip: 2
    if (flags == 6)
        compressionLevel = Z_BEST_SPEED;        //minizip: 1
    return compressionLevel;
}
void minizipCopyFile(unzFile zf, zipFile zfOut, const char *filename, int method, int flags, uint16_t internalAttribs, uint32_t externalAttribs, uint32_t dosDate, bool copyRaw, uint32_t crc, uint32_t contentsSize) {
    //copy provided file data into target file
    SAFE_CALL(unzOpenCurrentFile2(zf, NULL, NULL, copyRaw));
    zip_fileinfo info;
    info.internal_fa = internalAttribs;
    info.external_fa = externalAttribs;
    info.dosDate = dosDate;
    int level = CompressionLevelFromGpFlags(flags);
    SAFE_CALL(zipOpenNewFileInZip2(zfOut, filename, &info, NULL, 0, NULL, 0, NULL, method, level, copyRaw));
    while (1) {
        char buffer[SIZE_FILEBUFFER];
        int bytes = unzReadCurrentFile(zf, buffer, sizeof(buffer));
        if (bytes < 0)
            SAFE_CALL(bytes);
        if (bytes == 0)
            break;
        SAFE_CALL(zipWriteInFileInZip(zfOut, buffer, bytes));
    }
    SAFE_CALL(copyRaw ? zipCloseFileInZipRaw(zfOut, contentsSize, crc) : zipCloseFileInZip(zfOut));
    SAFE_CALL(unzCloseCurrentFile(zf));
}

}
