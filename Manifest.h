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
enum class ProvidedLocation {
    Inplace = 0,    //local zip file in the place where it should be
    Local = 1,      //local zip file (e.g. inside local cache of old versions)
    RemoteHttp = 2, //file remotely available via HTTP 1.1+

    Nowhere,        //(should never be used)

    Repacked,       //internal: file is on its place in "repacked" zip (not yet renamed back)
    Reduced,        //internal: file is in "reduced" zip, to be moved to cache later
};

/**
 * Metainformation about a provided file.
 * All of this can be quickly deduced from "provided manifest"
 * without having to download the actual provided files.
 */
struct ProvidedFile {
    //file/url path to the zip archive containing the file
    PathAR zipPath;
    //filename inside zip (for ordering/debugging)
    std::string filename;
    //type of file: local/remote
    ProvidedLocation location;
    //range of bytes in the zip representing the file
    //note: local file header INcluded
    uint32_t byterange[2];

    //hash of the contents of uncompressed file
    HashDigest contentsHash;
    //hash of the compressed file
    //note: local file header EXcluded
    HashDigest compressedHash;

    static bool IsLess_ByZip(const ProvidedFile &a, const ProvidedFile &b);
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

    static bool IsLess_ByZip(const TargetFile &a, const TargetFile &b);
};

/**
 * "Provided manifest" describes a set of files available.
 * The sync algorithm can soak any number of provided manifests.
 * Update is possible if all of them together cover the requirements of the selected target packages.
 * Example: we can create a provided manifest for a TDM's "differential update package".
 */
class ProvidedManifest {
    //arbitrary text attached to the manifest (only for debugging)
    std::string _comment;

    //the set of files declared available by this manifest
    std::vector<ProvidedFile> _files;

public:
    const std::string &GetComment() const { return _comment; }
    void SetComment(const std::string &text) { _comment = text; }

    int size() const { return _files.size(); }
    const ProvidedFile &operator[](int index) const { return _files[index]; }
    ProvidedFile &operator[](int index) { return _files[index]; }

    void Clear() { _files.clear(); }
    void AppendFile(const ProvidedFile &file) { _files.push_back(file); }
    void AppendManifest(const ProvidedManifest &other);
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
    std::string _comment;
    //set of files described in the manifest
    std::vector<TargetFile> _files;

public:
    const std::string &GetComment() const { return _comment; }
    void SetComment(const std::string &text) { _comment = text; }

    int size() const { return _files.size(); }
    const TargetFile &operator[](int index) const { return _files[index]; }
    TargetFile &operator[](int index) { return _files[index]; }

    void Clear() { _files.clear(); }
    void AppendFile(const TargetFile &file) { _files.push_back(file); }
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
    Manifest *_manifest;
    int _index;

    IndexIterator() : _manifest(nullptr), _index(0) {}
    IndexIterator(Manifest &manifest, int index) : _manifest(&manifest), _index(index) {}
    IndexIterator(Manifest &manifest, const File *file) {
        if (file) {
            _manifest = &manifest;
            _index = file - &manifest[0];
            TdmSyncAssert(_index >= 0 && _index < manifest.size());
        }
        else {
            _manifest = nullptr;
            _index = 0;
        }
    }
    File& operator*() const { return (*_manifest)[_index]; }
    File* operator->() const { return &(*_manifest)[_index]; }
    explicit operator bool() const { return _manifest != nullptr; }
};
typedef IndexIterator<TargetFile, TargetManifest> TargetIter;
typedef IndexIterator<ProvidedFile, ProvidedManifest> ProvidedIter;


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
    ProvidedLocation location,                                          //for provided manifest
    const std::string &packageName,                                     //for target manifest
    ProvidedManifest &providMani, TargetManifest &targetMani            //outputs
);

}
