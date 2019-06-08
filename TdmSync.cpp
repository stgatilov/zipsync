#include "TdmSync.h"
#include "StdFilesystem.h"
#include "StdString.h"
#include <algorithm>
#include "tsassert.h"

#include "blake2.h"

#include "unzip.h"
#include "minizip_extra.h"

//(only for viewing in debugger)
#include "minizip_private.h"

namespace fs = stdext;


namespace TdmSync {

typedef std::unique_ptr<FILE, int (*)(FILE*)> stdioFileUniquePtr;
class stdioFileHolder : public stdioFileUniquePtr {
public:
    stdioFileHolder(FILE *f) : stdioFileUniquePtr(f, fclose) {}
    stdioFileHolder(const char *path, const char *mode) : stdioFileUniquePtr(fopen(path, mode), fclose) { TdmSyncAssertF(get(), "Failed to open file \"%s\"", path); }
    operator FILE*() const { return get(); }
};


typedef std::unique_ptr<std::remove_pointer<unzFile>::type, int (*)(unzFile)> unzFileUniquePtr;
class unzFileHolder : public unzFileUniquePtr {
public:
    unzFileHolder(unzFile zf) : unzFileUniquePtr(zf, unzClose) {}
    unzFileHolder(const char *path) : unzFileUniquePtr(unzOpen(path), unzClose) { TdmSyncAssertF(get(), "Failed to open zip file \"%s\"", path); }
    operator unzFile() const { return get(); }
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


void WriteIniFile(const char *path, const IniData &data) {
    stdioFileHolder f(path, "wb");
    for (const auto &pNS : data) {
        fprintf(f, "[%s]\n", pNS.first.c_str());
        for (const auto &pKV : pNS.second)
            fprintf(f, "%s=%s\n", pKV.first.c_str(), pKV.second.c_str());
        fprintf(f, "\n");
    }
}
IniData ReadIniFile(const char *path) {
    char buffer[16<<10];
    stdioFileHolder f(path, "rb");
    IniData ini;
    IniSect sec;
    std::string name;
    auto CommitSec = [&]() {
        if (!name.empty())
            ini.push_back(std::make_pair(std::move(name), std::move(sec)));
        name.clear();
        sec.clear();
    };
    while (fgets(buffer, sizeof(buffer), f.get())) {
        std::string line = buffer;
        stdext::trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            CommitSec();
            name = line.substr(1, line.size() - 2);
        }
        else {
            size_t pos = line.find('=');
            TdmSyncAssertF(pos != std::string::npos, "Cannot parse ini line: %s", line.c_str());
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos+1);
            sec.push_back(std::make_pair(std::move(key), std::move(value)));
        }
    }
    CommitSec();
    return ini;
}

bool HashDigest::operator< (const HashDigest &other) const {
    return memcmp(data, other.data, sizeof(data)) < 0;
}
bool HashDigest::operator== (const HashDigest &other) const {
    return memcmp(data, other.data, sizeof(data)) == 0;
}
std::string HashDigest::Hex() const {
    char text[100];
    for (int i = 0; i < sizeof(data); i++)
        sprintf(text + 2*i, "%02x", data[i]);
    return text;
}
void HashDigest::Parse(const char *hex) {
    TdmSyncAssertF(strlen(hex) == 2 * sizeof(data), "Hex digest has wrong length %d", strlen(hex));
    for (int i = 0; i < sizeof(data); i++) {
        char octet[4] = {0};
        memcpy(octet, hex + 2*i, 2);
        uint32_t value;
        int k = sscanf(octet, "%02x", &value);
        TdmSyncAssertF(k == 1, "Cannot parse hex digest byte %s", octet);
        data[i] = value;
    }
}

bool PathAR::IsHttp(const std::string &path) {
    return stdext::starts_with(path, "http://");
}


static void CheckPath(const std::string &path, bool relative) {
    for (int i = 0; i < path.size(); i++)
        TdmSyncAssertF(uint8_t(path[i]) >= 32, "Non-printable character %d in path", int(path[i]));
    TdmSyncAssertF(!path.empty() && path != "/", "Empty path [%s]", path.c_str());
    TdmSyncAssertF(path.find_first_of("\\|[]=?&") == std::string::npos, "Forbidden symbol in path %s", path.c_str());
    TdmSyncAssertF(path[0] != '.', "Path must not start with dot: %s", path.c_str());
    if (relative) {
        TdmSyncAssertF(path.find_first_of(":") == std::string::npos, "Colon in relative path %s", path.c_str());
        TdmSyncAssertF(path[0] != '/', "Relative path starts with slash: %s", path.c_str())
    }

}

