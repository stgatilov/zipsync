#include "TdmSync.h"
#include "StdString.h"
#include <algorithm>
#include <map>
#include <set>
#include "tsassert.h"
#include "Utils.h"
#include "ZipUtils.h"


namespace TdmSync {

void UpdateProcess::Init(TargetManifest &&targetMani_, ProvidedManifest &&providedMani_, const std::string &rootDir_) {
    _targetMani = std::move(targetMani_);
    _providedMani = std::move(providedMani_);
    _rootDir = rootDir_;

    _targetMani.ReRoot(_rootDir);

    _updateType = (UpdateType)0xDDDDDDDD;
    _matches.clear();
}

bool UpdateProcess::DevelopPlan(UpdateType type) {
    _updateType = type;

    //build index of target files: by zip path + file path inside zip
    std::map<std::string, const TargetFile*> pathToTarget;
    for (int i = 0; i < _targetMani.size(); i++) {
        const TargetFile &tf = _targetMani[i];
        std::string fullPath = GetFullPath(tf.zipPath.abs, tf.filename);
        auto pib = pathToTarget.insert(std::make_pair(fullPath, &tf));
        TdmSyncAssertF(pib.second, "Duplicate target file at place %s", fullPath.c_str());
    }

    //find provided files which are already in-place
    for (int i = 0; i < _providedMani.size(); i++) {
        ProvidedFile &pf = _providedMani[i];
        if (pf.location != ProvidedLocation::Local)
            continue;
        std::string fullPath = GetFullPath(pf.zipPath.abs, pf.filename);
        auto iter = pathToTarget.find(fullPath);
        if (iter != pathToTarget.end()) {
            //give this provided file priority when choosing where to take file from
            pf.location = ProvidedLocation::Inplace;
        }
    }

    //build index of provided files (by hash on uncompressed file)
    std::map<HashDigest, std::vector<const ProvidedFile*>> pfIndex;
    for (int i = 0; i < _providedMani.size(); i++) {
        const ProvidedFile &pf = _providedMani[i];
        pfIndex[pf.contentsHash].push_back(&pf);
    }

    //find matching provided file for every target file
    _matches.clear();
    bool fullPlan = true;
    for (int i = 0; i < _targetMani.size(); i++) {
        const TargetFile &tf = _targetMani[i];

        auto iter = pfIndex.find(tf.contentsHash);
        if (iter == pfIndex.end()) {
            _matches.push_back(Match{TargetIter(_targetMani, &tf), ProvidedIter()});
            fullPlan = false;
            continue;
        }
        const std::vector<const ProvidedFile*> &candidates = iter->second;

        int bestScore = 1000000000;
        const ProvidedFile *bestFile = nullptr;
        for (const ProvidedFile *pf : candidates) {
            if (_updateType == UpdateType::SameCompressed && !(pf->compressedHash == tf.compressedHash))
                continue;
            int score = int(pf->location);
            if (score < bestScore) {
                bestScore = score;
                bestFile = pf;
            }
        }
        _matches.push_back(Match{TargetIter(_targetMani, &tf), ProvidedIter(_providedMani, bestFile)});
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
    if (_updateType == UpdateType::SameCompressed) {
        TdmSyncAssertF(want.compressedHash == have.compressedHash, "Wrong compressed hash of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhCompressedSize == have.fhCompressedSize, "Wrong compressed size of %s after repack", fullPath.c_str());
    }
    TdmSyncAssertF(want.fhCompressionMethod == have.fhCompressionMethod, "Wrong compression method of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhGeneralPurposeBitFlag == have.fhGeneralPurposeBitFlag, "Wrong flags of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhLastModTime == have.fhLastModTime, "Wrong modification time of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhInternalAttribs == have.fhInternalAttribs, "Wrong internal attribs of %s after repack", fullPath.c_str());
    TdmSyncAssertF(want.fhExternalAttribs == have.fhExternalAttribs, "Wrong external attribs of %s after repack", fullPath.c_str());
}

int CompressionLevelFromGpFlags(int flags) {
    int compressionLevel = Z_DEFAULT_COMPRESSION;
    if (flags == 2)
        compressionLevel = Z_BEST_COMPRESSION;  //minizip: 8,9
    if (flags == 4)
        compressionLevel = 2;                   //minizip: 2
    if (flags == 6)
        compressionLevel = Z_BEST_SPEED;        //minizip: 1
    return compressionLevel;
}

void UpdateProcess::RepackZips() {
    //verify that we are ready to do repacking
    TdmSyncAssertF(_matches.size() == _targetMani.size(), "RepackZips: DevelopPlan not called yet");
    for (Match m : _matches) {
        std::string fullPath = GetFullPath(m.target->zipPath.abs, m.target->filename);
        TdmSyncAssertF(m.provided, "RepackZips: target file %s is not provided", fullPath.c_str());
        TdmSyncAssertF(m.provided->location == ProvidedLocation::Inplace || m.provided->location == ProvidedLocation::Local, "RepackZips: target file %s is not available locally", fullPath.c_str());
    }

    //group target files by their zips
    std::sort(_matches.begin(), _matches.end(), [](const Match &a, const Match &b) {
        return TargetFile::IsLess_ZipFn(*a.target, *b.target);
    });
    std::map<std::string, std::vector<int>> zipToMatchIds;      //for every zip file: indices of all matches with target in it
    for (int i = 0; i < _matches.size(); i++) {
        const std::string &zipPath = _matches[i].target->zipPath.abs;
        zipToMatchIds[zipPath].push_back(i);
    }

    //check which target zips need no change at all
    std::map<std::string, std::vector<const ProvidedFile*>> zipToProvided;
    for (int i = 0; i < _providedMani.size(); i++) {
        zipToProvided[_providedMani[i].zipPath.abs].push_back(&_providedMani[i]);
    }
    std::vector<std::string> zipsDontChange;
    for (const auto &pZV : zipToMatchIds) {
        const std::string &zipPath = pZV.first;
        const std::vector<int> &matchIds = pZV.second;
        int cntInplace = matchIds.size();
        for (int midx : matchIds) {
            Match m = _matches[midx];
            if (m.provided->location != ProvidedLocation::Inplace)
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

    //TODO: handle case when downloaded zip fits target zip perfectly

    //prepare manifest for repacked files
    _repackedMani.Clear();
    _removedMani.Clear();

    //check how many provided files are used from every zip
    std::map<std::string, int> zipToMatchCnt;
    for (const auto &pZV : zipToMatchIds)
        for (int midx : pZV.second) {
            Match m = _matches[midx];
            zipToMatchCnt[m.provided->zipPath.abs]++;
        }


    //iterate over all zips and repack them
    for (const auto &pZV : zipToMatchIds) {
        const std::string &zipPath = pZV.first;
        const std::vector<int> &matchIds = pZV.second;

        //create new zip archive (it will contain the results of repacking)
        std::string zipPathOut = PrefixFile(zipPath, "__new__");
        ZipFileHolder zfOut(zipPathOut.c_str());

        //copy all files one-by-one
        std::map<int, bool> copiedRaw;
        for (int midx : matchIds) {
            Match m = _matches[midx];

            //open provided file for reading (TODO: optimize?)
            UnzFileHolder zf(m.provided->zipPath.abs.c_str());
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
            if (_updateType == UpdateType::SameContents && m.target->fhCompressionMethod == info.compression_method && m.target->fhGeneralPurposeBitFlag == info.flag)
                useRaw = true;  //same compression level

            //prepare metadata for target file
            zip_fileinfo infoOut;
            infoOut.dosDate = m.target->fhLastModTime;
            infoOut.internal_fa = m.target->fhInternalAttribs;
            infoOut.external_fa = m.target->fhExternalAttribs;
            int compressionLevel = CompressionLevelFromGpFlags(m.target->fhGeneralPurposeBitFlag);

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

        //analyze the repacked new zip
        UnzFileHolder zf(zipPathOut.c_str());
        SAFE_CALL(unzGoToFirstFile(zf));
        for (int i = 0; i < matchIds.size(); i++) {
            int midx = matchIds[i];
            Match &m = _matches[midx];
            if (i > 0) SAFE_CALL(unzGoToNextFile(zf));

            //analyze current file
            bool needsRehashCompressed = !copiedRaw[midx];
            TargetFile targetNew;
            ProvidedFile providedNew;
            providedNew.zipPath = targetNew.zipPath = PathAR::FromAbs(zipPathOut, _rootDir);
            providedNew.location = ProvidedLocation::Local;
            targetNew.package = "[repacked]";
            providedNew.contentsHash = targetNew.contentsHash = m.target->contentsHash;
            providedNew.compressedHash = targetNew.compressedHash = m.target->compressedHash;   //will be recomputed if needsRehashCompressed
            AnalyzeCurrentFile(zf, providedNew, targetNew, false, needsRehashCompressed);
            //check that it indeed matches the target
            ValidateFile(*m.target, targetNew);

            //add info about file to special manifest
            _repackedMani.AppendFile(providedNew);
            //switch the match for the target file to this new file
            zipToMatchCnt[m.provided->zipPath.abs]--;
            m.provided = ProvidedIter(_repackedMani, _repackedMani.size() - 1);
        }
        zf.reset();

        //see which target zip-s have contents no longer needed
        for (const auto &pZV : zipToMatchIds) {
            const std::string &zipPath = pZV.first;
            auto iter = zipToMatchCnt.find(zipPath);
            if (iter == zipToMatchCnt.end())
                continue;
            TdmSyncAssert(iter->second >= 0);
            if (iter->second > 0)
                continue;

            //this is a target zip and its contents are no longer needed anywhere
            zipToMatchCnt.erase(iter);
            const std::vector<const ProvidedFile*> &providedFiles = zipToProvided[zipPath];

            std::string zipPathOut = PrefixFile(zipPath, "__removed__");
            UnzFileHolder zf(zipPath.c_str());
            ZipFileHolder zfOut(zipPathOut.c_str());

            //go over files and copy ??? one-by-one
            SAFE_CALL(unzGoToFirstFile(zf));
            while (1) {
                SAFE_CALL(unzOpenCurrentFile2(zf, NULL, NULL, true));
                unz_file_info info;
                char filename[SIZE_PATH];
                SAFE_CALL(unzGetCurrentFileInfo(zf, &info, filename, sizeof(filename), NULL, 0, NULL, 0));
                zip_fileinfo infoOut;
                infoOut.dosDate = info.dosDate;
                infoOut.internal_fa = info.internal_fa;
                infoOut.external_fa = info.external_fa;
                SAFE_CALL(zipOpenNewFileInZip2(zfOut, filename, &infoOut, NULL, 0, NULL, 0, NULL, info.compression_method, CompressionLevelFromGpFlags(info.flag), true));
                while (1) {
                    char buffer[SIZE_FILEBUFFER];
                    int bytes = unzReadCurrentFile(zf, buffer, sizeof(buffer));
                    if (bytes < 0)
                        SAFE_CALL(bytes);
                    if (bytes == 0)
                        break;
                    SAFE_CALL(zipWriteInFileInZip(zfOut, buffer, bytes));
                }
                int res = unzGoToNextFile(zf);
                if (res == UNZ_END_OF_LIST_OF_FILE)
                    break;
                SAFE_CALL(res);
            }

            //removedMani
        }
    }

}

}
