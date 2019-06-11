#pragma once

#include <memory>
#include "tsassert.h"

#include "unzip.h"
#include "zip.h"


namespace TdmSync {

typedef std::unique_ptr<std::remove_pointer<unzFile>::type, int (*)(unzFile)> UnzFileUniquePtr;
/**
 * RAII wrapper around unzFile from minizip.h.
 * Automatically closes file on destruction.
 */
class UnzFileHolder : public UnzFileUniquePtr {
public:
    UnzFileHolder(unzFile zf);
    UnzFileHolder(const char *path);
    ~UnzFileHolder();
    operator unzFile() const { return get(); }
};

typedef std::unique_ptr<std::remove_pointer<zipFile>::type, int (*)(zipFile)> ZipFileUniquePtr;
/**
 * RAII wrapper around zipFile from minizip.h.
 * Automatically closes file on destruction.
 */
class ZipFileHolder : public ZipFileUniquePtr {
public:
    ZipFileHolder(zipFile zf);
    ZipFileHolder(const char *path);
    ~ZipFileHolder();
    operator zipFile() const { return get(); }
};


/**
 * TdmSync exception thrown when minizip function reports error.
 * Automatically throw by SAFE_CALL macro.
 */
class MinizipError : public BaseError {
public:
    MinizipError(int errcode);
    ~MinizipError();
};
/**
 * Performs whatever call you wrap into it and checks its return code.
 * If the return code is nonzero, then exception is thrown.
 */
#define SAFE_CALL(...) \
    do { \
        int mz_errcode = __VA_ARGS__; \
        if (mz_errcode != 0) throw MinizipError(mz_errcode); \
    } while (0)


//note: file must be NOT opened
void unzGetCurrentFilePosition(unzFile zf, uint32_t *localHeaderStart, uint32_t *fileDataStart, uint32_t *fileDataEnd);

}