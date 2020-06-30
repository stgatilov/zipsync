#include "ZipSync.h"
#include "Downloader.h"
#include "Utils.h"
#include "args.hxx"
#include <stdio.h>
#include <iostream>
#include "Wildcards.h"
#include <thread>
#include <mutex>
#include <map>
#include <random>


#include "StdFilesystem.h"
std::vector<std::string> EnumerateFilesInDirectory(const std::string &root) {
    using ZipSync::PathAR;
    std::vector<std::string> res;
    std::vector<stdext::path> allPaths = stdext::recursive_directory_enumerate(stdext::path(root));
    for (auto& entry : allPaths) {
        if (stdext::is_regular_file(entry)) {
            std::string absPath = entry.string();   //.generic_string()
            std::string relPath = PathAR::FromAbs(absPath, root).rel;
            res.push_back(relPath);
        }
    }
    return res;
}
std::string GetCwd() {
    return stdext::current_path().string(); //.generic_string();
}
size_t SizeOfFile(const std::string &path) {
    return stdext::file_size(path);
}
void CreateDirectories(const std::string &path) {
    stdext::create_directories(path);
}

std::string NormalizeSlashes(std::string path) {
    for (char &ch : path)
        if (ch == '\\')
            ch = '/';
    if (path.size() > 1 && path.back() == '/')
        path.pop_back();
    return path;
}
bool StartsWith(const std::string &text, const std::string &prefix) {
    return text.size() > prefix.size() && text.substr(0, prefix.size()) == prefix;
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

    //deduplicate
    std::set<std::string> resSet;
    int k = 0;
    for (int i = 0; i < resPaths.size(); i++) {
        auto pib = resSet.insert(resPaths[i]);
        if (pib.second)
            resPaths[k++] = resPaths[i];
    }
    resPaths.resize(k);

    return resPaths;
}

std::string DownloadSimple(const std::string &url, const std::string &rootDir, const char *printIndent = "") {
    std::string filepath;
    for (int i = 0; i < 100; i++) {
        filepath = rootDir + "/__download" + std::to_string(i) +  + "__" + ZipSync::GetFilename(url);
        if (!ZipSync::IfFileExists(filepath))
            break;
    }
    if (printIndent) {
        printf("%sDownloading %s to %s\n", printIndent, url.c_str(), filepath.c_str());
    }
    ZipSync::Downloader downloader;
    auto DataCallback = [&filepath](const void *data, int len) {
        ZipSync::StdioFileHolder f(filepath.c_str(), "wb");
        int res = fwrite(data, 1, len, f);
        if (res != len)
            throw std::runtime_error("Failed to write " + std::to_string(len) + " bytes downloaded from " + filepath);
    };
    downloader.EnqueueDownload(ZipSync::DownloadSource(url), DataCallback);
    downloader.DownloadAll();
    return filepath;
}

class ProgressIndicator {
    std::string content;
public:
    ~ProgressIndicator() {
        Finish();
    }
    void Erase() {
        if (content.empty())
            return;
        printf("\r");
        for (int i = 0; i < content.size(); i++)
            printf(" ");
        printf("\r");
        content.clear();
    }
    void Update(const char *line) {
        Erase();
        content = line;
        printf("%s", content.c_str());
    }
    void Update(double globalRatio, std::string globalComment, double localRatio = -1.0, std::string localComment = "") {
        auto PercentOf = [](double value) { return int(value * 100.0 + 0.5); };
        char buffer[1024];
        if (localRatio != -1.0 && localComment.size())
            sprintf(buffer, " %3d%% | %3d%% : %s : %s", PercentOf(globalRatio), PercentOf(localRatio), globalComment.c_str(), localComment.c_str());
        else
            sprintf(buffer, " %3d%%        : %s", PercentOf(globalRatio), globalComment.c_str());
        Update(buffer);
    }
private:
    void Finish() {
        if (content.empty())
            return;
        printf("\n");
    }
};

void ParallelFor(int from, int to, const std::function<void(int)> &body, int thrNum = -1, int blockSize = 1) {
    if (thrNum == 1) {
        for (int i = from; i < to; i++)
            body(i);
    }
    else {
        if (thrNum <= 0)
            thrNum = std::thread::hardware_concurrency();
        //int blockNum = (to - from + blockSize-1) / blockSize;

        std::vector<std::thread> threads(thrNum);
        int lastAssigned = from;
        std::exception_ptr workerException;
        std::mutex mutex;

        for (int t = 0; t < thrNum; t++) {
            auto ThreadFunc = [&,t]() {
                while (1) {
                    int left, right;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (workerException || lastAssigned == to)
                            break;
                        left = lastAssigned;
                        right = std::min(lastAssigned + blockSize, to);
                        lastAssigned = right;
                    }
                    try {
                        for (int i = left; i < right; i++) {
                            body(i);
                        }
                    } catch(...) {
                        std::lock_guard<std::mutex> lock(mutex);
                        workerException = std::current_exception();
                        break;
                    }
                }
            };
            threads[t] = std::thread(ThreadFunc);
        }
        for (int t = 0; t < thrNum; t++)
            threads[t].join();

        if (workerException)
            std::rethrow_exception(workerException);
    }
}

