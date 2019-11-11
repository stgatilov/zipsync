#include "ZipSync.h"
#include "StdString.h"
#include <algorithm>
#include <map>
#include <set>
#include "ZSAssert.h"
#include "Utils.h"
#include "ZipUtils.h"
#include "Downloader.h"


namespace ZipSync {

void UpdateProcess::Init(TargetManifest &&targetMani_, ProvidedManifest &&providedMani_, const std::string &rootDir_) {
    _targetMani = std::move(targetMani_);
    _providedMani = std::move(providedMani_);
    _rootDir = rootDir_;

    _targetMani.ReRoot(_rootDir);

    _updateType = (UpdateType)0xDDDDDDDD;
    _matches.clear();

    //make sure every target zip is "managed"
    for (int i = 0; i < _targetMani.size(); i++) {
        _managedZips.insert(_targetMani[i].zipPath.abs);
    }
}

void UpdateProcess::AddManagedZip(const std::string &zipPath, bool relative) {
    PathAR path = relative ? PathAR::FromRel(zipPath, _rootDir) : PathAR::FromAbs(zipPath, _rootDir);
    auto pib = _managedZips.insert(path.abs);
}

bool UpdateProcess::DevelopPlan(UpdateType type) {
    _updateType = type;

    //build index of target files: by zip path + file path inside zip
    std::map<std::string, const TargetFile*> pathToTarget;
    for (int i = 0; i < _targetMani.size(); i++) {
        const TargetFile &tf = _targetMani[i];
        std::string fullPath = GetFullPath(tf.zipPath.abs, tf.filename);
        auto pib = pathToTarget.insert(std::make_pair(fullPath, &tf));
        ZipSyncAssertF(pib.second, "Duplicate target file at place %s", fullPath.c_str());
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

        const ProvidedFile *bestFile = nullptr;
        int bestScore = 1000000000;

        auto iter = pfIndex.find(tf.contentsHash);
        if (iter != pfIndex.end()) {
            const std::vector<const ProvidedFile*> &candidates = iter->second;

            for (const ProvidedFile *pf : candidates) {
                if (_updateType == UpdateType::SameCompressed && !(pf->compressedHash == tf.compressedHash))
                    continue;
                int score = int(pf->location);
                if (score < bestScore) {
                    bestScore = score;
                    bestFile = pf;
                }
            }
        }
        _matches.push_back(Match{TargetIter(_targetMani, &tf), ProvidedIter(_providedMani, bestFile)});
        if (!bestFile)
            fullPlan = false;
    }

    return fullPlan;
}

/**
 * Implementation class for UpdateProcess::RepackZips method.
 */
class UpdateProcess::Repacker {
public:
    UpdateProcess &_owner;

    struct ZipInfo {
        std::string _zipPath;
        bool _managed = false;

        std::vector<TargetIter> _target;
        std::vector<ProvidedIter> _provided;
        std::vector<int> _matchIds;

        std::string _zipPathRepacked;
        std::string _zipPathReduced;

        //number of provided files in this zip still needed in future
        int _usedCnt = 0;
        bool _repacked = false;
        bool _reduced = false;

        bool operator< (const ZipInfo &b) const {
            return _zipPath < b._zipPath;
        }
    };
    std::vector<ZipInfo> _zips;
    ZipInfo& FindZip(const std::string &zipPath) {
        for (auto &zip : _zips)
            if (zip._zipPath == zipPath)
                return zip;
        ZipSyncAssert(false);
    }

    //indexed as matches: false if provided file was copied in "raw" mode, true if in recompressing mode
    std::vector<bool> _recompressed;
    //how many (local) provided files have specified compressed hash
    //note: includes files from repacked and reduced zips
    std::map<HashDigest, int> _hashProvidedCnt;

    //the manifest containing provided files created by repacking process
    ProvidedManifest _repackedMani;
    //the manifest containing no-longer-needed files from target zips
    ProvidedManifest _reducedMani;


    Repacker(UpdateProcess &owner) : _owner(owner) {}

    void CheckPreconditions() const {
        //verify that we are ready to do repacking
        ZipSyncAssertF(_owner._matches.size() == _owner._targetMani.size(), "RepackZips: DevelopPlan not called yet");
        for (Match m : _owner._matches) {
            std::string fullPath = GetFullPath(m.target->zipPath.abs, m.target->filename);
            ZipSyncAssertF(m.provided, "RepackZips: target file %s is not provided", fullPath.c_str());
            ZipSyncAssertF(m.provided->location == ProvidedLocation::Inplace || m.provided->location == ProvidedLocation::Local, "RepackZips: target file %s is not available locally", fullPath.c_str());
            ZipSyncAssert(_owner._managedZips.count(m.target->zipPath.abs));
        }
    }

