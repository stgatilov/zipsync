#pragma once

#include <vector>
#include <string>
#include <algorithm>

namespace TdmSync {

/// Contents of one section of ini file (note: ordered).
typedef std::vector<std::pair<std::string, std::string>> IniSect;
/// Contents of an ini file (note: ordered).
typedef std::vector<std::pair<std::string, IniSect>> IniData;

void WriteIniFile(const char *path, const IniData &data);
IniData ReadIniFile(const char *path);

}