PathAR PathAR::FromAbs(std::string absPath, std::string rootDir) {
    CheckPath(rootDir, false);
    CheckPath(absPath, false);
    bool lastSlash = (rootDir.back() == '/');
    int len = rootDir.size() - (lastSlash ? 1 : 0);
    TdmSyncAssertF(strncmp(absPath.c_str(), rootDir.c_str(), len) == 0, "Abs path %s is not within root dir %s", absPath.c_str(), rootDir.c_str());
    TdmSyncAssertF(absPath.size() > len && absPath[len] == '/', "Abs path %s is not within root dir %s", absPath.c_str(), rootDir.c_str());
    PathAR res;
    res.abs = absPath;
    res.rel = absPath.substr(len+1);
    return res;
}
PathAR PathAR::FromRel(std::string relPath, std::string rootDir) {
    CheckPath(rootDir, false);
    CheckPath(relPath, true);
    bool lastSlash = (rootDir.back() == '/');
    PathAR res;
    res.rel = relPath;
    res.abs = rootDir + (lastSlash ? "" : "/") + relPath;
    return res;
}


void AnalyzeCurrentFile(unzFile zf, ProvidedFile &provided, TargetFile &target) {
    char filename[32768];
    unz_file_info info;
    SAFE_CALL(unzGetCurrentFileInfo(zf, &info, filename, sizeof(filename), NULL, 0, NULL, 0));
    TdmSyncAssertF(info.version_needed == 20, "File %s needs zip version %d (not supported)", filename, info.version_needed);
    TdmSyncAssertF((info.flag & 0x08) == 0, "File %s has data descriptor (not supported)", filename);
    TdmSyncAssertF(info.size_file_extra == 0, "File %s has extra field in header (not supported)", filename);

    target.flhFilename = provided.filename = filename;
    target.flhCompressedSize = info.compressed_size;
    target.flhContentsSize = info.uncompressed_size;
    target.flhCompressionMethod = info.compression_method;
    target.flhGeneralPurposeBitFlag = info.flag;
    target.flhLastModTime = info.dosDate;
    target.flhCrc32 = info.crc;

    SAFE_CALL(unzOpenCurrentFile(zf));
    int64_t pos = unzGetCurrentFileZStreamPos64(zf);
    int localHeaderSize = 30 + info.size_filename + info.size_file_extra;
    provided.byterange[0] = pos - localHeaderSize;
    provided.byterange[1] = pos + info.compressed_size;
    SAFE_CALL(unzCloseCurrentFile(zf));

    for (int mode = 0; mode < 2; mode++) {
        SAFE_CALL(unzOpenCurrentFile2(zf, NULL, NULL, !mode));

        blake2s_state blake;
        blake2s_init(&blake, sizeof(HashDigest));
        char buffer[65536];
        int processedBytes = 0;
        while (1) {
            int bytes = unzReadCurrentFile(zf, buffer, sizeof(buffer));
            if (bytes < 0)
                SAFE_CALL(bytes);
            if (bytes == 0)
                break;
            blake2s_update(&blake, buffer, bytes);
            processedBytes += bytes;
        } 
        HashDigest cmpHash;
        blake2s_final(&blake, cmpHash.data, sizeof(HashDigest));

        SAFE_CALL(unzCloseCurrentFile(zf));

        if (mode == 0)
            target.compressedHash = provided.compressedHash = cmpHash;
        else
            target.contentsHash = provided.contentsHash = cmpHash;
    }
}

void AppendManifestsFromLocalZip(
    const std::string &zipPathAbs, const std::string &rootDir,             //path to local zip (both absolute?)
    ProvidingLocation location,                                         //for providing manifest
    const std::string &packageName,                                     //for target manifest
    ProvidingManifest &providMani, TargetManifest &targetMani           //outputs
) {
    PathAR zipPath = PathAR::FromAbs(zipPathAbs, rootDir);

    unzFileHolder zf(zipPath.abs.c_str());
    TdmSyncAssertF(!unzIsZip64(zf), "Zip64 is not supported!");
    SAFE_CALL(unzGoToFirstFile(zf));
    while (1) {
        ProvidedFile pf;
        TargetFile tf;
        AnalyzeCurrentFile(zf, pf, tf);

        pf.location = location;
        pf.zipPath = zipPath;
        providMani.AppendFile(pf);
        tf.zipPath = zipPath;
        tf.packageName = packageName;
        targetMani.AppendFile(tf);

        int err = unzGoToNextFile(zf);
        if (err == UNZ_END_OF_LIST_OF_FILE)
            break;
        SAFE_CALL(err);
    }
    zf.reset();
}
void ProvidingManifest::AppendLocalZip(const std::string &zipPath, const std::string &rootDir) {
    TargetManifest temp;
    ProvidingLocation location = (PathAR::IsHttp(rootDir) ? ProvidingLocation::RemoteHttp : ProvidingLocation::Local);
    AppendManifestsFromLocalZip(zipPath, rootDir, location, "", *this, temp);
}
void TargetManifest::AppendLocalZip(const std::string &zipPath, const std::string &rootDir, const std::string &packageName) {
    ProvidingManifest temp;
    AppendManifestsFromLocalZip(zipPath, rootDir, ProvidingLocation::Inplace, packageName, temp, *this);
}


