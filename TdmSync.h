#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <map>

namespace TdmSync {

typedef std::vector<std::pair<std::string, std::string>> IniSect;
typedef std::vector<std::pair<std::string, IniSect>> IniData;
void WriteIniFile(const char *path, const IniData &data);
IniData ReadIniFile(const char *path);

template<class T> void AppendVector(std::vector<T> &dst, const std::vector<T> &src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

/**
 * The main hash digest used for all files.
 * If two files have same hash value, then they are considered equal (no check required).
 * Thus, a reliable cryptographic hash must be used.
 */
struct HashDigest {
    //256-bit BLAKE2s hash  (TODO: use [p]arallel flavor?)
    uint8_t data[32];

    bool operator< (const HashDigest &other) const;
    std::string Hex() const;
    void Parse(const char *hex);
};

/**
 * File path or http URL in both absolute and relative format.
 */
struct PathAR {
    std::string abs;
    std::string rel;

    static bool IsHttp(const std::string &path);
    bool IsUrl() const { return IsHttp(abs); }

    static PathAR FromAbs(std::string absPath, std::string rootDir);
    static PathAR FromRel(std::string relPath, std::string rootDir);
};

/**
 * A type of location of a provided file.
 */
enum class ProvidingLocation {
    Inplace,        //local zip file in the place where it should be
    Local,          //local zip file (e.g. inside local cache of old versions)
    RemoteHttp,     //file remotely available via HTTP 1.1+
};

/**
 * Metainformation about a provided file.
 * All of this can be quickly deduced from "providing manifest"
 * without having to download the actual provided files.
 */
struct ProvidedFile {
    //hash of the contents of uncompressed file
    HashDigest contentsHash;
    //hash of the compressed file
    //note: local file header EXcluded
    HashDigest compressedHash;

    //type of file: local/remote
    ProvidingLocation location;
    //file/url path to the zip archive containing the file
    PathAR zipPath;
    //range of bytes in the zip representing the file
    //note: local file header INcluded
    uint32_t byterange[2];

    //filename inside zip (for ordering/debugging)
    std::string filename;

    static bool IsLess_Ini(const ProvidedFile &a, const ProvidedFile &b);
};

/**
 * Metainformation about a file which must be present according to selected target package.
 * All this information must be prepared in packaging process and saved into "target manifest".
 */
struct TargetFile {
    //hash of the contents of uncompressed file
    HashDigest contentsHash;
    //hash of the compressed file
    //note: local file header EXcluded
    HashDigest compressedHash;

    //path to the zip archive (i.e. where it must be)
    PathAR zipPath;

    //name of the target package it belongs to
    //"target package" = a set of files which must be installed (several packages may be chosen)
    std::string packageName;

    //(contents of zip file local header follows)
    //  local file header signature     4 bytes  (spec: 0x04034b50)
	//  version needed to extract       2 bytes  (minizip: 20 --- NO zip64!)
	//  general purpose bit flag        2 bytes  ???
	//  compression method              2 bytes  ???
	//  last mod file time              2 bytes  ???
	//  last mod file date              2 bytes  ???
	//  crc-32                          4 bytes  (defined from contents)
	//  compressed size                 4 bytes  (defined from contents)
	//  uncompressed size               4 bytes  (defined from contents)
	//  filename length                 2 bytes  ???
	//  extra field length              2 bytes  (minizip: 0)
    //  
	//  filename (variable size)                 ???
	//  extra field (variable size)              (minizip: empty)

    //filename inside zip
    std::string flhFilename;
    //last modification time in DOS format
    uint32_t flhLastModTime;
    //compression method
    uint16_t flhCompressionMethod;
    //compression settings for DEFLATE algorithm
    uint16_t flhGeneralPurposeBitFlag;

    //CRC-32 of uncompressed file contents
    uint32_t flhCrc32;
    //size of compressed file
    //note: local file header EXcluded
    uint32_t flhCompressedSize;
    //size of uncompressed file
    uint32_t flhContentsSize;

    static bool IsLess_Ini(const TargetFile &a, const TargetFile &b);
};

/**
 * "Providing manifest" describes a set of files available.
 * The sync algorithm can soak any number of providing manifests.
 * Update is possible if all of them together cover the requirements of the selected target packages.
 * Example: we can create a providing manifest for a TDM's "differential update package".
 */
class ProvidingManifest {
    //arbitrary text attached to the manifest (only for debugging)
    std::string comment;

    //the set of files declared available by this manifest
    std::vector<ProvidedFile> files;

public:
    const std::string &GetComment() const { return comment; }
    void SetComment(const std::string &text) { comment = text; }

    void Clear() { files.clear(); }
    void AppendFile(const ProvidedFile &file) { files.push_back(file); }
    void AppendManifest(const ProvidingManifest &other) { AppendVector(files, other.files); }
    void AppendLocalZip(const std::string &zipPath, const std::string &rootDir);

    const ProvidedFile &FindFile(const HashDigest &hash, bool compressed = false) const;    //???

    void ReadFromIni(const IniData &data, const std::string &rootDir);
    IniData WriteToIni() const;
};

/**
 * "Target manifest" describes one or several target packages.
 * Examples:
 *   TDM 2.06 has single target manifest file (describes assets/exe packages)
 *   user selects the set of packages to install --- it may be considered a target manifest too
 */
class TargetManifest {
    //arbitrary text attached to the manifest (only for debugging)
    std::string comment;
    //set of files described in the manifest
    std::vector<TargetFile> files;

public:
    const std::string &GetComment() const { return comment; }
    void SetComment(const std::string &text) { comment = text; }

    void Clear() { files.clear(); }
    void AppendFile(const TargetFile &file) { files.push_back(file); }
    void AppendLocalZip(const std::string &zipPath, const std::string &rootDir, const std::string &packageName);
    void AppendManifest(const TargetManifest &other) { AppendVector(files, other.files); }

    void ReadFromIni(const IniData &data, const std::string &rootDir);
    IniData WriteToIni() const;
};

void AppendManifestsFromLocalZip(
    const std::string &zipPath, const std::string &rootDir,             //path to local zip (both absolute?)
    ProvidingLocation location,                                         //for providing manifest
    const std::string &packageName,                                     //for target manifest
    ProvidingManifest &providMani, TargetManifest &targetMani           //outputs
);

enum class UpdateType {
    SameContents,       //uncompressed contents of every file must match (and compression settings too)
    SameCompressed,     //compressed contents and local file header must be bitwise the same
};

/**
 * Represents the local updater cache (physically present near installation).
 * Usually it contains:
 *   1. old files which are no longer used, but may be helpful to return back to previous version
 *   2. providing manifest for all the files in from p.1
 *   3. various manifests from remote places --- to avoid downloading them again
 */
class LocalCache {
    //TODO
};

/**
 * Represents the whole updating process.
 */
class UpdateProcess {
    //the target manifest being the goal of this update
    TargetManifest targetMani;
    //the providing manifest showing the current state of installation
    //note: it is changed during the update process
    ProvidingManifest providingMani;
    //the root directory of installation being updated
    //all target files zip paths are treated relative to it
    std::string rootDir;

    //the temporary directory where all the downloaded stuff goes
    std::string downloadDir;
    //which type of "sameness" we want to achieve
    UpdateType updateType;

    //set of target files which cannot be obtained from provided files
    //if not empty, then doing the update is impossible
    std::vector<const TargetFile*> unavailableFiles;

    typedef std::pair<const TargetFile*, const ProvidedFile*> Correspondence;
    //the best matching provided file for every target file
    std::vector<Correspondence> matches;

public:
    //must be called prior to any usage of an instance
    void Init(TargetManifest &&targetMani, ProvidingManifest &&providingMani, const std::string &rootDir);

    //decide how to execute the update (which files to find where)
    bool DevelopPlan(UpdateType type);

    //TODO: parallel / with iterations?
    void DownloadRemoteFiles(const std::string &downloadDir);

    //TODO: parallel / with iterations / overlapped with previous?
    void RepackZips();

    //TODO: parallel / with iterations / overlapped with previous?
    void RemoveOldZips(const LocalCache *cache);
};

}