double TotalCompressedSize(const ZipSync::Manifest &mani, bool providedOnly = true) {
    double size = 0.0;
    for (int i = 0; i < mani.size(); i++) {
        if (providedOnly && mani[i].location == ZipSync::FileLocation::Nowhere)
            continue;
        size += mani[i].props.compressedSize;
    }
    return size;
}
int TotalCount(const ZipSync::Manifest &mani, bool providedOnly = true) {
    int cnt = 0;
    for (int i = 0; i < mani.size(); i++) {
        if (providedOnly && mani[i].location == ZipSync::FileLocation::Nowhere)
            continue;
        cnt++;
    }
    return cnt;
}

void DoClean(std::string root) {
    static std::string DELETE_PREFIXES[] = {"__reduced__", "__download", "__repacked__"};
    static std::string RESTORE_PREFIXES[] = {"__repacked__"};

    std::vector<std::string> allFiles = EnumerateFilesInDirectory(root);
    for (std::string filename : allFiles) {
        std::string fn = ZipSync::GetFilename(filename);

        bool shouldDelete = false;
        for (const std::string &p : DELETE_PREFIXES)
            if (StartsWith(fn, p))
                shouldDelete = true;
        if (!shouldDelete)
            continue;

        std::string shouldRestore;
        for (const std::string &p : RESTORE_PREFIXES)
            if (StartsWith(fn, p))
                shouldRestore = fn.substr(p.size());
        if (!shouldRestore.empty()) {
            std::string fullOldPath = root + '/' + filename;
            std::string fullNewPath = root + '/' + shouldRestore;
            if (!ZipSync::IfFileExists(fullNewPath)) {
                printf("Restoring %s...\n", fullNewPath.c_str());
                ZipSync::RenameFile(fullOldPath, fullNewPath);
                continue;
            }
        }

        std::string fullPath = root + '/' + filename;
        printf("Deleting %s...\n", fullPath.c_str());
        ZipSync::RemoveFile(fullPath);
    }
}
void CommandClean(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "the root directory to clean after repack\n", {'r', "root"});
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::HiddenFromDescription);
    parser.Parse();

    std::string root = GetCwd();
    if (argRootDir)
        root = argRootDir.Get();
    root = NormalizeSlashes(root);
    DoClean(root);
}

void DoNormalize(std::string root, std::string outDir, std::vector<std::string> zipPaths) {
    double totalSize = 1.0, doneSize = 0.0;
    for (auto zip : zipPaths)
        totalSize += SizeOfFile(zip);
    printf("Going to normalize %d zips in %s%s of total size %0.3lf MB\n", int(zipPaths.size()), (root.empty() ? "nowhere" : root.c_str()), (outDir.empty() ? " inplace" : ""), totalSize * 1e-6);

    {
        ProgressIndicator progress;
        for (std::string zip : zipPaths) {
            progress.Update(doneSize / totalSize, ("Normalizing \"" + zip + "\"...").c_str());
            doneSize += SizeOfFile(zip);
            if (!outDir.empty()) {
                std::string rel = ZipSync::PathAR::FromAbs(zip, root).rel;
                std::string zipOut = ZipSync::PathAR::FromRel(rel, outDir).abs;
                ZipSync::CreateDirectoriesForFile(zipOut, outDir);
                ZipSync::minizipNormalize(zip.c_str(), zipOut.c_str());
            }
            else
                ZipSync::minizipNormalize(zip.c_str());
        }
        progress.Update(1.0, "Normalizing done");
    }
}
void CommandNormalize(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "Relative paths to zips are based from this directory", {'r', "root"});
    args::PositionalList<std::string> argZips(parser, "zips", "List of files or globs specifying which zips in root directory to include", args::Options::Required);
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

    DoNormalize(root, outDir, zipPaths);
}

