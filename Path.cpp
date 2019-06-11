#include "Path.h"
#include "tsassert.h"
#include "StdString.h"


namespace TdmSync {

bool PathAR::IsHttp(const std::string &path) {
    return stdext::starts_with(path, "http://");
}


static void CheckPath(const std::string &path, bool relative) {
    for (int i = 0; i < path.size(); i++)
        TdmSyncAssertF(uint8_t(path[i]) >= 32, "Non-printable character %d in path", int(path[i]));
    TdmSyncAssertF(!path.empty() && path != "/", "Empty path [%s]", path.c_str());
    TdmSyncAssertF(path.find_first_of("\\|[]=?&") == std::string::npos, "Forbidden symbol in path %s", path.c_str());
    TdmSyncAssertF(path[0] != '.', "Path must not start with dot: %s", path.c_str());
    if (relative) {
        TdmSyncAssertF(path.find_first_of(":") == std::string::npos, "Colon in relative path %s", path.c_str());
        TdmSyncAssertF(path[0] != '/', "Relative path starts with slash: %s", path.c_str())
    }

}

PathAR PathAR::FromAbs(std::string absPath, std::string rootDir) {
    CheckPath(rootDir, false);
    CheckPath(absPath, false);
    bool lastSlash = (rootDir.back() == '/');
    int len = rootDir.size() - (lastSlash ? 1 : 0);
    TdmSyncAssertF(strncmp(absPath.c_str(), rootDir.c_str(), len) == 0, "Abs path %s is not within root dir %s", absPath.c_str(), rootDir.c_str());
    TdmSyncAssertF(absPath.size() > len && absPath[len] == '/', "Abs path %s is not within root dir %s", absPath.c_str(), rootDir.c_str());
    PathAR res;
    res.abs = absPath;
    res.rel = absPath.substr(len+1);
    return res;
}
PathAR PathAR::FromRel(std::string relPath, std::string rootDir) {
    CheckPath(rootDir, false);
    CheckPath(relPath, true);
    bool lastSlash = (rootDir.back() == '/');
    PathAR res;
    res.rel = relPath;
    res.abs = rootDir + (lastSlash ? "" : "/") + relPath;
    return res;
}
std::string GetFullPath(const std::string &zipPath, const std::string &filename) {
    return zipPath + "||" + filename;
}
void ParseFullPath(const std::string &fullPath, std::string &zipPath, std::string &filename) {
    size_t pos = fullPath.find("||");
    TdmSyncAssertF(pos != std::string::npos, "Cannot split fullname into zip path and filename: %s", fullPath.c_str());
    zipPath = fullPath.substr(0, pos);
    filename = fullPath.substr(pos + 2);
}

std::string PrefixFile(std::string absPath, std::string prefix) {
    size_t pos = absPath.find_last_of('/');
    if (pos != std::string::npos)
        pos++;
    else
        pos = 0;
    absPath.insert(absPath.begin() + pos, prefix.begin(), prefix.end());
    return absPath;
}

}