    void ClassifyMatchesByTargetZip() {
        //create ZipInfo structure for every zip involved
        std::set<std::string> zipPaths = _owner._managedZips;
        for (int i = 0; i < _owner._providedMani.size(); i++) {
            const ProvidedFile &pf = _owner._providedMani[i];
            if (pf.location == ProvidedLocation::Inplace || pf.location == ProvidedLocation::Local) {
                _hashProvidedCnt[pf.compressedHash]++;
                zipPaths.insert(pf.zipPath.abs);
            }
        }
        for (const std::string &zp : zipPaths) {
            ZipInfo zip;
            zip._zipPath = zp;
            zip._zipPathRepacked = PrefixFile(zp, "__repacked__");
            zip._zipPathReduced = PrefixFile(zp, "__reduced__");
            _zips.push_back(zip);
        }

        //fill zips with initial info
        for (const std::string &zipPath : _owner._managedZips)
            FindZip(zipPath)._managed = true;
        for (int i = 0; i < _owner._targetMani.size(); i++) {
            const TargetFile &tf = _owner._targetMani[i];
            FindZip(tf.zipPath.abs)._target.push_back(TargetIter(_owner._targetMani, i));
        }
        for (int i = 0; i < _owner._providedMani.size(); i++) {
            const ProvidedFile &pf = _owner._providedMani[i];
            if (pf.location == ProvidedLocation::RemoteHttp)
                continue;
            FindZip(pf.zipPath.abs)._provided.push_back(ProvidedIter(_owner._providedMani, i));
        }
        for (int i = 0; i < _owner._matches.size(); i++) {
            const Match &m = _owner._matches[i];
            FindZip(m.target->zipPath.abs)._matchIds.push_back(i);
            FindZip(m.provided->zipPath.abs)._usedCnt++;
        }
    }

    void ProcessZipsWithoutRepacking() {
        //two important use-cases when no repacking should be done:
        //  1. existing zip did not change and should not be updated
        //  2. clean install: downloaded zip should be renamed without repacking
        for (ZipInfo &dstZip : _zips) {
            if (dstZip._matchIds.empty())
                continue;       //nothing to put into this zip
            int k = dstZip._matchIds.size();

            //find source zip candidate
            const PathAR &srcZipPath = _owner._matches[dstZip._matchIds[0]].provided->zipPath;
            ZipInfo &srcZip = FindZip(srcZipPath.abs);
            if (srcZip._provided.size() != k)
                continue;       //number of files is different

            //check that "match" mapping maps into source zip and is surjective
            std::set<const ProvidedFile *> providedSet;
            for (int midx : dstZip._matchIds) {
                Match m = _owner._matches[midx];
                if (m.provided->zipPath.abs != srcZip._zipPath)
                    break;
                providedSet.insert(m.provided.get());
            }
            if (providedSet.size() != k)
                continue;       //some matches map outside (or not surjective)

            if (!srcZip._managed)
                continue;       //non-managed zip: cannot rename
            if (srcZip._usedCnt != k)
                continue;       //every file inside zip must be used exactly once

            { //check that filenames and header data are same (we cannot detect it by provided manifest, unfortunately)
                std::map<uint32_t, TargetIter> bytestartToTarget;
                for (int midx : dstZip._matchIds) {
                    Match m = _owner._matches[midx];
                    bytestartToTarget[m.provided->byterange[0]] = m.target;
                }
                UnzFileHolder zf(srcZip._zipPath.c_str());
                bool allSame = true;
                for (int i = 0; i < k; i++) {
                    SAFE_CALL(i == 0 ? unzGoToFirstFile(zf) : unzGoToNextFile(zf));
                    ProvidedFile pf;
                    TargetFile tf;
                    AnalyzeCurrentFile(zf, pf, tf, false, false);
                    auto iter = bytestartToTarget.find(pf.byterange[0]);
                    if (iter == bytestartToTarget.end()) {
                        allSame = false;
                        break;
                    }
                    const TargetFile &want = *iter->second;
                    if (want.filename != tf.filename ||
                        want.fhLastModTime != tf.fhLastModTime ||
                        want.fhCompressionMethod != tf.fhCompressionMethod ||
                        want.fhGeneralPurposeBitFlag != tf.fhGeneralPurposeBitFlag ||
                        want.fhInternalAttribs != tf.fhInternalAttribs ||
                        want.fhExternalAttribs != tf.fhExternalAttribs ||
                        want.fhCompressedSize != tf.fhCompressedSize ||
                        want.fhContentsSize != tf.fhContentsSize ||
                        want.fhCrc32 != tf.fhCrc32
                    ) {
                        allSame = false;
                        break;
                    }
                }
                if (!allSame)
                    continue;   //filenames of metadata differ
            }

            //note: we can rename source zip into target zip directly
            //this would substitute both repacking and reducing

            //do the physical action
            CreateDirectoriesForFile(dstZip._zipPath, _owner._rootDir);
            RenameFile(srcZip._zipPath, dstZip._zipPathRepacked);

            //update all the data structures
            dstZip._repacked = true;
            srcZip._usedCnt = 0;
            srcZip._reduced = true;
            std::map<uint32_t, ProvidedIter> filesMap;
            for (ProvidedIter pf : srcZip._provided) {
                pf->location = ProvidedLocation::Repacked;
                _repackedMani.AppendFile(*pf);
                filesMap[pf->byterange[0]] = ProvidedIter(_repackedMani, _repackedMani.size() - 1);
            }
            for (int midx : dstZip._matchIds) {
                _recompressed.resize(midx + 1, false);
                _recompressed[midx] = false;
                ProvidedIter &pf = _owner._matches[midx].provided;
                ProvidedIter newIter = filesMap.at(pf->byterange[0]);
                pf->Nullify();
                pf = newIter;
            }
        }
    }

