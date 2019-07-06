#include "Ini.h"
#include "Utils.h"
#include "StdString.h"
#include "tsassert.h"


namespace ZipSync {

void WriteIniFile(const char *path, const IniData &data) {
    StdioFileHolder f(path, "wb");
    for (const auto &pNS : data) {
        fprintf(f, "[%s]\n", pNS.first.c_str());
        for (const auto &pKV : pNS.second)
            fprintf(f, "%s=%s\n", pKV.first.c_str(), pKV.second.c_str());
        fprintf(f, "\n");
    }
}
IniData ReadIniFile(const char *path) {
    char buffer[SIZE_LINEBUFFER];
    StdioFileHolder f(path, "rb");
    IniData ini;
    IniSect sec;
    std::string name;
    auto CommitSec = [&]() {
        if (!name.empty())
            ini.push_back(std::make_pair(std::move(name), std::move(sec)));
        name.clear();
        sec.clear();
    };
    while (fgets(buffer, sizeof(buffer), f.get())) {
        std::string line = buffer;
        stdext::trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            CommitSec();
            name = line.substr(1, line.size() - 2);
        }
        else {
            size_t pos = line.find('=');
            ZipSyncAssertF(pos != std::string::npos, "Cannot parse ini line: %s", line.c_str());
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos+1);
            sec.push_back(std::make_pair(std::move(key), std::move(value)));
        }
    }
    CommitSec();
    return ini;
}

}