bool ProvidedFile::IsLess_Ini(const ProvidedFile &a, const ProvidedFile &b) {
    return std::tie(a.zipPath.rel, a.filename, a.contentsHash) < std::tie(b.zipPath.rel, b.filename, b.contentsHash);
}
bool TargetFile::IsLess_Ini(const TargetFile &a, const TargetFile &b) {
    return std::tie(a.packageName, a.zipPath.rel, a.flhFilename, a.contentsHash) < std::tie(b.packageName, b.zipPath.rel, b.flhFilename, b.contentsHash);
}

IniData ProvidingManifest::WriteToIni() const {
    //sort files by INI order
    std::vector<const ProvidedFile*> order;
    for (const auto &f : files)
        order.push_back(&f);
    std::sort(order.begin(), order.end(), [](auto a, auto b) {
        return ProvidedFile::IsLess_Ini(*a, *b);
    });

    IniData ini;
    for (const ProvidedFile *pf : order) {
        IniSect section;
        section.push_back(std::make_pair("contentsHash", pf->contentsHash.Hex()));
        section.push_back(std::make_pair("compressedHash", pf->compressedHash.Hex()));
        section.push_back(std::make_pair("byterange", std::to_string(pf->byterange[0]) + "-" + std::to_string(pf->byterange[1])));

        std::string secName = pf->zipPath.rel + "||" + pf->filename;
        ini.push_back(std::make_pair(secName, std::move(section)));
    }

    return ini;
}
void ProvidingManifest::ReadFromIni(const IniData &data, const std::string &rootDir) {
    ProvidingLocation location = (PathAR::IsHttp(rootDir) ? ProvidingLocation::RemoteHttp : ProvidingLocation::Local);

    for (const auto &pNS : data) {
        ProvidedFile pf;
        pf.location = location;

        const std::string &name = pNS.first;
        size_t pos = name.find("||");
        TdmSyncAssertF(pos != std::string::npos, "Section name does not specify a file: %s", name.c_str());
        pf.zipPath = PathAR::FromRel(name.substr(0, pos), rootDir);
        pf.filename = name.substr(pos + 2);

        std::map<std::string, std::string> dict(pNS.second.begin(), pNS.second.end());
        pf.contentsHash.Parse(dict.at("contentsHash").c_str());
        pf.compressedHash.Parse(dict.at("compressedHash").c_str());

        std::string byterange = dict.at("byterange");
        pos = byterange.find('-');
        TdmSyncAssertF(pos != std::string::npos, "Byterange %s has no hyphen", byterange.c_str());
        pf.byterange[0] = std::stoul(byterange.substr(0, pos));
        pf.byterange[1] = std::stoul(byterange.substr(pos+1));
        TdmSyncAssert(pf.byterange[0] < pf.byterange[1]);

        AppendFile(pf);
    }
}