    void RepackZip(ZipInfo &zip) {
        //ensure all directories are created if missing
        CreateDirectoriesForFile(zip._zipPath, _owner._rootDir);
        //create new zip archive (it will contain results of repacking)
        ZipFileHolder zfOut(zip._zipPathRepacked.c_str());

        //copy all target files one-by-one
        for (int midx : zip._matchIds) {
            const Match &m = _owner._matches[midx];

            //find provided file (TODO: optimize?)
            UnzFileHolder zf(m.provided->zipPath.abs.c_str());
            ZipSyncAssert(unzLocateFileAtBytes(zf, m.provided->filename.c_str(), m.provided->byterange[0], m.provided->byterange[1]));

            //can we avoid recompressing the file?
            unz_file_info info;
            SAFE_CALL(unzGetCurrentFileInfo(zf, &info, NULL, 0, NULL, 0, NULL, 0));
            bool copyRaw = false;
            if (m.provided->compressedHash == m.target->compressedHash)
                copyRaw = true;  //bitwise same
            if (_owner._updateType == UpdateType::SameContents && m.target->fhCompressionMethod == info.compression_method && m.target->fhGeneralPurposeBitFlag == info.flag)
                copyRaw = true;  //same compression level

            //copy the file to the new zip
            minizipCopyFile(zf, zfOut,
                m.target->filename.c_str(),
                m.target->fhCompressionMethod, m.target->fhGeneralPurposeBitFlag,
                m.target->fhInternalAttribs, m.target->fhExternalAttribs, m.target->fhLastModTime,
                copyRaw, m.target->fhCrc32, m.target->fhContentsSize
            );
            //remember whether we repacked or not --- to be used in AnalyzeRepackedZip
            _recompressed.resize(midx+1, false);
            _recompressed[midx] = !copyRaw;
        }

        //flush and close new zip
        zfOut.reset();
        zip._repacked = true;
    }

