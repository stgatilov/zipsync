#pragma once

namespace TdmSync {

static const int SIZE_PATH = 4<<10;
static const int SIZE_FILEBUFFER = 64<<10;
static const int SIZE_LINEBUFFER = 16<<10;

typedef std::unique_ptr<FILE, int (*)(FILE*)> stdioFileUniquePtr;
class stdioFileHolder : public stdioFileUniquePtr {
public:
    stdioFileHolder(FILE *f) : stdioFileUniquePtr(f, fclose) {}
    stdioFileHolder(const char *path, const char *mode) : stdioFileUniquePtr(fopen(path, mode), fclose) { TdmSyncAssertF(get(), "Failed to open file \"%s\"", path); }
    operator FILE*() const { return get(); }
};

template<class T> void AppendVector(std::vector<T> &dst, const std::vector<T> &src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

}