IniData TargetManifest::WriteToIni() const {
    //sort files by INI order
    std::vector<const TargetFile*> order;
    for (const auto &f : files)
        order.push_back(&f);
    std::sort(order.begin(), order.end(), [](auto a, auto b) {
        return TargetFile::IsLess_Ini(*a, *b);
    });

    IniData ini;
    for (const TargetFile *tf : order) {
        IniSect section;
        section.push_back(std::make_pair("package", tf->packageName));
        section.push_back(std::make_pair("contentsHash", tf->contentsHash.Hex()));
        section.push_back(std::make_pair("compressedHash", tf->compressedHash.Hex()));
        section.push_back(std::make_pair("lastModTime", std::to_string(tf->flhLastModTime)));
        section.push_back(std::make_pair("compressionMethod", std::to_string(tf->flhCompressionMethod)));
        section.push_back(std::make_pair("gpbitFlag", std::to_string(tf->flhGeneralPurposeBitFlag)));
        section.push_back(std::make_pair("crc32", std::to_string(tf->flhCrc32)));
        section.push_back(std::make_pair("compressedSize", std::to_string(tf->flhCompressedSize)));
        section.push_back(std::make_pair("contentsSize", std::to_string(tf->flhContentsSize)));

        std::string secName = tf->zipPath.rel + "||" + tf->flhFilename;
        ini.push_back(std::make_pair(secName, std::move(section)));
    }

    return ini;
}
void TargetManifest::ReadFromIni(const IniData &data, const std::string &rootDir) {
    for (const auto &pNS : data) {
        TargetFile tf;

        const std::string &name = pNS.first;
        size_t pos = name.find("||");
        TdmSyncAssertF(pos != std::string::npos, "Section name does not specify a file: %s", name.c_str());
        tf.zipPath = PathAR::FromRel(name.substr(0, pos), rootDir);
        tf.flhFilename = name.substr(pos + 2);

        std::map<std::string, std::string> dict(pNS.second.begin(), pNS.second.end());
        tf.packageName = dict.at("package");
        tf.contentsHash.Parse(dict.at("contentsHash").c_str());
        tf.compressedHash.Parse(dict.at("compressedHash").c_str());

        tf.flhLastModTime = std::stoul(dict.at("lastModTime"));
        tf.flhCompressionMethod = std::stoul(dict.at("compressionMethod"));
        tf.flhGeneralPurposeBitFlag = std::stoul(dict.at("gpbitFlag"));
        tf.flhCrc32 = std::stoul(dict.at("crc32"));
        tf.flhCompressedSize = std::stoul(dict.at("compressedSize"));
        tf.flhContentsSize = std::stoul(dict.at("contentsSize"));

        AppendFile(tf);
    }
}

void TargetManifest::ReRoot(const std::string &rootDir) {
    for (TargetFile &tf : files) {
        tf.zipPath = PathAR::FromRel(tf.zipPath.rel, rootDir);
    }
}


void UpdateProcess::Init(TargetManifest &&targetMani_, ProvidingManifest &&providingMani_, const std::string &rootDir_) {
    targetMani = std::move(targetMani_);
    providingMani = std::move(providingMani_);
    rootDir = rootDir_;

    targetMani.ReRoot(rootDir);

    downloadDir = "__tdmsync_download";
    updateType = (UpdateType)0xDDDDDDDD;

    matches.clear();
}

bool UpdateProcess::DevelopPlan(UpdateType type) {
    updateType = type;

    //build index of target files: by zip path + file path inside zip
    std::map<std::string, const TargetFile*> pathToTarget;
    for (int i = 0; i < targetMani.size(); i++) {
        const TargetFile &tf = targetMani[i];
        std::string fullPath = tf.zipPath.abs + "||" + tf.flhFilename;
        auto pib = pathToTarget.insert(std::make_pair(fullPath, &tf));
        TdmSyncAssertF(pib.second, "Duplicate target file at place %s", fullPath.c_str());
    }

    //find providing files which are already in-place
    for (int i = 0; i < providingMani.size(); i++) {
        ProvidedFile &pf = providingMani[i];
        if (pf.location != ProvidingLocation::Local)
            continue;
        std::string fullPath = pf.zipPath.abs + "||" + pf.filename;
        auto iter = pathToTarget.find(fullPath);
        if (iter != pathToTarget.end()) {
            //give this providing file priority when choosing where to take file from
            pf.location = ProvidingLocation::Inplace;
        }
    }

    //build index of providing files (by hash on uncompressed file)
    std::map<HashDigest, std::vector<const ProvidedFile*>> pfIndex;
    for (int i = 0; i < providingMani.size(); i++) {
        const ProvidedFile &pf = providingMani[i];
        pfIndex[pf.contentsHash].push_back(&pf);
    }

    //find matching providing file for every target file
    matches.clear();
    bool fullPlan = true;
    for (int i = 0; i < targetMani.size(); i++) {
        const TargetFile &tf = targetMani[i];

        auto iter = pfIndex.find(tf.contentsHash);
        if (iter == pfIndex.end()) {
            matches.push_back(Match{&tf, nullptr});
            fullPlan = false;
            continue;
        }
        const std::vector<const ProvidedFile*> &candidates = iter->second;

        int bestScore = 1000000000;
        const ProvidedFile *bestFile = nullptr;
        for (const ProvidedFile *pf : candidates) {
            if (updateType == UpdateType::SameCompressed && !(pf->compressedHash == tf.compressedHash))
                continue;
            int score = int(pf->location);
            if (score < bestScore) {
                bestScore = score;
                bestFile = pf;
            }
        }
        matches.push_back(Match{&tf, bestFile});
    }

    return fullPlan;
}


}
