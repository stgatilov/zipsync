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

/**
 * Implementation class for UpdateProcess::RepackZips method.
 */
class UpdateProcess::Repacker {
public:
    UpdateProcess &_owner;

    //for every target zip to be repacked: indices of all matches with target file in it
    std::map<std::string, std::vector<int>> _zipToMatchIds;
    //for every provided zip used in repacking: how many of its files are still needed
    //???
    std::map<std::string, int> _zipToUsedCnt;
    //for every match index: true if provided file was copied in "raw" mode, false if in recompressing mode
    std::map<int, bool> _copiedRaw;

    //the manifest containing provided files created by repacking process
    ProvidedManifest _repackedMani;
    //the manifest containing no-longer-needed files from target zips
    ProvidedManifest _removedMani;


    Repacker(UpdateProcess &owner) : _owner(owner) {}

    void CheckPreconditions() const {
        //verify that we are ready to do repacking
        TdmSyncAssertF(_owner._matches.size() == _owner._targetMani.size(), "RepackZips: DevelopPlan not called yet");
        for (Match m : _owner._matches) {
            std::string fullPath = GetFullPath(m.target->zipPath.abs, m.target->filename);
            TdmSyncAssertF(m.provided, "RepackZips: target file %s is not provided", fullPath.c_str());
            TdmSyncAssertF(m.provided->location == ProvidedLocation::Inplace || m.provided->location == ProvidedLocation::Local, "RepackZips: target file %s is not available locally", fullPath.c_str());
        }
    }

