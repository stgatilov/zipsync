#include "TdmSync.h"
#include "StdFilesystem.h"
#include "StdString.h"
#include <algorithm>
#include <map>
#include <set>
#include "tsassert.h"

#include "blake2.h"

#include "unzip.h"
#include "zip.h"
#include "minizip_extra.h"

//(only for viewing in debugger)
#include "minizip_private.h"

namespace fs = stdext;

static const int SIZE_PATH = 4<<10;
static const int SIZE_FILEBUFFER = 64<<10;
static const int SIZE_LINEBUFFER = 16<<10;


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
    char buffer[SIZE_LINEBUFFER];
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
std::string GetFullPath(const std::string &zipPath, const std::string &filename) {
    return zipPath + "||" + filename;
}
void ParseFullPath(const std::string &fullPath, std::string &zipPath, std::string &filename) {
    size_t pos = fullPath.find("||");
    TdmSyncAssertF(pos != std::string::npos, "Cannot split fullname into zip path and filename: %s", fullPath.c_str());
    zipPath = fullPath.substr(0, pos);
    filename = fullPath.substr(pos + 2);
}

std::string PathAR::PrefixFile(std::string absPath, std::string prefix) {
    size_t pos = absPath.find_last_of('/');
    if (pos != std::string::npos)
        pos++;
    else
        pos = 0;
    absPath.insert(absPath.begin() + pos, prefix.begin(), prefix.end());
    return absPath;
}


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