    void ValidateFile(const TargetFile &want, const TargetFile &have) const {
        std::string fullPath = GetFullPath(have.zipPath.abs, have.filename);
        //zipPath is different while repacking
        //package does not need to be checked
        ZipSyncAssertF(want.filename == have.filename, "Wrong filename of %s after repack: need %s", fullPath.c_str(), want.filename.c_str());
        ZipSyncAssertF(want.contentsHash == have.contentsHash, "Wrong contents hash of %s after repack", fullPath.c_str());
        ZipSyncAssertF(want.fhContentsSize == have.fhContentsSize, "Wrong contents size of %s after repack", fullPath.c_str());
        ZipSyncAssertF(want.fhCrc32 == have.fhCrc32, "Wrong crc32 of %s after repack", fullPath.c_str());
        if (_owner._updateType == UpdateType::SameCompressed) {
            ZipSyncAssertF(want.compressedHash == have.compressedHash, "Wrong compressed hash of %s after repack", fullPath.c_str());
            ZipSyncAssertF(want.fhCompressedSize == have.fhCompressedSize, "Wrong compressed size of %s after repack", fullPath.c_str());
        }
        ZipSyncAssertF(want.fhCompressionMethod == have.fhCompressionMethod, "Wrong compression method of %s after repack", fullPath.c_str());
        ZipSyncAssertF(want.fhGeneralPurposeBitFlag == have.fhGeneralPurposeBitFlag, "Wrong flags of %s after repack", fullPath.c_str());
        ZipSyncAssertF(want.fhLastModTime == have.fhLastModTime, "Wrong modification time of %s after repack", fullPath.c_str());
        ZipSyncAssertF(want.fhInternalAttribs == have.fhInternalAttribs, "Wrong internal attribs of %s after repack", fullPath.c_str());
        ZipSyncAssertF(want.fhExternalAttribs == have.fhExternalAttribs, "Wrong external attribs of %s after repack", fullPath.c_str());
    }

    void AnalyzeRepackedZip(const ZipInfo &zip) {
        //analyze the repacked new zip
        UnzFileHolder zf(zip._zipPathRepacked.c_str());
        SAFE_CALL(unzGoToFirstFile(zf));
        for (int i = 0; i < zip._matchIds.size(); i++) {
            int midx = zip._matchIds[i];
            Match &m = _owner._matches[midx];
            if (i > 0) SAFE_CALL(unzGoToNextFile(zf));

            //analyze current file
            bool needsRehashCompressed = _recompressed[midx];
            TargetFile targetNew;
            ProvidedFile providedNew;
            providedNew.zipPath = targetNew.zipPath = PathAR::FromAbs(zip._zipPathRepacked, _owner._rootDir);
            providedNew.location = ProvidedLocation::Repacked;
            targetNew.package = "[repacked]";
            providedNew.contentsHash = targetNew.contentsHash = m.provided->contentsHash;
            providedNew.compressedHash = targetNew.compressedHash = m.provided->compressedHash;   //will be recomputed if needsRehashCompressed
            AnalyzeCurrentFile(zf, providedNew, targetNew, false, needsRehashCompressed);
            //check that it indeed matches the target
            ValidateFile(*m.target, targetNew);

            //decrement ref count on zip (which might allow to "reduce" it in ReduceOldZips)
            int &usedCnt = FindZip(m.provided->zipPath.abs)._usedCnt;
            ZipSyncAssert(usedCnt >= 0);
            usedCnt--;
            //increment ref count on compressed hash
            _hashProvidedCnt[providedNew.compressedHash]++;

            //add info about file to special manifest
            _repackedMani.AppendFile(providedNew);
            //switch the match for the target file to this new file
            m.provided = ProvidedIter(_repackedMani, _repackedMani.size() - 1);
        }
        zf.reset();
    }

