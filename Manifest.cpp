#include "Manifest.h"
#include "tsassert.h"
#include "StdString.h"
#include "Utils.h"
#include "ZipUtils.h"
#include "Path.h"
#include <tuple>
#include <map>

#include "minizip_extra.h"


namespace TdmSync {

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


void AnalyzeCurrentFile(unzFile zf, ProvidedFile &provided, TargetFile &target, bool hashContents, bool hashCompressed) {
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

        Hasher hasher;
        char buffer[SIZE_FILEBUFFER];
        int processedBytes = 0;
        while (1) {
            int bytes = unzReadCurrentFile(zf, buffer, sizeof(buffer));
            if (bytes < 0)
                SAFE_CALL(bytes);
            if (bytes == 0)
                break;
            hasher.Update(buffer, bytes);
            processedBytes += bytes;
        } 
        HashDigest cmpHash = hasher.Finalize();

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
    const std::string &zipPathAbs, const std::string &rootDir,
    ProvidedLocation location,
    const std::string &packageName,
    ProvidedManifest &providMani, TargetManifest &targetMani
) {
    PathAR zipPath = PathAR::FromAbs(zipPathAbs, rootDir);

    UnzFileHolder zf(zipPath.abs.c_str());
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
void ProvidedManifest::AppendLocalZip(const std::string &zipPath, const std::string &rootDir) {
    TargetManifest temp;
    ProvidedLocation location = (PathAR::IsHttp(rootDir) ? ProvidedLocation::RemoteHttp : ProvidedLocation::Local);
    AppendManifestsFromLocalZip(zipPath, rootDir, location, "", *this, temp);
}
void TargetManifest::AppendLocalZip(const std::string &zipPath, const std::string &rootDir, const std::string &packageName) {
    ProvidedManifest temp;
    AppendManifestsFromLocalZip(zipPath, rootDir, ProvidedLocation::Inplace, packageName, temp, *this);
}

void TargetManifest::AppendManifest(const TargetManifest &other) {
    AppendVector(files, other.files);
}
void ProvidedManifest::AppendManifest(const ProvidedManifest &other) {
    AppendVector(files, other.files);
}

IniData ProvidedManifest::WriteToIni() const {
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
void ProvidedManifest::ReadFromIni(const IniData &data, const std::string &rootDir) {
    ProvidedLocation location = (PathAR::IsHttp(rootDir) ? ProvidedLocation::RemoteHttp : ProvidedLocation::Local);

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

}
