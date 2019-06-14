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

/// Append filename with prefix (e.g. "C:/__download__models.pk4" from "C:/models.pk4").
std::string PrefixFile(std::string absPath, std::string prefix);

std::string GetFullPath(const std::string &zipPath, const std::string &filename);
void ParseFullPath(const std::string &fullPath, std::string &zipPath, std::string &filename);

bool IfExists(const std::string &path);

}
