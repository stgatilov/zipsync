#include "ZipSync.h"
#include "args.hxx"
#include <stdio.h>
#include <iostream>
#include "Wildcards.h"


#include <filesystem>
namespace fs = std::experimental::filesystem::v1;
std::vector<std::string> EnumerateFilesInDirectory(const std::string &root) {
    using ZipSync::PathAR;
    std::vector<std::string> res;
    for (auto& entry: fs::recursive_directory_iterator(fs::path(root))) {
        if (fs::is_regular_file(entry)) {
            std::string absPath = entry.path().generic_string();
            std::string relPath = PathAR::FromAbs(absPath, root).rel;
            res.push_back(relPath);
        }
    }
    return res;
}
std::string GetCwd() {
    return fs::current_path().generic_string();
}

std::string NormalizeSlashes(std::string path) {
    for (char &ch : path)
        if (ch == '\\')
            ch = '/';
    return path;
}

std::string GetPath(std::string path, const std::string &root) {
    using ZipSync::PathAR;
    path = NormalizeSlashes(path);
    if (PathAR::IsAbsolute(path))
        return path;
    else
        return PathAR::FromRel(path, root).abs;
}

std::vector<std::string> CollectFilePaths(const std::vector<std::string> &elements, const std::string &root) {
    using ZipSync::PathAR;
    std::vector<std::string> resPaths;
    std::vector<std::string> wildcards;
    for (std::string str : elements) {
        str = NormalizeSlashes(str);
        if (PathAR::IsAbsolute(str))
            resPaths.push_back(str);
        else {
            if (str.find_first_of("*?") == std::string::npos)
                resPaths.push_back(PathAR::FromRel(str, root).abs);
            else
                wildcards.push_back(str);
        }
    }
    if (!wildcards.empty()) {
        auto allFiles = EnumerateFilesInDirectory(root);
        for (const std::string &path : allFiles) {
            bool matches = false;
            for (const std::string &wild : wildcards)
                if (WildcardMatch(wild.c_str(), path.c_str()))
                    matches = true;
            if (matches)
                resPaths.push_back(PathAR::FromRel(path, root).abs);
        }
    }
    return resPaths;
}


void CommandNormalize(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "Relative paths to zips are based from this directory", {'r', "root"});
    args::PositionalList<std::string> argZips(parser, "zips", "List of files or globs specifying which zips in root directory to include");
    args::ValueFlag<std::string> argOutDir(parser, "output", "Write normalized zips to this directory (instead of modifying in-place)", {'o', "output"});
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::HiddenFromDescription);
    parser.Parse();

    std::string root = GetCwd();
    if (argRootDir)
        root = argRootDir.Get();
    root = NormalizeSlashes(root);
    std::string outDir;
    if (argOutDir)
        outDir = NormalizeSlashes(argOutDir.Get());
    std::vector<std::string> zipPaths = CollectFilePaths(argZips.Get(), root);

    for (std::string zip : zipPaths) {
        if (argOutDir) {
            std::string rel = ZipSync::PathAR::FromAbs(zip, root).rel;
            std::string zipOut = ZipSync::PathAR::FromRel(rel, outDir).abs;
            ZipSync::CreateDirectoriesForFile(zipOut, outDir);
            ZipSync::minizipNormalize(zip.c_str(), zipOut.c_str());
        }
        else
            ZipSync::minizipNormalize(zip.c_str());
    }
}

void CommandAnalyze(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "Manifests would contain paths relative to this root directory\n"
        "(all relative paths are based from the root directory)", {'r', "root"}, args::Options::Required);
    args::ValueFlag<std::string> argTargetMani(parser, "trgMani", "Path where Target manifest would be written", {'t', "target"}, "target.ini");
    args::ValueFlag<std::string> argProvidedMani(parser, "provMani", "Path where Provided manifest would be written", {'p', "provided"}, "provided.ini");
    args::Flag argNoTarget(parser, "notarget", "Do not generate Target manifest", {"no-target"});
    args::Flag argNoProvided(parser, "noprovided", "Do not generate Provided manifest", {"no-provided"});
    args::PositionalList<std::string> argZips(parser, "zips", "List of files or globs specifying which zips in root directory to include");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::HiddenFromDescription);
    parser.Parse();

    std::string root = NormalizeSlashes(argRootDir.Get());
    std::string targetManiPath = GetPath(argTargetMani.Get(), root);
    std::string providManiPath = GetPath(argProvidedMani.Get(), root);
    std::vector<std::string> zipPaths = CollectFilePaths(argZips.Get(), root);

    ZipSync::ProvidedManifest providMani;
    ZipSync::TargetManifest targetMani;
    for (std::string zipPath : zipPaths)
        ZipSync::AppendManifestsFromLocalZip(zipPath, root, ZipSync::ProvidedLocation::Local, "", providMani, targetMani);

    if (!argNoTarget)
        ZipSync::WriteIniFile(targetManiPath.c_str(), targetMani.WriteToIni());
    if (!argNoProvided)
        ZipSync::WriteIniFile(providManiPath.c_str(), providMani.WriteToIni());
}

int main(int argc, char **argv) {
    args::ArgumentParser parser("ZipSync command line tool.");
    parser.helpParams.programName = "zipsync";
    parser.helpParams.width = 120;
    parser.helpParams.flagindent = 4;
    parser.helpParams.showTerminator = false;
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::Command analyze(parser, "analyze", "Create manifests for specified set of zips (on local machine)", CommandAnalyze);
    args::Command normalize(parser, "normalize", "Normalize specified set of zips (on local machine)", CommandNormalize);
    try {
        parser.ParseCLI(argc, argv);
    }
    catch (const args::Help&) {
        std::cout << parser;
        return 0;
    }
    catch (const args::Error& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 2;
    }
    return 0;
}