    void ReduceOldZips() {
        //see which target zip-s have contents no longer needed
        for (ZipInfo &zip : _zips) {
            if (!zip._managed)
                continue;       //no targets, not repacked, don't remove
            if (zip._reduced)
                continue;       //already reduced
            if (zip._usedCnt > 0)
                continue;       //original zip still needed as source

            if (IfFileExists(zip._zipPath)) {
                UnzFileHolder zf(zip._zipPath.c_str());
                ZipFileHolder zfOut(zip._zipPathReduced.c_str());

                //go over files and copy unique ones to reduced zip
                std::vector<ProvidedFile> copiedFiles;
                SAFE_CALL(unzGoToFirstFile(zf));
                while (1) {
                    //find current file in provided manifest
                    uint32_t range[2];
                    unzGetCurrentFilePosition(zf, &range[0], NULL, &range[1]);
                    ProvidedIter found;
                    for (ProvidedIter pf : zip._provided) {
                        if (pf->byterange[0] == range[0] && pf->byterange[1] == range[1]) {
                            ZipSyncAssertF(!found, "Provided manifest of %s has duplicate byteranges", zip._zipPath.c_str());
                            found = pf;
                        }
                    }
                    //check whether we should retain the file or remove it
                    unz_file_info info;
                    char filename[SIZE_PATH];
                    SAFE_CALL(unzGetCurrentFileInfo(zf, &info, filename, sizeof(filename), NULL, 0, NULL, 0));
                    ZipSyncAssertF(found, "Provided manifest of %s doesn't have file %s", zip._zipPath.c_str(), filename);
                    int &usedCnt = _hashProvidedCnt.at(found->compressedHash);
                    if (usedCnt == 1) {
                        //not available otherwise -> repack
                        minizipCopyFile(zf, zfOut,
                            filename,
                            info.compression_method, info.flag,
                            info.internal_fa, info.external_fa, info.dosDate,
                            true, info.crc, info.uncompressed_size
                        );
                        copiedFiles.push_back(*found);
                    }
                    else {
                        //drop it: it will still be available
                        usedCnt--;
                    }
                    int res = unzGoToNextFile(zf);
                    if (res == UNZ_END_OF_LIST_OF_FILE)
                        break;
                    SAFE_CALL(res);
                }

                zf.reset();
                zfOut.reset();

                if (copiedFiles.empty()) {
                    //empty reduced zip -> remove it
                    RemoveFile(zip._zipPathReduced);
                }
                else {
                    //analyze reduced zip, add all files to manifest
                    UnzFileHolder zf(zip._zipPathReduced.c_str());
                    SAFE_CALL(unzGoToFirstFile(zf));
                    for (int i = 0; i < copiedFiles.size(); i++) {
                        ProvidedFile pf;
                        TargetFile tf;
                        AnalyzeCurrentFile(zf, pf, tf, false, false);
                        pf.zipPath = PathAR::FromAbs(zip._zipPathReduced, _owner._rootDir);
                        pf.location = ProvidedLocation::Reduced;
                        pf.contentsHash = copiedFiles[i].contentsHash;
                        pf.compressedHash = copiedFiles[i].compressedHash;
                        _reducedMani.AppendFile(pf);
                        if (i+1 < copiedFiles.size())
                            SAFE_CALL(unzGoToNextFile(zf));
                    }
                }

                //remove the old file
                RemoveFile(zip._zipPath);
                //nullify all provided files from the removed zip
                for (ProvidedIter pf : zip._provided) {
                    pf->Nullify();
                }
            }

            zip._reduced = true;
        }
    }

    void RenameRepackedZips() {
        //see which target zip-s have contents no longer needed
        for (ZipInfo &zip : _zips) {
            if (!zip._repacked)
                continue;       //not repacked (yet?)
            if (!zip._reduced)
                continue;       //not reduced original yet

            if (zip._matchIds.empty()) {
                //we don't create empty zips (minizip support issue)
                RemoveFile(zip._zipPathRepacked);
            }
            else {
                ZipSyncAssertF(!IfFileExists(zip._zipPath), "Zip %s exists immediately before renaming repacked file", zip._zipPath.c_str());
                RenameFile(zip._zipPathRepacked, zip._zipPath);
            }
            //update provided files in repacked zip (all of them must be among matches by now)
            for (int midx : zip._matchIds) {
                ProvidedFile &pf = *_owner._matches[midx].provided;
                pf.zipPath = PathAR::FromAbs(zip._zipPath, _owner._rootDir);
                pf.location = ProvidedLocation::Inplace;
            }
        }
    }

    void RewriteProvidedManifest() {
        ProvidedManifest newProvidedMani;

        for (int i = 0; i < _repackedMani.size(); i++)
            newProvidedMani.AppendFile(std::move(_repackedMani[i]));
        _repackedMani.Clear();
        for (int i = 0; i < _reducedMani.size(); i++)
            newProvidedMani.AppendFile(std::move(_reducedMani[i]));
        _reducedMani.Clear();
        for (int i = 0; i < _owner._providedMani.size(); i++) {
            ProvidedFile &pf = _owner._providedMani[i];
            if (pf.location == ProvidedLocation::Nowhere)
                continue;       //was removed during zip-reduce
            newProvidedMani.AppendFile(std::move(pf));
        }
        _owner._providedMani.Clear();

        _owner._providedMani = std::move(newProvidedMani);
        //TODO: do we need matches afterwards?
        _owner._matches.clear();
    }