void CommandAnalyze(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "Manifests would contain paths relative to this root directory\n"
        "(all relative paths are based from the root directory)", {'r', "root"});
    args::Flag argClean(parser, "clean", "Run \"clean\" command before doing analysis", {'c', "clean"});
    args::Flag argNormalize(parser, "normalize", "Run \"normalize\" command before doing analysis", {'n', "normalize"});
    args::ValueFlag<std::string> argManifest(parser, "mani", "Path where full manifest would be written (default: manifest.iniz)", {'m', "manifest"}, "manifest.iniz");
    args::ValueFlag<int> argThreads(parser, "threads", "Use this number of parallel threads to accelerate analysis (0 = max)", {'j', "threads"}, 1);
    args::PositionalList<std::string> argZips(parser, "zips", "List of files or globs specifying which zips in root directory to analyze", args::Options::Required);
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::HiddenFromDescription);
    parser.Parse();

    std::string root = GetCwd();
    if (argRootDir)
        root = argRootDir.Get();
    root = NormalizeSlashes(root);
    std::string maniPath = GetPath(argManifest.Get(), root);
    int threadsNum = argThreads.Get();

    if (argClean)
        DoClean(root);
    std::vector<std::string> zipPaths = CollectFilePaths(argZips.Get(), root);
    if (argNormalize)
        DoNormalize(root, "", zipPaths);

    double totalSize = 1.0, doneSize = 0.0;
    for (auto zip : zipPaths)
        totalSize += SizeOfFile(zip);
    printf("Going to analyze %d zips in %s of total size %0.3lf MB in %d threads\n", int(zipPaths.size()), root.c_str(), totalSize * 1e-6, threadsNum);

    std::vector<ZipSync::Manifest> zipManis(zipPaths.size());
    {
        ProgressIndicator progress;
        std::mutex mutex;
        ParallelFor(0, zipPaths.size(), [&](int index) {
            std::string zipPath = zipPaths[index];
            {
                std::lock_guard<std::mutex> lock(mutex);
                progress.Update(doneSize / totalSize, "Analysing \"" + zipPath + "\"...");
            }
            zipManis[index].AppendLocalZip(zipPath, root, "");
            {
                std::lock_guard<std::mutex> lock(mutex);
                doneSize += SizeOfFile(zipPath);
                progress.Update(doneSize / totalSize, "Analysed  \"" + zipPath + "\"...");
            }
        }, threadsNum);
        progress.Update(1.0, "Analysing done");
    }

    ZipSync::Manifest manifest;
    for (const auto &tm : zipManis)
        manifest.AppendManifest(tm);
    ZipSync::WriteIniFile(maniPath.c_str(), manifest.WriteToIni());
}

void CommandDiff(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "The set of zips is located in this root directory\n"
        "(all relative paths are based from it)", {'r', "root"});
    args::ValueFlag<std::string> argManifest(parser, "mani", "Path to provided manifest of the zips set", {'m', "manifest"}, "manifest.iniz");
    args::ValueFlagList<std::string> argSubtractedMani(parser, "subm", "Paths or URLs of provided manifests being subtracted", {'s', "subtract"}, {}, args::Options::Required);
    args::ValueFlag<std::string> argOutDir(parser, "output", "Difference zips and manifests will be written to this directory", {'o', "output"}, args::Options::Required);
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::HiddenFromDescription);
    parser.Parse();

    std::string root = GetCwd();
    if (argRootDir)
        root = argRootDir.Get();
    root = NormalizeSlashes(root);
    std::string outRoot = root;
    if (argOutDir)
        outRoot = argOutDir.Get();
    outRoot = NormalizeSlashes(outRoot);
    std::string maniPath = GetPath(argManifest.Get(), root);
    std::string outManiPath = GetPath(argManifest.Get(), outRoot);
    if (EnumerateFilesInDirectory(outRoot).size() > 0)
        throw std::runtime_error("Output directory is not empty: " + outRoot);
    CreateDirectories(outRoot);

    ZipSync::Manifest fullMani;
    fullMani.ReadFromIni(ZipSync::ReadIniFile(maniPath.c_str()), root);
    printf("Subtracting from %s containing %d files of size %0.3lf MB:\n", 
        maniPath.c_str(), TotalCount(fullMani), TotalCompressedSize(fullMani) * 1e-6
    );
    std::set<ZipSync::HashDigest> subtractedHashes;
    for (std::string path : argSubtractedMani.Get()) {
        path = NormalizeSlashes(path);
        std::string localPath = path;
        if (ZipSync::PathAR::IsHttp(path))
            localPath = DownloadSimple(path, outRoot, "  ");
        std::string providedRoot = ZipSync::GetDirPath(path);
        ZipSync::Manifest mani;
        mani.ReadFromIni(ZipSync::ReadIniFile(localPath.c_str()), providedRoot);
        printf("   %s containing %d files of size %0.3lf MB\n", 
            path.c_str(), TotalCount(mani), TotalCompressedSize(mani) * 1e-6
        );
        for (int i = 0; i < mani.size(); i++)
            subtractedHashes.insert(mani[i].compressedHash);
    }

    ZipSync::Manifest filteredMani;
    ZipSync::Manifest subtractedMani;
    for (int i = 0; i < fullMani.size(); i++) {
        auto &pf = fullMani[i];
        if (subtractedHashes.count(pf.compressedHash))
            subtractedMani.AppendFile(pf);
        else
            filteredMani.AppendFile(pf);
    }
    printf("Result will be written to %s containing %d files of size %0.3lf MB\n", 
        outRoot.c_str(), TotalCount(filteredMani), TotalCompressedSize(filteredMani) * 1e-6
    );

    ZipSync::UpdateProcess update;
    update.Init(filteredMani, filteredMani, outRoot);
    bool ok = update.DevelopPlan(ZipSync::UpdateType::SameCompressed);
    if (!ok)
        throw std::runtime_error("Internal error: DevelopPlan failed");
    update.RepackZips();
    ZipSync::Manifest provMani = update.GetProvidedManifest();

    fullMani = provMani.Filter([](const ZipSync::FileMetainfo &f) {
        return f.location == ZipSync::FileLocation::Inplace;
    });
    for (int i = 0; i < subtractedMani.size(); i++) {
        auto &pf = subtractedMani[i];
        pf.DontProvide();
        fullMani.AppendFile(pf);
    }
    printf("Saving manifest of the diff to %s\n", outManiPath.c_str());
    ZipSync::WriteIniFile(outManiPath.c_str(), fullMani.WriteToIni());
}

