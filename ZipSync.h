#pragma once

#include "Manifest.h"
#include <set>


namespace ZipSync {

class LocalCache;

enum class UpdateType {
    SameContents,       //uncompressed contents of every file must match (and compression settings too)
    SameCompressed,     //compressed contents and local file header must be bitwise the same
};

/**
 * Represents the whole updating process.
 */
class UpdateProcess {
public:
    struct Match {
        //target file (surely not NULL)
        TargetIter target;
        //provided file which will fulfill it (NULL if no match found)
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
    //set of local zips which should be removed/replaced by update
    //note: every zip with a target file gets managed automatically
    std::set<std::string> _managedZips;

    //the best matching provided file for every target file
    std::vector<Match> _matches;

    class Repacker;
    friend class Repacker;

public:
    //must be called prior to any usage of an instance
    void Init(TargetManifest &&targetMani, ProvidedManifest &&providedMani, const std::string &rootDir);
    void AddManagedZip(const std::string &zipPath, bool relative = false);

    //decide how to execute the update (which files to find where)
    bool DevelopPlan(UpdateType type);

    void DownloadRemoteFiles(const std::string &downloadDir);

    //having all matches available locally, perform the update
    //TODO: pass progress callback
    void RepackZips();

    void RemoveOldZips(const LocalCache *cache);


    const ProvidedManifest &GetProvidedManifest() const { return _providedMani; }

    int MatchCount() const { return _matches.size(); }
    const Match &GetMatch(int idx) const { return _matches[idx]; }
};

}
