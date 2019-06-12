#pragma once

#include "Manifest.h"


namespace TdmSync {

enum class UpdateType {
    SameContents,       //uncompressed contents of every file must match (and compression settings too)
    SameCompressed,     //compressed contents and local file header must be bitwise the same
};

/**
 * Represents the local updater cache (physically present near installation).
 * Usually it contains:
 *   1. old files which are no longer used, but may be helpful to return back to previous version
 *   2. provided manifest for all the files in from p.1
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
        TargetIter target;
        //provided file which will fulfill it (if NULL, then no match found)
        ProvidedIter provided;
    };

private:
    //the target manifest being the goal of this update
    TargetManifest _targetMani;
    //the provided manifest showing the current state of installation
    //note: it is changed during the update process
    ProvidedManifest _providedMani;
    //the root directory of installation being updated
    //all target files zip paths are treated relative to it
    std::string _rootDir;

    //which type of "sameness" we want to achieve
    UpdateType _updateType;

    //the best matching provided file for every target file
    std::vector<Match> _matches;

    //the manifest containing provided files created by repacking process
    ProvidedManifest _repackedMani;
    //the manifest containing no-longer-needed files from target zips
    ProvidedManifest _removedMani;

public:
    //must be called prior to any usage of an instance
    void Init(TargetManifest &&targetMani, ProvidedManifest &&providedMani, const std::string &rootDir);

    //decide how to execute the update (which files to find where)
    bool DevelopPlan(UpdateType type);

    void DownloadRemoteFiles(const std::string &downloadDir);

    //having all matches available locally, perform the update
    //TODO: pass progress callback
    void RepackZips();

    void RemoveOldZips(const LocalCache *cache);



    int MatchCount() const { return _matches.size(); }
    const Match &GetMatch(int idx) const { return _matches[idx]; }

private:
    void ValidateFile(const TargetFile &want, const TargetFile &have) const;
};

}
