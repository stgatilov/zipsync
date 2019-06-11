#pragma once

#include <string>

namespace TdmSync {

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
};

std::string PrefixFile(std::string absPath, std::string prefix);

}