//sets all properties except for:
//  PT: "zipPath"
//  P: "location"
//  T: "package"
//  PT: "contentsHash" (if hashContents = false)
//  PT: "compressedHash" (if hashCompressed = false)
void AnalyzeCurrentFile(unzFile zf, ProvidedFile &provided, TargetFile &target, bool hashContents = true, bool hashCompressed = true) {
    char filename[SIZE_PATH];
    unz_file_info info;
    SAFE_CALL(unzGetCurrentFileInfo(zf, &info, filename, sizeof(filename), NULL, 0, NULL, 0));

    TdmSyncAssertF(info.version == 0, "File %s has made-by version %d (not supported)", filename, info.version);
    TdmSyncAssertF(info.version_needed == 20, "File %s needs zip version %d (not supported)", filename, info.version_needed);
    TdmSyncAssertF((info.flag & 0x08) == 0, "File %s has data descriptor (not supported)", filename);
    TdmSyncAssertF((info.flag & 0x01) == 0, "File %s is encrypted (not supported)", filename);
    TdmSyncAssertF((info.flag & (~0x06)) == 0, "File %s has flags %d (not supported)", filename, info.flag);
    TdmSyncAssertF(info.compression_method == 0 || info.compression_method == 8, "File %s has compression %d (not supported)", filename, info.compression_method);
    TdmSyncAssertF(info.size_file_extra == 0, "File %s has extra field in header (not supported)", filename);
    TdmSyncAssertF(info.size_file_comment == 0, "File %s has comment in header (not supported)", filename);
    TdmSyncAssertF(info.disk_num_start == 0, "File %s has disk nonzero number (not supported)", filename);
    //TODO: check that extra field is empty in local file header?...

    target.filename = provided.filename = filename;
    target.fhCrc32 = info.crc;
    target.fhCompressedSize = info.compressed_size;
    target.fhContentsSize = info.uncompressed_size;
    target.fhCompressionMethod = info.compression_method;
    target.fhGeneralPurposeBitFlag = info.flag;
    target.fhLastModTime = info.dosDate;
    target.fhInternalAttribs = info.internal_fa;
    target.fhExternalAttribs = info.external_fa;
    unzGetCurrentFilePosition(zf, &provided.byterange[0], NULL, &provided.byterange[1]);

    for (int mode = 0; mode < 2; mode++) {
        if (!(mode == 0 ? hashContents : hashCompressed))
            continue;
        SAFE_CALL(unzOpenCurrentFile2(zf, NULL, NULL, !mode));

        blake2s_state blake;
        blake2s_init(&blake, sizeof(HashDigest));
        char buffer[SIZE_FILEBUFFER];
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

        if (mode == 0) {
            TdmSyncAssertF(processedBytes == target.fhCompressedSize, "File %s has wrong compressed size: %d instead of %d", filename, target.fhCompressedSize, processedBytes);
            target.compressedHash = provided.compressedHash = cmpHash;
        }
        else {
            TdmSyncAssertF(processedBytes == target.fhContentsSize, "File %s has wrong uncompressed size: %d instead of %d", filename, target.fhContentsSize, processedBytes);
            target.contentsHash = provided.contentsHash = cmpHash;
        }
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
        pf.zipPath = tf.zipPath = zipPath;
        pf.location = location;
        tf.package = packageName;

        AnalyzeCurrentFile(zf, pf, tf);

        providMani.AppendFile(pf);
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


bool ProvidedFile::IsLess_ZipFn(const ProvidedFile &a, const ProvidedFile &b) {
    return std::tie(a.zipPath.rel, a.filename, a.contentsHash) < std::tie(b.zipPath.rel, b.filename, b.contentsHash);
}
bool TargetFile::IsLess_ZipFn(const TargetFile &a, const TargetFile &b) {
    return std::tie(a.zipPath.rel, a.filename, a.contentsHash) < std::tie(b.zipPath.rel, b.filename, b.contentsHash);
}
bool ProvidedFile::IsLess_Ini(const ProvidedFile &a, const ProvidedFile &b) {
    return IsLess_ZipFn(a, b);
}
bool TargetFile::IsLess_Ini(const TargetFile &a, const TargetFile &b) {
    return std::tie(a.package, a.zipPath.rel, a.filename, a.contentsHash) < std::tie(b.package, b.zipPath.rel, b.filename, b.contentsHash);
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

        std::string secName = "File " + GetFullPath(pf->zipPath.rel, pf->filename);
        ini.push_back(std::make_pair(secName, std::move(section)));
    }

    return ini;
}
void ProvidingManifest::ReadFromIni(const IniData &data, const std::string &rootDir) {
    ProvidingLocation location = (PathAR::IsHttp(rootDir) ? ProvidingLocation::RemoteHttp : ProvidingLocation::Local);

    for (const auto &pNS : data) {
        ProvidedFile pf;
        pf.location = location;

        std::string name = pNS.first;
        if (!stdext::starts_with(name, "File "))
            continue;
        name = name.substr(5);

        ParseFullPath(name, pf.zipPath.rel, pf.filename);
        pf.zipPath = PathAR::FromRel(pf.zipPath.rel, rootDir);

        std::map<std::string, std::string> dict(pNS.second.begin(), pNS.second.end());
        pf.contentsHash.Parse(dict.at("contentsHash").c_str());
        pf.compressedHash.Parse(dict.at("compressedHash").c_str());

        std::string byterange = dict.at("byterange");
        size_t pos = byterange.find('-');
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
        section.push_back(std::make_pair("package", tf->package));
        section.push_back(std::make_pair("contentsHash", tf->contentsHash.Hex()));
        section.push_back(std::make_pair("compressedHash", tf->compressedHash.Hex()));
        section.push_back(std::make_pair("crc32", std::to_string(tf->fhCrc32)));
        section.push_back(std::make_pair("lastModTime", std::to_string(tf->fhLastModTime)));
        section.push_back(std::make_pair("compressionMethod", std::to_string(tf->fhCompressionMethod)));
        section.push_back(std::make_pair("gpbitFlag", std::to_string(tf->fhGeneralPurposeBitFlag)));
        section.push_back(std::make_pair("compressedSize", std::to_string(tf->fhCompressedSize)));
        section.push_back(std::make_pair("contentsSize", std::to_string(tf->fhContentsSize)));
        section.push_back(std::make_pair("internalAttribs", std::to_string(tf->fhInternalAttribs)));
        section.push_back(std::make_pair("externalAttribs", std::to_string(tf->fhExternalAttribs)));

        std::string secName = "File " + GetFullPath(tf->zipPath.rel, tf->filename);
        ini.push_back(std::make_pair(secName, std::move(section)));
    }

    return ini;
}
void TargetManifest::ReadFromIni(const IniData &data, const std::string &rootDir) {
    for (const auto &pNS : data) {
        TargetFile tf;

        std::string name = pNS.first;
        if (!stdext::starts_with(name, "File "))
            continue;
        name = name.substr(5);

        ParseFullPath(name, tf.zipPath.rel, tf.filename);
        tf.zipPath = PathAR::FromRel(tf.zipPath.rel, rootDir);

        std::map<std::string, std::string> dict(pNS.second.begin(), pNS.second.end());
        tf.package = dict.at("package");
        tf.contentsHash.Parse(dict.at("contentsHash").c_str());
        tf.compressedHash.Parse(dict.at("compressedHash").c_str());

        tf.fhLastModTime = std::stoul(dict.at("lastModTime"));
        tf.fhCompressionMethod = std::stoul(dict.at("compressionMethod"));
        tf.fhGeneralPurposeBitFlag = std::stoul(dict.at("gpbitFlag"));
        tf.fhCompressedSize = std::stoul(dict.at("compressedSize"));
        tf.fhContentsSize = std::stoul(dict.at("contentsSize"));
        tf.fhInternalAttribs = std::stoul(dict.at("internalAttribs"));
        tf.fhExternalAttribs = std::stoul(dict.at("externalAttribs"));

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

    updateType = (UpdateType)0xDDDDDDDD;
    matches.clear();
}

bool UpdateProcess::DevelopPlan(UpdateType type) {
    updateType = type;

    //build index of target files: by zip path + file path inside zip
    std::map<std::string, const TargetFile*> pathToTarget;
    for (int i = 0; i < targetMani.size(); i++) {
        const TargetFile &tf = targetMani[i];
        std::string fullPath = GetFullPath(tf.zipPath.abs, tf.filename);
        auto pib = pathToTarget.insert(std::make_pair(fullPath, &tf));
        TdmSyncAssertF(pib.second, "Duplicate target file at place %s", fullPath.c_str());
    }

    //find providing files which are already in-place
    for (int i = 0; i < providingMani.size(); i++) {
        ProvidedFile &pf = providingMani[i];
        if (pf.location != ProvidingLocation::Local)
            continue;
        std::string fullPath = GetFullPath(pf.zipPath.abs, pf.filename);
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

void UpdateProcess::ValidateFile(const TargetFile &want, const TargetFile &have) const {
    std::string fullPath = GetFullPath(have.zipPath.abs, have.filename);
    //zipPath is different while repacking
    //package does not need to be checked
    TdmSyncAssertF(want.filename == have.filename, "Wrong filename of %s after repack: need %s", fullPath.c_str(), want.filename.c_str());
    TdmSyncAssertF(want.contentsHash == have.contentsHash, "Wrong contents hash of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhContentsSize == have.fhContentsSize, "Wrong contents size of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhCrc32 == have.fhCrc32, "Wrong crc32 of %s after repack", fullPath.c_str());
    if (updateType == UpdateType::SameCompressed) {
        TdmSyncAssertF(want.compressedHash == have.compressedHash, "Wrong compressed hash of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhCompressedSize == have.fhCompressedSize, "Wrong compressed size of %s after repack", fullPath.c_str());
    }
    TdmSyncAssertF(want.fhCompressionMethod == have.fhCompressionMethod, "Wrong compression method of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhGeneralPurposeBitFlag == have.fhGeneralPurposeBitFlag, "Wrong flags of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhLastModTime == have.fhLastModTime, "Wrong modification time of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhInternalAttribs == have.fhInternalAttribs, "Wrong internal attribs of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhExternalAttribs == have.fhExternalAttribs, "Wrong external attribs of %s after repack", fullPath.c_str());
}

void UpdateProcess::RepackZips() {
    //verify that we are ready to do repacking
    TdmSyncAssertF(matches.size() == targetMani.size(), "RepackZips: DevelopPlan not called yet");
    for (Match m : matches) {
        std::string fullPath = GetFullPath(m.target->zipPath.abs, m.target->filename);
        TdmSyncAssertF(m.provided, "RepackZips: target file %s is not provided", fullPath.c_str());
        TdmSyncAssertF(m.provided->location == ProvidingLocation::Inplace || m.provided->location == ProvidingLocation::Local, "RepackZips: target file %s is not available locally", fullPath.c_str());
    }

    //group target files by their zips
    std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        return TargetFile::IsLess_ZipFn(*a.target, *b.target);
    });
    std::map<std::string, std::vector<int>> zipToMatchIds;      //for every zip file: indices of all matches with target in it
    for (int i = 0; i < matches.size(); i++) {
        const std::string &zipPath = matches[i].target->zipPath.abs;
        zipToMatchIds[zipPath].push_back(i);
    }

    //check which target zips need no change at all
    std::map<std::string, std::vector<const ProvidedFile*>> zipToProvided;
    for (int i = 0; i < providingMani.size(); i++) {
        zipToProvided[providingMani[i].zipPath.abs].push_back(&providingMani[i]);
    }
    std::vector<std::string> zipsDontChange;
    for (const auto &pZV : zipToMatchIds) {
        const std::string &zipPath = pZV.first;
        const std::vector<int> &matchIds = pZV.second;
        int cntInplace = matchIds.size();
        for (int midx : matchIds) {
            Match m = matches[midx];
            if (m.provided->location != ProvidingLocation::Inplace)
                cntInplace--;
        }
        int cntProvided = 0;
        auto iter = zipToProvided.find(zipPath);
        if (iter != zipToProvided.end())
            cntProvided = iter->second.size();
        if (cntInplace == matchIds.size() && cntInplace == cntProvided)
            zipsDontChange.push_back(zipPath);
    }
    //remove such zips from our todo list
    for (const std::string zipfn : zipsDontChange)
        zipToMatchIds.erase(zipfn);


    //iterate over all zips and repack them
    std::map<int, ProvidedFile> matchIdToRepacked;
    for (const auto &pZV : zipToMatchIds) {
        const std::string &zipPath = pZV.first;
        const std::vector<int> &matchIds = pZV.second;

        //create new zip archive (it will contain the results of repacking)
        std::string zipPathOut = PathAR::PrefixFile(zipPath, "__new__");
        zipFileHolder zfOut(zipPathOut.c_str());

        //copy all files one-by-one
        std::map<int, bool> copiedRaw;
        for (int midx : matchIds) {
            Match m = matches[midx];

            //open provided file for reading (TODO: optimize?)
            unzFileHolder zf(m.provided->zipPath.abs.c_str());
            SAFE_CALL(unzGoToFirstFile(zf));
            while (1) {
                char filename[SIZE_PATH];
                SAFE_CALL(unzGetCurrentFileInfo(zf, NULL, filename, sizeof(filename), NULL, 0, NULL, 0));
                if (strcmp(filename, m.provided->filename.c_str()) == 0) {
                    uint32_t from, to;
                    unzGetCurrentFilePosition(zf, &from, NULL, &to);
                    if (from == m.provided->byterange[0] && to == m.provided->byterange[1])
                        break;  //hit
                    //miss: only happens if two files have same name (e.g. tdm_update_2.06_to_2.07.zip)
                    from = from; //noop
                }
                SAFE_CALL(unzGoToNextFile(zf));
            }
            unz_file_info info;
            SAFE_CALL(unzGetCurrentFileInfo(zf, &info, NULL, 0, NULL, 0, NULL, 0));

            //can we avoid recompressing the file?
            bool useRaw = false;
            if (m.provided->compressedHash == m.target->compressedHash)
                useRaw = true;  //bitwise same
            if (updateType == UpdateType::SameContents && m.target->fhCompressionMethod == info.compression_method && m.target->fhGeneralPurposeBitFlag == info.flag)
                useRaw = true;  //same compression level

            //prepare metadata for target file
            zip_fileinfo infoOut;
            infoOut.dosDate = m.target->fhLastModTime;
            infoOut.internal_fa = m.target->fhInternalAttribs;
            infoOut.external_fa = m.target->fhExternalAttribs;
            int compressionLevel = Z_DEFAULT_COMPRESSION;
            if (m.target->fhGeneralPurposeBitFlag == 2)
                compressionLevel = Z_BEST_COMPRESSION;  //minizip: 8,9
            if (m.target->fhGeneralPurposeBitFlag == 4)
                compressionLevel = 2;                   //minizip: 2
            if (m.target->fhGeneralPurposeBitFlag == 6)
                compressionLevel = Z_BEST_SPEED;        //minizip: 1

            //copy provided file data into target file
            SAFE_CALL(unzOpenCurrentFile2(zf, NULL, NULL, useRaw));
            SAFE_CALL(zipOpenNewFileInZip2(zfOut, m.target->filename.c_str(), &infoOut, NULL, 0, NULL, 0, NULL, m.target->fhCompressionMethod, compressionLevel, useRaw));
            //TODO: should we check BLAKE hash here?
            while (1) {
                char buffer[SIZE_FILEBUFFER];
                int bytes = unzReadCurrentFile(zf, buffer, sizeof(buffer));
                if (bytes < 0)
                    SAFE_CALL(bytes);
                if (bytes == 0)
                    break;
                SAFE_CALL(zipWriteInFileInZip(zfOut, buffer, bytes));
            }
            SAFE_CALL(useRaw ? zipCloseFileInZipRaw(zfOut, m.target->fhContentsSize, m.target->fhCrc32) : zipCloseFileInZip(zfOut));
            SAFE_CALL(unzCloseCurrentFile(zf));

            copiedRaw[midx] = useRaw;
        }
        //flush and close new zip
        zfOut.reset();

#if 1
        //analyze the repacked file (fast)
        unzFileHolder zf(zipPathOut.c_str());
        SAFE_CALL(unzGoToFirstFile(zf));
        for (int i = 0; i < matchIds.size(); i++) {
            int midx = matchIds[i];
            Match m = matches[midx];
            if (i > 0) SAFE_CALL(unzGoToNextFile(zf));

            bool needsRehashCompressed = !copiedRaw[midx];
            TargetFile targetNew;
            ProvidedFile providedNew;
            providedNew.zipPath = targetNew.zipPath = PathAR::FromAbs(zipPathOut, rootDir);
            providedNew.location = ProvidingLocation::Local;
            targetNew.package = "[repacked]";
            providedNew.contentsHash = targetNew.contentsHash = m.target->contentsHash;
            providedNew.compressedHash = targetNew.compressedHash = m.target->compressedHash;   //will be recomputed if needsRehashCompressed
            AnalyzeCurrentFile(zf, providedNew, targetNew, false, needsRehashCompressed);

            ValidateFile(*m.target, targetNew);
            matchIdToRepacked[midx] = providedNew;
        }
        zf.reset();
#else
        //analyze the repacked file (slow)
        ProvidingManifest newProvidingMani;
        TargetManifest newTargetMani;
        AppendManifestsFromLocalZip(zipPathOut, rootDir, ProvidingLocation::Local, "[repacked]", newProvidingMani, newTargetMani);
        for (int i = 0; i < matchIds.size(); i++) {
            int midx = matchIds[i];
            Match m = matches[midx];

            ValidateFile(*m.target, newTargetMani[i]);
            //remember that where this match was repacked to
            TdmSyncAssert(newProvidingMani[i].filename == newTargetMani[i].filename);  //by construction
            matchIdToRepacked[midx] = newProvidingMani[i];
        }
#endif

        //TODO: remove?
    }

    /*//change provided file in all repacked matches
    for (const auto &pIP : matchIdToRepacked) {
        Match &m = matches[pIP.first];

    }*/

}

}