void CommandUpdate(args::Subparser &parser) {
    args::ValueFlag<std::string> argRootDir(parser, "root", "The update should create/update the set of zips in this root directory\n"
        "(all relative paths are based from the root directory)", {'r', "root"});
    args::ValueFlag<std::string> argTargetMani(parser, "trgMani", "Path to the target manifest to update to", {'t', "target"}, "manifest.iniz", args::Options::Required);
    args::ValueFlagList<std::string> argProvidedMani(parser, "provMani", "Path to additional provided manifests describing where to take files from", {'p', "provided"}, {});
    args::Flag argClean(parser, "clean", "Run \"clean\" command before and after update", {'c', "clean"});
    args::PositionalList<std::string> argManagedZips(parser, "managed", "List of files or globs specifying which zips must be updated");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::HiddenFromDescription);
    parser.Parse();

    std::string root = GetCwd();
    if (argRootDir)
        root = argRootDir.Get();
    root = NormalizeSlashes(root);
    std::string targetManiPath = GetPath(argTargetMani.Get(), root);
    CreateDirectories(root);
    if (argClean.Get())
        DoClean(root);

    std::vector<std::string> providManiPaths = CollectFilePaths(argProvidedMani.Get(), root);
    std::vector<std::string> managedZips = CollectFilePaths(argManagedZips.Get(), root);

    ZipSync::Manifest targetManifest;
    ZipSync::Manifest providedManifest;
    std::string targetManiLocalPath = targetManiPath;
    if (ZipSync::PathAR::IsHttp(targetManiPath))
        targetManiLocalPath = DownloadSimple(targetManiPath, root, "");
    targetManifest.ReadFromIni(ZipSync::ReadIniFile(targetManiLocalPath.c_str()), root);
    printf("Updating directory %s to target %s with %d files of size %0.3lf MB\n",
        root.c_str(), targetManiPath.c_str(), TotalCount(targetManifest, false), TotalCompressedSize(targetManifest, false) * 1e-6
    );
    printf("Provided manifests:\n");
    {
        std::string srcDir = ZipSync::GetDirPath(targetManiPath);
        ZipSync::Manifest mani = targetManifest.Filter([](const ZipSync::FileMetainfo &f) {
            return f.location != ZipSync::FileLocation::Nowhere;
        });
        mani.ReRoot(srcDir);
        printf("  %s containing %d files of size %0.3lf MB\n",
            targetManiPath.c_str(), TotalCount(mani), TotalCompressedSize(mani) * 1e-6
        );
        providedManifest.AppendManifest(mani);
    }
    for (std::string provManiPath : providManiPaths) {
        std::string srcDir = ZipSync::GetDirPath(provManiPath);
        std::string provManiLocalPath = provManiPath;
        if (ZipSync::PathAR::IsHttp(provManiPath))
            provManiLocalPath = DownloadSimple(provManiPath, root, "  ");
        ZipSync::Manifest mani;
        mani.ReadFromIni(ZipSync::ReadIniFile(provManiLocalPath.c_str()), srcDir);
        mani = mani.Filter([](const ZipSync::FileMetainfo &f) {
            return f.location != ZipSync::FileLocation::Nowhere;
        });
        printf("  %s containing %d files of size %0.3lf MB\n",
            provManiPath.c_str(), TotalCount(mani), TotalCompressedSize(mani) * 1e-6
        );
        providedManifest.AppendManifest(mani);
    }

    ZipSync::UpdateProcess update;
    update.Init(targetManifest, providedManifest, root);
    if (managedZips.size())
        printf("Managing %d zip files\n", (int)managedZips.size());
    for (int i = 0; i < managedZips.size(); i++)
        update.AddManagedZip(managedZips[i]);
    bool ok = update.DevelopPlan(ZipSync::UpdateType::SameCompressed);
    if (!ok) {
        int n = update.MatchCount();
        std::vector<ZipSync::ManifestIter> misses;
        for (int i = 0; i < n; i++) {
            const auto &m = update.GetMatch(i);
            if (!m.provided)
                misses.push_back(m.target);
        }
        std::shuffle(misses.begin(), misses.end(), std::mt19937(time(0)));
        n = misses.size();
        int k = std::min(n, 10);
        printf("Here are some of the missing files (%d out of %d):\n", k, n);
        for (int i = 0; i < k; i++) {
            printf("  %s||%s of size = %d/%d with hash = %s/%s\n",
                misses[i]->zipPath.rel.c_str(),
                misses[i]->filename.c_str(),
                misses[i]->props.compressedSize,
                misses[i]->props.contentsSize,
                misses[i]->compressedHash.Hex().c_str(),
                misses[i]->contentsHash.Hex().c_str()
            );
        }
        throw std::runtime_error("DevelopPlan failed: provided manifests not enough");
    }
    printf("Update plan developed\n");

    uint64_t bytesTotal = 0, bytesRemote = 0;
    int numTotal = 0, numRemote = 0;
    for (int i = 0; i < update.MatchCount(); i++) {
        const auto &m = update.GetMatch(i);
        uint32_t size = m.provided->byterange[1] - m.provided->byterange[0];
        if (m.provided->location == ZipSync::FileLocation::RemoteHttp) {
            numRemote++;
            bytesRemote += size;
        }
        numTotal++;
        bytesTotal += size;
    }
    printf("To be downloaded:\n");
    printf("  %d/%d files of size %0.0lf/%0.0lf MB (%0.2lf%%)\n", numRemote, numTotal, 1e-6 * bytesRemote, 1e-6 * bytesTotal, 100.0 * bytesRemote/bytesTotal);

    printf("Downloading missing files...\n");
    {
        ProgressIndicator progress;
        update.DownloadRemoteFiles([&progress](double ratio, const char *comment) {
            progress.Update(ratio, comment);
        });
        progress.Update(1.0, "All downloads complete");
    }
    printf("Repacking zips...\n");
    update.RepackZips();
    ZipSync::Manifest provMani = update.GetProvidedManifest();

    provMani = provMani.Filter([](const ZipSync::FileMetainfo &f) {
        return f.location == ZipSync::FileLocation::Inplace;
    });
    std::string resManiPath = GetPath("manifest.iniz", root);
    printf("Saving resulting manifest to %s\n", resManiPath.c_str());
    ZipSync::WriteIniFile(resManiPath.c_str(), provMani.WriteToIni());

    if (argClean.Get())
        DoClean(root);
}

int main(int argc, char **argv) {
    args::ArgumentParser parser("ZipSync command line tool.");
    parser.helpParams.programName = "zipsync";
    parser.helpParams.width = 120;
    parser.helpParams.flagindent = 4;
    parser.helpParams.showTerminator = false;
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::Command clean(parser, "clean", "Delete temporary and intermediate files after repacking", CommandClean);
    args::Command normalize(parser, "normalize", "Normalize specified set of zips (on local machine)", CommandNormalize);
    args::Command analyze(parser, "analyze", "Create manifests for specified set of zips (on local machine)", CommandAnalyze);
    args::Command diff(parser, "diff", "Remove files available in given manifests from the set of zips", CommandDiff);
    args::Command update(parser, "update", "Perform update of the set of zips to specified target", CommandUpdate);
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
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 2;
    }
    return 0;
}