    void ClassifyMatchesByTargetZip() {
        //sort matches: all matches with target in one zip are consecutive
        //I'm not sure whether it is required by the algorithm, but it is more convenient
        std::sort(_owner._matches.begin(), _owner._matches.end(), [](const Match &a, const Match &b) {
            return TargetFile::IsLess_ByZip(*a.target, *b.target);
        });

        //classify all matches into target zips
        _zipToMatchIds.clear();
        for (int i = 0; i < _owner._matches.size(); i++) {
            const Match &m = _owner._matches[i];
            const std::string &zipPath = m.target->zipPath.abs;
            _zipToMatchIds[zipPath].push_back(i);
        }

        //check how many provided files are used from every zip
        for (const auto &pZV : _zipToMatchIds)
            for (int midx : pZV.second) {
                Match m = _owner._matches[midx];
                _zipToUsedCnt[m.provided->zipPath.abs]++;
            }
    }

#if 0
    void ProcessZipsWithoutRepacking() {
        //check which target zips need no change at all
        std::map<std::string, std::vector<ProvidedIter>> zipToProvided;
        for (int i = 0; i < _owner._providedMani.size(); i++) {
            const ProvidedFile &pf = _owner._providedMani[i];
            zipToProvided[pf.zipPath.abs].push_back(ProvidedIter(_owner._providedMani, i));
        }
        std::vector<std::string> zipsDontChange;
        for (const auto &pZV : _zipToMatchIds) {
            const std::string &zipPath = pZV.first;
            const std::vector<int> &matchIds = pZV.second;
            int cntInplace = matchIds.size();
            for (int midx : matchIds) {
                Match m = _owner._matches[midx];
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
            _zipToMatchIds.erase(zipfn);
    
        //TODO: handle case when downloaded zip fits target zip perfectly
    }
#endif

    void RepackZip(const std::string &zipPath, const std::vector<int> &matchIds, const std::string &zipPathOut) {
        //create new zip archive (it will contain results of repacking)
        ZipFileHolder zfOut(zipPathOut.c_str());

        //copy all target files one-by-one
        for (int midx : matchIds) {
            const Match &m = _owner._matches[midx];

            //find provided file (TODO: optimize?)
            UnzFileHolder zf(m.provided->zipPath.abs.c_str());
            TdmSyncAssert(unzLocateFileAtBytes(zf, m.provided->filename.c_str(), m.provided->byterange[0], m.provided->byterange[1]));

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
            _copiedRaw[midx] = copyRaw;
        }

        //flush and close new zip
        zfOut.reset();
    }

    void ValidateFile(const TargetFile &want, const TargetFile &have) const {
        std::string fullPath = GetFullPath(have.zipPath.abs, have.filename);
        //zipPath is different while repacking
        //package does not need to be checked
        TdmSyncAssertF(want.filename == have.filename, "Wrong filename of %s after repack: need %s", fullPath.c_str(), want.filename.c_str());
        TdmSyncAssertF(want.contentsHash == have.contentsHash, "Wrong contents hash of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhContentsSize == have.fhContentsSize, "Wrong contents size of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhCrc32 == have.fhCrc32, "Wrong crc32 of %s after repack", fullPath.c_str());
        if (_owner._updateType == UpdateType::SameCompressed) {
            TdmSyncAssertF(want.compressedHash == have.compressedHash, "Wrong compressed hash of %s after repack", fullPath.c_str());
            TdmSyncAssertF(want.fhCompressedSize == have.fhCompressedSize, "Wrong compressed size of %s after repack", fullPath.c_str());
        }
        TdmSyncAssertF(want.fhCompressionMethod == have.fhCompressionMethod, "Wrong compression method of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhGeneralPurposeBitFlag == have.fhGeneralPurposeBitFlag, "Wrong flags of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhLastModTime == have.fhLastModTime, "Wrong modification time of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhInternalAttribs == have.fhInternalAttribs, "Wrong internal attribs of %s after repack", fullPath.c_str());
        TdmSyncAssertF(want.fhExternalAttribs == have.fhExternalAttribs, "Wrong external attribs of %s after repack", fullPath.c_str());
    }

    void AnalyzeRepackedZip(const std::string &zipPathOut, const std::vector<int> &matchIds) {
        //analyze the repacked new zip
        UnzFileHolder zf(zipPathOut.c_str());
        SAFE_CALL(unzGoToFirstFile(zf));
        for (int i = 0; i < matchIds.size(); i++) {
            int midx = matchIds[i];
            Match &m = _owner._matches[midx];
            if (i > 0) SAFE_CALL(unzGoToNextFile(zf));

            //analyze current file
            bool needsRehashCompressed = !_copiedRaw[midx];
            TargetFile targetNew;
            ProvidedFile providedNew;
            providedNew.zipPath = targetNew.zipPath = PathAR::FromAbs(zipPathOut, _owner._rootDir);
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
            _zipToUsedCnt[m.provided->zipPath.abs]--;
            m.provided = ProvidedIter(_repackedMani, _repackedMani.size() - 1);
        }
        zf.reset();
    }

    void ReduceOldZips() {
        //see which target zip-s have contents no longer needed
        for (const auto &pZV : _zipToMatchIds) {
            const std::string &zipPath = pZV.first;
            auto iter = _zipToUsedCnt.find(zipPath);
            if (iter == _zipToUsedCnt.end())
                continue;
            TdmSyncAssert(iter->second >= 0);
            if (iter->second > 0)
                continue;

            //this is a target zip and its contents are no longer needed anywhere
            _zipToUsedCnt.erase(iter);

            std::string zipPathOut = PrefixFile(zipPath, "__removed__");
            UnzFileHolder zf(zipPath.c_str());
            ZipFileHolder zfOut(zipPathOut.c_str());

            //go over files and copy ??? one-by-one
            SAFE_CALL(unzGoToFirstFile(zf));
            while (1) {
                unz_file_info info;
                char filename[SIZE_PATH];
                SAFE_CALL(unzGetCurrentFileInfo(zf, &info, filename, sizeof(filename), NULL, 0, NULL, 0));
                minizipCopyFile(zf, zfOut,
                    filename,
                    info.compression_method, info.flag,
                    info.internal_fa, info.external_fa, info.dosDate,
                    true, info.crc, info.uncompressed_size
                );
                int res = unzGoToNextFile(zf);
                if (res == UNZ_END_OF_LIST_OF_FILE)
                    break;
                SAFE_CALL(res);
            }
        }
    }

    void DoAll() {
        CheckPreconditions();
        ClassifyMatchesByTargetZip();
        //ProcessZipsWithoutRepacking();    //TODO

        //prepare manifest for repacked files
        _repackedMani.Clear();
        _removedMani.Clear();

        //iterate over all zips and repack them
        for (const auto &pZV : _zipToMatchIds) {
            const std::string &zipPath = pZV.first;
            const std::vector<int> &matchIds = pZV.second;
            std::string zipPathOut = PrefixFile(zipPath, "__new__");

            RepackZip(zipPath, matchIds, zipPathOut);
            AnalyzeRepackedZip(zipPathOut, matchIds);
            ReduceOldZips();
        }
    }
};

void UpdateProcess::RepackZips() {
    Repacker impl(*this);
    impl.DoAll();
}

}
