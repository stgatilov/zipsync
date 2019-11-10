#pragma once

#include <string>

namespace ZipSync {

/**
 * File path or http URL in both absolute and relative format.
 */
struct PathAR {
    std::string abs;
    std::string rel;

    static bool IsHttp(const std::string &path);
    static bool IsAbsolute(const std::string &path);
    bool IsUrl() const { return IsHttp(abs); }
    std::string GetRootDir() const;

    static PathAR FromAbs(std::string absPath, std::string rootDir);
    static PathAR FromRel(std::string relPath, std::string rootDir);
};

/// Append filename with prefix (e.g. "C:/__download__models.pk4" from "C:/models.pk4").
std::string PrefixFile(std::string absPath, std::string prefix);

std::string GetFullPath(const std::string &zipPath, const std::string &filename);
void ParseFullPath(const std::string &fullPath, std::string &zipPath, std::string &filename);


//the functions below actually interact with filesystem!

bool IfFileExists(const std::string &path);
void RemoveFile(const std::string &path);
void RenameFile(const std::string &oldPath, const std::string &newPath);
bool CreateDir(const std::string &dirPath);
void CreateDirectoriesForFile(const std::string &filePath, const std::string &rootPath);

}
