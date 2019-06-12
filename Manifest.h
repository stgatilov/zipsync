#pragma once

#include <stdint.h>
#include <vector>
#include "Path.h"
#include "Hash.h"
#include "Ini.h"
#include "ZipUtils.h"


namespace TdmSync {

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
    void Reserve(int num) { files.reserve(num); }
    void AppendFile(const ProvidedFile &file) { files.push_back(file); }
    void AppendManifest(const ProvidingManifest &other);
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
    void AppendManifest(const TargetManifest &other);

    void ReadFromIni(const IniData &data, const std::string &rootDir);
    IniData WriteToIni() const;

    void ReRoot(const std::string &rootDir);
};

/**
 * Iterator to a file in a manifest.
 * Appends do NOT invalidate it.
 */
template<class File, class Manifest> struct IndexIterator {
    Manifest *manifest;
    int index;

    IndexIterator() : manifest(nullptr), index(0) {}
    IndexIterator(Manifest &manifest, int index) : manifest(&manifest), index(index) {}
    IndexIterator(Manifest &manifest, const File *file) : manifest(&manifest), index(file - &manifest[0]) {
        TdmSyncAssert(index >= 0 && index < manifest.size());
    }
    File& operator*() const { return (*manifest)[index]; }
    File* operator->() const { return &(*manifest)[index]; }
    explicit operator bool() const { return manifest != nullptr; }
};
typedef IndexIterator<TargetFile, TargetManifest> TargetIter;
typedef IndexIterator<ProvidedFile, ProvidingManifest> ProvidedIter;


//sets all properties except for:
//  PT: "zipPath"
//  P: "location"
//  T: "package"
//  PT: "contentsHash" (if hashContents = false)
//  PT: "compressedHash" (if hashCompressed = false)
void AnalyzeCurrentFile(unzFile zf, ProvidedFile &provided, TargetFile &target, bool hashContents = true, bool hashCompressed = true);

//creates both types of manifest at once (2x faster than creating them separately)
void AppendManifestsFromLocalZip(
    const std::string &zipPath, const std::string &rootDir,             //path to local zip (both absolute?)
    ProvidingLocation location,                                         //for providing manifest
    const std::string &packageName,                                     //for target manifest
    ProvidingManifest &providMani, TargetManifest &targetMani           //outputs
);

}
