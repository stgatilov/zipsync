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
    bool operator== (const HashDigest &other) const;
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

    static std::string PrefixFile(std::string absPath, std::string prefix);
};

/**
 * A type of location of a provided file.
 */
enum class ProvidingLocation {
    Inplace = 0,    //local zip file in the place where it should be
    Local = 1,      //local zip file (e.g. inside local cache of old versions)
    RemoteHttp = 2, //file remotely available via HTTP 1.1+
    Nowhere,        //(should never be used)
};

/**
 * Metainformation about a provided file.
 * All of this can be quickly deduced from "providing manifest"
 * without having to download the actual provided files.
 */
struct ProvidedFile {
    //file/url path to the zip archive containing the file
    PathAR zipPath;
    //filename inside zip (for ordering/debugging)
    std::string filename;
    //type of file: local/remote
    ProvidingLocation location;
    //range of bytes in the zip representing the file
    //note: local file header INcluded
    uint32_t byterange[2];

    //hash of the contents of uncompressed file
    HashDigest contentsHash;
    //hash of the compressed file
    //note: local file header EXcluded
    HashDigest compressedHash;

    static bool IsLess_ZipFn(const ProvidedFile &a, const ProvidedFile &b);
    static bool IsLess_Ini(const ProvidedFile &a, const ProvidedFile &b);
};

/**
 * Metainformation about a file which must be present according to selected target package.
 * All this information must be prepared in packaging process and saved into "target manifest".
 */
struct TargetFile {
    //path to the zip archive (i.e. where it must be)
    PathAR zipPath;
    //filename inside zip (stored in file header too)
    std::string filename;
    //name of the target package it belongs to
    //"target package" = a set of files which must be installed (several packages may be chosen)
    std::string package;

    //(contents of zip file central header follows)
    //  version made by                 2 bytes  (minizip: 0)
    //  version needed to extract       2 bytes  (minizip: 20 --- NO zip64!)
    //  general purpose bit flag        2 bytes  ???  [0|2|4|6]
    //  compression method              2 bytes  ???  [0|8]
    //  last mod file time              2 bytes  ???
    //  last mod file date              2 bytes  ???
    //  crc-32                          4 bytes  (defined from contents --- checked by minizip)
    //  compressed size                 4 bytes  (defined from contents --- checked by me)
    //  uncompressed size               4 bytes  (defined from contents --- checked by me)
    //  filename length                 2 bytes  ???
    //  extra field length              2 bytes  (minizip: 0)
    //  file comment length             2 bytes  (minizip: 0)
    //  disk number start               2 bytes  (minizip: 0)
    //  internal file attributes        2 bytes  ???
    //  external file attributes        4 bytes  ???
    //  relative offset of local header 4 bytes  (dependent on file layout)
    //  filename (variable size)        ***      ???
    //  extra field (variable size)     ***      (minizip: empty)
    //  file comment (variable size)    ***      (minizip: empty)

    //last modification time in DOS format
    uint32_t fhLastModTime;
    //compression method
    uint16_t fhCompressionMethod;
    //compression settings for DEFLATE algorithm
    uint16_t fhGeneralPurposeBitFlag;
    //internal attributes
    uint16_t fhInternalAttribs;
    //external attributes
    uint32_t fhExternalAttribs;
    //size of compressed file (excessive)
    //note: local file header EXcluded
    uint32_t fhCompressedSize;
    //size of uncompressed file (needed when writing in RAW mode)
    uint32_t fhContentsSize;
    //CRC32 checksum of uncompressed file (needed when writing in RAW mode)
    uint32_t fhCrc32;

    //hash of the contents of uncompressed file
    HashDigest contentsHash;
    //hash of the compressed file
    //note: local file header EXcluded
    HashDigest compressedHash;

    static bool IsLess_ZipFn(const TargetFile &a, const TargetFile &b);
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

    int size() const { return files.size(); }
    const ProvidedFile &operator[](int index) const { return files[index]; }
    ProvidedFile &operator[](int index) { return files[index]; }

    void Clear() { files.clear(); }
    void AppendFile(const ProvidedFile &file) { files.push_back(file); }
    void AppendManifest(const ProvidingManifest &other) { AppendVector(files, other.files); }
    void AppendLocalZip(const std::string &zipPath, const std::string &rootDir);

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

    int size() const { return files.size(); }
    const TargetFile &operator[](int index) const { return files[index]; }
    TargetFile &operator[](int index) { return files[index]; }

    void Clear() { files.clear(); }
    void AppendFile(const TargetFile &file) { files.push_back(file); }
    void AppendLocalZip(const std::string &zipPath, const std::string &rootDir, const std::string &packageName);
    void AppendManifest(const TargetManifest &other) { AppendVector(files, other.files); }

    void ReadFromIni(const IniData &data, const std::string &rootDir);
    IniData WriteToIni() const;

    void ReRoot(const std::string &rootDir);
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
public:
    struct Match {
        //target file (surely not NULL)
        const TargetFile *target;
        //provided file which will fulfill it (if NULL, then no match found)
        const ProvidedFile *provided;
    };

private:
    //the target manifest being the goal of this update
    TargetManifest targetMani;
    //the providing manifest showing the current state of installation
    //note: it is changed during the update process
    ProvidingManifest providingMani;
    //the root directory of installation being updated
    //all target files zip paths are treated relative to it
    std::string rootDir;

    //which type of "sameness" we want to achieve
    UpdateType updateType;

    //the best matching provided file for every target file
    std::vector<Match> matches;

    //the manifest containing provided files created by repacking process
    //note: "compressedHash" may be incorrect for these files!
    ProvidingManifest repackedMani;

public:
    //must be called prior to any usage of an instance
    void Init(TargetManifest &&targetMani, ProvidingManifest &&providingMani, const std::string &rootDir);

    //decide how to execute the update (which files to find where)
    bool DevelopPlan(UpdateType type);

    int MatchCount() const { return matches.size(); }
    const Match &GetMatch(int idx) const { return matches[idx]; }

    void DownloadRemoteFiles(const std::string &downloadDir);

    //having all matches available locally, perform the update
    //TODO: pass progress callback
    void RepackZips();

    void RemoveOldZips(const LocalCache *cache);
};

}