    void DoAll() {
        CheckPreconditions();
        ClassifyMatchesByTargetZip();

        //prepare manifest for repacked files
        _repackedMani.Clear();
        _reducedMani.Clear();

        ProcessZipsWithoutRepacking();

        //iterate over all zips and repack them
        ReduceOldZips();
        for (ZipInfo &zip : _zips) {
            if (!zip._managed)
                continue;   //no targets, no need to remove
            if (zip._matchIds.empty())
                continue;   //minizip doesn't support empty zip
            if (zip._repacked && zip._reduced)
                continue;   //renamed in ProcessZipsWithoutRepacking
            RepackZip(zip);
            AnalyzeRepackedZip(zip);
            ReduceOldZips();
        }

        RenameRepackedZips();
        RewriteProvidedManifest();
    }
};

void UpdateProcess::RepackZips() {
    Repacker impl(*this);
    impl.DoAll();
}

void UpdateProcess::DownloadRemoteFiles() {
    struct UrlData {
        PathAR path;
        StdioFileHolder file;
        int finishedCount = 0, totalCount = 0;
        std::map<int, uint32_t> matchIdxToStart;
        UrlData() : file(nullptr) {}
    };

    std::map<std::string, UrlData> urlStates;
    Downloader downloader;
    for (int midx = 0; midx < _matches.size(); midx++) {
        const Match &m = _matches[midx];
        if (m.provided->location != ProvidedLocation::RemoteHttp)
            continue;
        const std::string &url = m.provided->zipPath.abs;

        PathAR &fn = urlStates[url].path;
        if (fn.abs.empty()) {
            for (int t = 0; t < 100; t++) {
                fn = PathAR::FromRel("__download" + std::to_string(t) + "__" + m.provided->zipPath.rel, _rootDir);
                if (!IfFileExists(fn.abs))
                    break;
            }
        }
        urlStates[url].totalCount++;

        DownloadSource src;
        src.url = url;
        src.byterange[0] = m.provided->byterange[0];
        src.byterange[1] = m.provided->byterange[1];
        downloader.EnqueueDownload(src, [this,&urlStates,midx](const void *data, uint32_t bytes) {
            const Match &m = _matches[midx];
            const std::string &url = m.provided->zipPath.abs;
            UrlData &state = urlStates[url];
            if (!state.file)
                state.file = StdioFileHolder(state.path.abs.c_str(), "wb");

            state.matchIdxToStart[midx] = ftell(state.file);
            ZipSyncAssert(state.matchIdxToStart[midx] >= 0);
            size_t written = fwrite(data, 1, bytes, state.file);
            ZipSyncAssert(written == bytes);

            if (++state.finishedCount == state.totalCount)
                state.file.reset();
        });
    }

    downloader.SetProgressCallback([this](double ratio, const char *message) {
        //TODO
    });
    downloader.DownloadAll();

    for (const auto &pKV : urlStates) {
        const std::string  &url = pKV.first;
        const UrlData &state = pKV.second;

        minizipAddCentralDirectory(state.path.abs.c_str());
        UnzFileHolder zf(state.path.abs.c_str());

        for (const auto &pMO : state.matchIdxToStart) {
            int matchIdx = pMO.first;
            uint32_t offset = pMO.second;
            Match &m = _matches[matchIdx];

            //verify hash of the downloaded file (we must be sure that it is correct)
            //TODO: what if bad mirror takes compressed file and marks at as uncompressed in zip?...
            uint32_t size = (m.provided->byterange[1] - m.provided->byterange[0]);
            ZipSyncAssert(unzLocateFileAtBytes(zf, m.provided->filename.c_str(), offset, offset + size));
            SAFE_CALL(unzOpenCurrentFile2(zf, NULL, NULL, true));
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
            HashDigest obtainedHash = hasher.Finalize();
            SAFE_CALL(unzCloseCurrentFile(zf));

            const HashDigest &expectedHash = m.provided->compressedHash;
            std::string fullPath = GetFullPath(url, m.provided->filename);
            ZipSyncAssertF(obtainedHash == expectedHash, "Hash of \"%s\" after download is %s instead of %s", fullPath.c_str(), obtainedHash.Hex().c_str(), expectedHash.Hex().c_str());

            ProvidedFile pf = *m.provided;
            pf.zipPath = state.path;
            pf.byterange[0] = offset;
            pf.byterange[1] = offset + size;
            pf.location = ProvidedLocation::Local;

            int pi = _providedMani.size();
            _providedMani.AppendFile(pf);
            m.provided = ProvidedIter(_providedMani, pi);
        }
    }

}

}