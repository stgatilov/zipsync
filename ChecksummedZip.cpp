#include "ChecksummedZip.h"
#include "ZipUtils.h"
#include "Utils.h"
#include "Hash.h"
#include "Downloader.h"


namespace ZipSync {

static const char *HASH_FILENAME = "hash.txt";
static const char *HASH_PREFIX = "zsMH:";

void WriteChecksummedZip(const char *zipPath, const void *data, uint32_t size, const char *dataFilename) {
    std::string hash = HASH_PREFIX + Hasher().Update(data, size).Finalize().Hex();

    ZipFileHolder zf(zipPath);
    zip_fileinfo info = {0};
    info.dosDate = 0x28210000;  //1 January 2000 --- set it just to make date valid

    SAFE_CALL(zipOpenNewFileInZip(zf, HASH_FILENAME, &info, NULL, 0, NULL, 0, NULL, Z_NO_COMPRESSION, Z_NO_COMPRESSION));
    SAFE_CALL(zipWriteInFileInZip(zf, hash.data(), hash.size()));
    SAFE_CALL(zipCloseFileInZip(zf));

    SAFE_CALL(zipOpenNewFileInZip(zf, dataFilename, &info, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_COMPRESSION));
    SAFE_CALL(zipWriteInFileInZip(zf, data, size));
    SAFE_CALL(zipCloseFileInZip(zf));
}

HashDigest GetHashOfChecksummedZip(const char *zipPath) {
    static const int EXPECTED_HASHFILE_SIZE = strlen(HASH_PREFIX) + HashDigest().Hex().size();

    UnzFileHolder zf(zipPath);
    SAFE_CALL(unzLocateFile(zf, "hash.txt", true));

    unz_file_info info;
    SAFE_CALL(unzGetCurrentFileInfo(zf, &info, NULL, 0, NULL, 0, NULL, 0));
    ZipSyncAssert(info.uncompressed_size == EXPECTED_HASHFILE_SIZE);

    std::vector<char> text(EXPECTED_HASHFILE_SIZE);
    SAFE_CALL(unzOpenCurrentFile(zf));
    int read = unzReadCurrentFile(zf, text.data(), text.size());
    ZipSyncAssert(read == EXPECTED_HASHFILE_SIZE);
    SAFE_CALL(unzCloseCurrentFile(zf));

    ZipSyncAssert(memcmp(text.data(), HASH_PREFIX, strlen(HASH_PREFIX)) == 0);
    HashDigest hash;
    hash.Parse(text.data() + strlen(HASH_PREFIX));
    return hash;
}

std::vector<uint8_t> ReadChecksummedZip(const char *zipPath, const char *dataFilename) {
    HashDigest expectedHash = GetHashOfChecksummedZip(zipPath);

    UnzFileHolder zf(zipPath);
    SAFE_CALL(unzLocateFile(zf, dataFilename, true));

    unz_file_info info;
    SAFE_CALL(unzGetCurrentFileInfo(zf, &info, NULL, 0, NULL, 0, NULL, 0));
    SAFE_CALL(unzOpenCurrentFile(zf));
    std::vector<uint8_t> data(info.uncompressed_size);
    int read = unzReadCurrentFile(zf, data.data(), data.size());
    ZipSyncAssert(read == data.size());
    SAFE_CALL(unzCloseCurrentFile(zf));

    HashDigest obtainedHash = Hasher().Update(data.data(), data.size()).Finalize();
    ZipSyncAssert(expectedHash == obtainedHash);

    return data;
}

std::vector<HashDigest> GetHashesOfRemoteChecksummedZips(Downloader &downloader, const std::vector<std::string> &urls) {
    int n = urls.size();

    //download a few bytes at start of each remote file
    std::vector<std::vector<char>> startData(n);
    static const int DOWNLOADED_BYTES = 128;    //107 bytes is enough
    for (int i = 0; i < n; i++) {
        auto callback = [&startData,i](const void *data, uint32_t size) -> void {
            ZipSyncAssert(size == DOWNLOADED_BYTES);
            startData[i].assign((char*)data, (char*)data + size);
        };
        downloader.EnqueueDownload(DownloadSource(urls[i], 0, DOWNLOADED_BYTES), callback);
    }
    downloader.DownloadAll();

    //find hash of remote files
    std::vector<HashDigest> remoteHashes(n);
    for (int i = 0; i < n; i++) {
        std::string bytes(startData[i].begin(), startData[i].end());
        int pos = (int)bytes.find(HASH_PREFIX);
        if (pos < 0 || pos + HashDigest().Hex().size() >= bytes.size())
            continue;
        std::string hex = bytes.substr(pos, HashDigest().Hex().size());
        bool bad = false;
        for (char c : hex)
            if (!(isdigit(c) || c >= 'a' && c <= 'f'))
                bad = true;
        if (bad)
            continue;
        remoteHashes[i].Parse(hex.c_str());
    }

    return remoteHashes;
}

std::vector<int> DownloadChecksummedZips(Downloader &downloader,
    const std::vector<std::string> &urls, const std::vector<HashDigest> &remoteHashes,
    const std::vector<std::string> &cachedZipPaths,
    std::vector<std::string> &outputPaths
) {
    int n = urls.size();
    ZipSyncAssert(remoteHashes.size() == n);
    ZipSyncAssert(outputPaths.size() == n);
    int m = cachedZipPaths.size();

    //partly read cached zips to obtain their hashes
    std::vector<HashDigest> cachedHashes(m);
    for (int i = 0; i < m; i++)
        cachedHashes[i] = GetHashOfChecksummedZip(cachedZipPaths[i].c_str());

    //detect which zips are already available locally
    std::vector<int> matching(n, -1);
    for (int i = 0; i < n; i++) {
        if (remoteHashes[i] == HashDigest())
            continue;       //hash not available
        for (int j = 0; j < m; j++)
            if (remoteHashes[i] == cachedHashes[j]) {
                matching[i] = j;
                outputPaths[i] = cachedZipPaths[j];
                break;
            }
    }

    //download all the rest of zips
    std::vector<StdioFileHolder> fileHandles;
    for (int i = 0; i < n; i++) {
        fileHandles.emplace_back(nullptr);
        if (matching[i] >= 0)
            continue;

        fileHandles[i] = StdioFileHolder(outputPaths[i].c_str(), "wb");
        auto callback = [&fileHandles,i](const void *data, uint32_t size) -> void {
            int wr = fwrite(data, 1, size, fileHandles[i]);
            ZipSyncAssert(wr == size);
        };
        downloader.EnqueueDownload(urls[i], callback);
    }
    downloader.DownloadAll();
    fileHandles.clear();

    return matching;
}

}
