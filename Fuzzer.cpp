#include "Fuzzer.h"
#include <string>
#include <vector>
#include <random>
#include <set>
#include <map>
#include "StdString.h"
#include "StdFilesystem.h"
#include "ZSAssert.h"
#include "ZipSync.h"
#include "HttpServer.h"
#include <zip.h>
#include <functional>


namespace ZipSync {

typedef std::uniform_int_distribution<int> IntD;
typedef std::uniform_real_distribution<double> DblD;

//==========================================================================================

class FuzzerGenerator {
protected:
    std::mt19937 _rnd;
    UpdateType _updateType = UpdateType::SameCompressed;

public:
    void SetSeed(int seed) {
        _rnd.seed(seed);
    }
    void SetUpdateType(UpdateType type) {
        _updateType = type;
    }

    template<class C> typename C::value_type & RandomFrom(C &cont) {
        int idx = IntD(0, cont.size() - 1)(_rnd);
        auto iter = cont.begin();
        std::advance(iter, idx);
        return *iter;
    }
    template<class C> const typename C::value_type & RandomFrom(const C &cont) {
        return RandomFrom(const_cast<C&>(cont));
    }

    std::vector<int> GenPartition(int sum, int cnt, int minV = 0) {
        sum -= cnt * minV;
        ZipSyncAssert(sum >= 0);
        std::vector<int> res;
        for (int i = 0; i < cnt; i++) {
            double avg = double(sum) / (cnt - i);
            int val = IntD(0, int(2*avg))(_rnd);
            val = std::min(val, sum);
            if (i+1 == cnt)
                val = sum;
            res.push_back(minV + val);
            sum -= val;
        }
        std::shuffle(res.begin(), res.end(), _rnd);
        return res;
    }

    std::string GenExtension() {
        static const std::vector<std::string> extensions = {
            ".txt", ".bin", ".dat", ".jpg", ".png", ".mp4", ".md5mesh", ".lwo",
            ".exe", ".ini", ".zip", ".pk4"
        };
        return RandomFrom(extensions);
    }

    std::string GenName() {
        std::string res;
        int len = IntD(0, 1)(_rnd) ? IntD(3, 10)(_rnd) : IntD(1, 3)(_rnd);
        for (int i = 0; i < len; i++) {
            int t = IntD(0, 3)(_rnd);
            char ch = 0;
            if (t == 0) ch = IntD('0', '9')(_rnd);
            if (t == 1) ch = IntD('a', 'z')(_rnd);
            if (t == 2) ch = IntD('A', 'Z')(_rnd);
            if (t == 3) ch = (IntD(0, 1)(_rnd) ? ' ' : '_');
            res += ch;
        }
        //trailing spaces don't work well in Windows
        if (res.front() == ' ') res.front() = '_';
        if (res.back() == ' ') res.back() = '_';
        //avoid Windows reserved names: CON, PRN, AUX, NUL, COM1, ...
        if ((res.size() == 3 || res.size() == 4) && isalpha(res[0]) && isalpha(res[1]) && isalpha(res[2]))
            res[0] = '_';
        return res;
    }

    std::vector<std::string> GenPaths(int number, const char *extension = nullptr) {
        std::vector<std::string> res;
        std::set<std::string> used;
        while (res.size() < number) {
            std::string path;
            if (res.empty() || IntD(0, 99)(_rnd) < 20) {
                int depth = IntD(0, 2)(_rnd);
                for (int i = 0; i < depth; i++)
                    path += GenName() + "/";
                path += GenName() + (extension ? extension : GenExtension());
            }
            else {
                std::string base = RandomFrom(res);
                std::vector<std::string> terms;
                stdext::split(terms, base, "/");
                int common = IntD(0, terms.size()-1)(_rnd);
                terms.resize(common);
                int want = IntD(0, 2)(_rnd);
                while (terms.size() < want)
                    terms.push_back(GenName());
                path = stdext::join(terms, "/");
                if (!path.empty()) path += "/";
                path += GenName() + (extension ? extension : GenExtension());
            }
            if (used.insert(path).second)
                res.push_back(path);
        }
        std::shuffle(res.begin(), res.end(), _rnd);
        return res;
    }

    std::vector<uint8_t> GenFileContents() {
        int pwr = IntD(0, 10)(_rnd);
        int size = IntD((1<<pwr)-1, 2<<pwr)(_rnd);
        std::vector<uint8_t> res;
        int t = IntD(0, 3)(_rnd);
        if (t == 0) {
            for (int i = 0; i < size; i++)
                res.push_back(IntD(0, 255)(_rnd));
        }
        else if (t == 1) {
            for (int i = 0; i < size/4; i++) {
                int pwr = IntD(0, 30)(_rnd);
                int value = IntD((1<<pwr)-1, (2<<pwr)-1)(_rnd);
                for (int j = 0; j < 4; j++) {
                    res.push_back(value & 0xFF);
                    value >>= 8;
                }
            }
        }
        else if (t == 2) {
            std::string text;
            while (text.size() < size) {
                char buff[128];
                double x = DblD(-100.0, 100.0)(_rnd);
                double y = DblD(-10.0, 30.0)(_rnd);
                double z = DblD(0.0, 1.0)(_rnd);
                sprintf(buff, "%0.3lf %0.6lf %0.10lf\n", x, y, z);
                text += buff;
            }
            res.resize(text.size());
            memcpy(res.data(), text.data(), text.size());
        }
        else if (t == 3) {
            std::string source = R"(
Sample: top 60,000 lemmas and ~100,000 word forms (both sets included for the same price) 	Top 20,000 or 60,000 lemmas: simple word list, frequency by genre, or as an eBook. 	Top 100,000 word forms. Also contains information on COCA genres, and frequency in the BNC (British), SOAP (informal) and COHA (historical)
  	
rank 	  lemma / word 	PoS 	freq 	range 	range10
7371 	  brew 	v 	94904 	0.06 	0.01
17331 	  useable 	j 	17790 	0.02 	0.00
27381 	  uppercase 	n 	5959 	0.02 	0.00
37281 	  half-naked 	j 	2459 	0.00 	0.00
47381 	  bellhop 	n 	1106 	0.00 	0.00
57351 	  tetherball 	n 	425 	0.00 	0.00
	
rank 	  lemma / word 	PoS 	freq 	dispersion
7309 	  attic 	n 	2711 	0.91
17311 	  tearful 	j 	542 	0.93
27303 	  tailgate 	v 	198 	0.85
37310 	  hydraulically 	r 	78 	0.83
47309 	  unsparing 	j 	35 	0.83
57309 	  embryogenesis 	n 	22 	0.66
            )";
            std::string text;
            while (text.size() < size) {
                int l = IntD(0, source.size())(_rnd);
                int r = IntD(0, source.size())(_rnd);
                if (r < l) std::swap(l, r);
                int rem = size - text.size();
                r = std::min(r, l + rem);
                text += source.substr(l, r-l);
            }
            res.resize(text.size());
            memcpy(res.data(), text.data(), text.size());
        }
        return res;
    }

    struct InZipParams {
        int method;
        int level;
        uint32_t dosDate;
        uint16_t internalAttribs;
        uint32_t externalAttribs;

        bool operator==(const InZipParams &b) const {
            return (
                std::tie(method, level, dosDate, internalAttribs, externalAttribs) == 
                std::tie(b.method, b.level, b.dosDate, b.internalAttribs, b.externalAttribs)
            );
        }
    };
    InZipParams GenInZipParams() {
        InZipParams res;
        res.method = (IntD(0, 2)(_rnd) > 0 ? 8 : 0);
        res.level = res.method ? IntD(Z_BEST_SPEED, Z_BEST_COMPRESSION)(_rnd) : 0;
        res.dosDate = IntD(INT_MIN, INT_MAX)(_rnd);
        res.internalAttribs = IntD(INT_MIN, INT_MAX)(_rnd);
        res.externalAttribs = IntD(INT_MIN, INT_MAX)(_rnd);
        res.externalAttribs &= ~0xFF;   //zero 0-th byte: it indicates directories
        return res;
    }

    struct InZipFile {
        InZipParams params;
        std::vector<uint8_t> contents;
    };
    typedef std::vector<std::pair<std::string, InZipFile>> InZipState;
    typedef std::map<std::string, InZipState> DirState;

    bool DoFilesMatch(const InZipFile &a, const InZipFile &b) const {
        if (a.contents != b.contents)
            return false;
        if (_updateType == UpdateType::SameCompressed && !(a.params.method == b.params.method && a.params.level == b.params.level))
            return false;
        return true;
    }

    DirState GenTargetState(int numFiles, int numZips) {
        std::vector<std::string> zipPaths = GenPaths(numZips, ".zip");
        std::vector<int> fileCounts = GenPartition(numFiles, numZips);
        DirState state;
        for (int i = 0; i < numZips; i++) {
            int k = fileCounts[i];
            InZipState inzip;
            std::vector<std::string> filePaths = GenPaths(k);
            for (int j = 0; j < k; j++) {
                auto params = GenInZipParams();
                auto contents = GenFileContents();
                inzip.emplace_back(filePaths[j], InZipFile{std::move(params), std::move(contents)});
            }
            state[zipPaths[i]] = std::move(inzip);
        }
        return state;
    }

    DirState GenMutatedState(const DirState &source) {
        DirState state;

        std::vector<std::string> appendableZips;

        int sameZips = IntD(0, source.size() * 2/3)(_rnd);
        for (int i = 0; i < sameZips; i++) {
            bool samePath = (IntD(0, 99)(_rnd) < 75);
            bool appendable = (IntD(0, 99)(_rnd) < 50);
            bool incomplete = (IntD(0, 99)(_rnd) < 30);
            const auto &pPZ = RandomFrom(source);
            std::string filename = (samePath ? pPZ.first : GenPaths(1)[0]);
            InZipState inzip = pPZ.second;
            if (incomplete) {
                int removeCnt = IntD(0, inzip.size()/2)(_rnd);
                for (int j = 0; j < removeCnt; j++)
                    inzip.erase(inzip.begin() + IntD(0, inzip.size()-1)(_rnd));
            }
            if (appendable)
                appendableZips.push_back(filename);
            state[filename] = std::move(inzip);
        }

        std::vector<InZipState::const_iterator> sourceFiles;
        std::vector<std::string> candidatePaths;
        for (const auto &zipPair : source) {
            const auto &files = zipPair.second;
            for (auto iter = files.cbegin(); iter != files.cend(); iter++) {
                sourceFiles.push_back(iter);
                candidatePaths.push_back(iter->first);
            }
        }
        {
            auto np = GenPaths(candidatePaths.size() + 1);
            candidatePaths.insert(candidatePaths.end(), np.begin(), np.end());
        }

        std::vector<InZipFile> appendFiles;
        int sameFiles = IntD(0, sourceFiles.size())(_rnd);
        for (int i = 0; i < sameFiles; i++) {
            auto iter = RandomFrom(sourceFiles);
            InZipParams params = IntD(0, 1)(_rnd) ? iter->second.params : GenInZipParams();
            appendFiles.push_back(InZipFile{params, iter->second.contents});
        }
        int rndFiles = IntD(0, sourceFiles.size())(_rnd);
        for (int i = 0; i < rndFiles; i++) {
            auto params = GenInZipParams();
            auto contents = GenFileContents();
            appendFiles.push_back(InZipFile{params, std::move(contents)});
        }

        {
            auto np = GenPaths(appendableZips.size() + 1, ".zip");
            appendableZips.insert(appendableZips.end(), np.begin(), np.end());
        }
        for (auto& f : appendFiles) {
            std::string zipPath = RandomFrom(appendableZips);
            InZipState &inzip = state[zipPath];
            std::string path = RandomFrom(candidatePaths);
            int pos = IntD(0, 1)(_rnd) || inzip.empty() ? inzip.size() : IntD(0, inzip.size()-1)(_rnd);
            inzip.insert(inzip.begin() + pos, std::make_pair(std::move(path), std::move(f)));
        }

        return state;
    }

    void TryAddFullZip(DirState &target, std::vector<DirState*> provided) {
        if (IntD(0, 99)(_rnd) >= 40)
            return;
        int numZips = IntD(1, 2)(_rnd);
        DirState added = GenTargetState(IntD(numZips, 4)(_rnd), numZips);

        std::vector<std::string> zipnames;
        for (const auto &zipPair : added)
            zipnames.push_back(zipPair.first);
        for (const auto &zipPair : target)
            zipnames.push_back(zipPair.first);
        for (auto *prov : provided)
            for (const auto &zipPair : *prov)
                zipnames.push_back(zipPair.first);

        if (IntD(0, 99)(_rnd) < 50) {
            DirState ns;
            for (auto &zipPair : added) {
                std::string newName = RandomFrom(zipnames);
                ns[newName] = zipPair.second;
            }
            added = ns;
        }

        for (const auto &zipPair : added) {
            for (const auto &filePair : zipPair.second)
                target[zipPair.first].push_back(filePair);
            int mult = IntD(1, 2)(_rnd);
            for (int i = 0; i < mult; i++) {
                DirState &other = *RandomFrom(provided);
                std::string zipName = (IntD(0, 99)(_rnd) < 50 ? GenPaths(1, ".zip")[0] : RandomFrom(zipnames));
                for (const auto &filePair : zipPair.second)
                    other[zipName].push_back(filePair);
            }
        }
    }

    bool AddMissingFiles(const DirState &target, std::vector<DirState*> provided, bool leaveMisses = false) {
        std::vector<const InZipFile*> targetFiles;
        for (const auto &zipPair : target)
            for (const auto &filePair : zipPair.second)
                targetFiles.push_back(&filePair.second);

        std::vector<const InZipFile*> providedFiles;
        for (const DirState *dir : provided) {
            for (const auto &zipPair : *dir)
                for (const auto &filePair : zipPair.second)
                    providedFiles.push_back(&filePair.second);
        }

        int k = 0;
        for (const InZipFile *tf : targetFiles) {
            bool present = false;
            for (const InZipFile *pf : providedFiles) {
                if (DoFilesMatch(*tf, *pf))
                    present = true;
            }
            if (!present)
                targetFiles[k++] = tf;
        }
        targetFiles.resize(k);

        if (targetFiles.empty())
            return true;
        bool surelySucceed = true;
        if (leaveMisses && IntD(0, 1)(_rnd)) {
            targetFiles.resize(IntD(k/2, k-1)(_rnd));
            surelySucceed = false;
        }

        auto filePaths = GenPaths(targetFiles.size());
        for (int i = 0; i < targetFiles.size(); i++) {
            DirState *dst = RandomFrom(provided);
            std::string zipPath = GenPaths(1, ".zip")[0];
            InZipState &inzip = (*dst)[zipPath];
            std::string path = filePaths[i];
            int pos = IntD(0, inzip.size())(_rnd);
            inzip.insert(inzip.begin() + pos, std::make_pair(path, *targetFiles[i]));
        }
        return surelySucceed;
    }

    void SplitState(const DirState &full, std::vector<DirState> &parts) {
        int n = parts.size();
        for (int i = 0; i < n; i++)
             parts[i].clear();
        std::vector<char> used;
        for (const auto &zipPair : full)
            for (const auto &filePair : zipPair.second) {
                int q = (IntD(0, 99)(_rnd) < 25 ? 2 : 1);
                used.assign(n, false);
                for (int j = 0; j < q; j++) {
                    int x = IntD(0, n-1)(_rnd);
                    if (used[x]) continue;
                    used[x] = true;
                    parts[x][zipPair.first].push_back(filePair);
                }
            }
    }

    static bool ArePathsCaseAliased(std::string pathA, std::string pathB) {
#ifndef _WIN32
        return false;  //case-sensitive
#else
        pathA += '/';
        pathB += '/';
        int k = 0;
        //find common case-insensitive prefix
        while (k < pathA.size() && k < pathB.size() && tolower(pathA[k]) == tolower(pathB[k]))
            k++;
        //trim end up to last slash
        while (k > 0 && pathA[k-1] != '/')
            k--;
        //check that prefixes are same
        std::string prefixA = pathA.substr(0, k);
        std::string prefixB = pathB.substr(0, k);
        if (prefixA != prefixB)
            return true;
        return false;
#endif
    }
    bool CheckForCaseAliasing(const DirState &state1, const DirState &state2) {
        for (const auto &zp1 : state1) {
            for (const auto &zp2 : state2)
                if (ArePathsCaseAliased(zp1.first, zp2.first))
                    return true;
        }
        return false;
    }

    void WriteState(const std::string &localRoot, const std::string &remoteRoot, const DirState &state, Manifest *mani) {
        Manifest addedMani;
        for (const auto &zipPair : state) {
            PathAR zipPath = PathAR::FromRel(zipPair.first, localRoot);
            stdext::create_directories(stdext::path(zipPath.abs).parent_path());

            //note: minizip's unzip cannot work with empty zip files
            if (zipPair.second.empty())
                continue;

            ZipFileHolder zf(zipPath.abs.c_str());
            for (const auto &filePair : zipPair.second) {
                const auto &params = filePair.second.params;
                const auto &contents = filePair.second.contents;
                zip_fileinfo info;
                info.dosDate = params.dosDate;
                info.internal_fa = params.internalAttribs;
                info.external_fa = params.externalAttribs;
                SAFE_CALL(zipOpenNewFileInZip(zf, filePair.first.c_str(), &info, NULL, 0, NULL, 0, NULL, params.method, params.level));
                SAFE_CALL(zipWriteInFileInZip(zf, contents.data(), contents.size()));
                SAFE_CALL(zipCloseFileInZip(zf));
            }
            zf.reset();

            addedMani.AppendLocalZip(zipPath.abs, localRoot, "default");
        }
        addedMani.ReRoot(remoteRoot);
        if (mani)
            mani->AppendManifest(addedMani);
    }
};

//==========================================================================================

class Fuzzer : private FuzzerGenerator {
    struct SourcePath {
        std::string localDir;
        std::string urlDir;     //same as localDir if used locally
    };
    //directories where everything happens
    std::string _baseDir;
    std::string _rootTargetDir;
    std::string _rootInplaceDir;
    std::vector<SourcePath> _rootSources;       //includes local and remote directories

    //directory contents: generated randomly
    DirState _initialTargetState;
    DirState _initialInplaceState;
    DirState _initialAllSourcesState;
    std::vector<DirState> _initialSourceState;
    bool _shouldUpdateSucceed;

    //initial manifests passed to the updater
    Manifest _initialTargetMani;
    Manifest _initialProvidedMani;

    //http servers (serving content for remote directories)
    bool _remoteEnabled = false;
    std::vector<std::unique_ptr<HttpServer>> _httpServers;

    //the update class
    std::unique_ptr<UpdateProcess> _updater;

    //various counters (increased every run)
    int _numCasesGenerated = 0;         //generated
    int _numCasesValidated = 0;         //passed to updater (valid)
    int _numCasesShouldSucceed = 0;     //should succeed according to prior knowledge
    int _numCasesActualSucceed = 0;     //actually updated successfully

    //various manifests after update
    Manifest _finalComputedProvidedMani;    //computed by UpdateProcess during update
    Manifest _finalActualTargetMani;        //real contents of "inplace" dir (without "reduced" zips)
    Manifest _finalActualProvidedMani;      //real contents of "inplace" dir (includes "reduced" zips)

    void AssertManifestsSame(IniData &&iniDataA, std::string dumpFnA, IniData &&iniDataB, std::string dumpFnB, bool ignoreCompressedHash = false, bool ignoreByterange = false) const {
        auto ClearCompressedHash = [&](IniData &ini) {
            for (auto& pSect : ini) {
                for (auto &pProp : pSect.second) {
                    if (ignoreCompressedHash) {
                        if (pProp.first == "compressedHash" || pProp.first == "compressedSize")
                            pProp.second = "(removed)";
                    }
                    if (ignoreByterange) {
                        if (pProp.first == "byterange")
                            pProp.second = "(removed)";
                    }
                    if (pProp.first == "package")
                        pProp.second = "(removed)";
                }
                if (pSect.first.find("__download") != std::string::npos) {
                    for (auto &pProp : pSect.second)
                        if (pProp.first == "internalAttribs" || pProp.first == "externalAttribs")
                            pProp.second = "(removed)";
                }
            }
        };
        ClearCompressedHash(iniDataA);
        ClearCompressedHash(iniDataB);
        if (iniDataA != iniDataB) {
            WriteIniFile((_baseDir + "/" + dumpFnA).c_str(), iniDataA);
            WriteIniFile((_baseDir + "/" + dumpFnB).c_str(), iniDataB);
        }
        ZipSyncAssert(iniDataA == iniDataB);
    }

public:
    void SetRemoteEnabled(bool enabled) {
        _remoteEnabled = enabled;
        while (_remoteEnabled && _httpServers.size() < 3) {
            int idx = _httpServers.size();
            _httpServers.emplace_back(new HttpServer());
            _httpServers[idx]->SetPortNumber(HttpServer::PORT_DEFAULT + idx);
            _httpServers[idx]->SetBlockSize(30 + idx * 7);   //for more extensive testing 
            _httpServers[idx]->Start();
        }
        while (!_remoteEnabled && _httpServers.size() > 0)
            _httpServers.pop_back();
    }

    void GenerateInput(std::string baseDir, int seed) {
        _baseDir = baseDir;
        SetSeed(seed);

        _rootTargetDir = _baseDir + "/target";
        _rootInplaceDir = _baseDir + "/inplace";
        _rootSources.clear();
        _rootSources.push_back(SourcePath{_baseDir + "/local", _baseDir + "/local"});
        if (_remoteEnabled) {
            int k = IntD(0, 2)(_rnd);
            for (int i = 0; i < k; i++) {
                SourcePath sp = {_baseDir + "/remote" + std::to_string(i), _httpServers[i]->GetRootUrl()};
                _rootSources.push_back(sp);
                _httpServers[i]->SetRootDir(sp.localDir);
            }
        }

        SetUpdateType(seed % 2 ? UpdateType::SameCompressed : UpdateType::SameContents);

        _initialTargetState = GenTargetState(50, 10);
        _initialInplaceState = GenMutatedState(_initialTargetState);
        _initialAllSourcesState = GenMutatedState(_initialTargetState);
        _initialSourceState.assign(_rootSources.size(), {});
        SplitState(_initialAllSourcesState, _initialSourceState);
        std::vector<DirState*> provStates = {&_initialInplaceState};
        for (auto &s : _initialSourceState) provStates.push_back(&s);
        TryAddFullZip(_initialTargetState, provStates);
        _shouldUpdateSucceed = AddMissingFiles(_initialTargetState, provStates, true);

        _numCasesGenerated++;
    }

    bool ValidateInput() {
        bool aliased = (
            CheckForCaseAliasing(_initialTargetState, _initialTargetState) ||
            CheckForCaseAliasing(_initialInplaceState, _initialInplaceState) ||
            CheckForCaseAliasing(_initialAllSourcesState, _initialAllSourcesState)
        );
        if (aliased) {
            //some of the directories is expected to contain case-aliased paths: skip such case
            return false;
        }
        if (CheckForCaseAliasing(_initialTargetState, _initialInplaceState)) {
            //some zip paths in updated directory and target directory are case-aliased
            //while it does not prohibit the update, it would be hard to validate it due to case differences
            return false;
        }
        if (_remoteEnabled) {
            bool aliased = (
                CheckForCaseAliasing(_initialAllSourcesState, _initialTargetState) ||
                CheckForCaseAliasing(_initialAllSourcesState, _initialInplaceState)
            );
            if (aliased) {
                //some zip paths in updated directory and remote/target directory are case-aliased
                //this will break update, because downloader will put new files into wrong-case directory
                //TODO: shouldn't this be fixed?
                return false;
            }
        }

        _numCasesValidated++;
        return true;
    }

    void WriteInput() {
        _initialTargetMani.Clear();
        _initialProvidedMani.Clear();
        WriteState(_rootTargetDir, _rootTargetDir, _initialTargetState, &_initialTargetMani);
        WriteState(_rootInplaceDir, _rootInplaceDir, _initialInplaceState, &_initialProvidedMani);
        for (int i = 0; i < _rootSources.size(); i++)
            WriteState(_rootSources[i].localDir, _rootSources[i].urlDir, _initialSourceState[i], &_initialProvidedMani);
    }

    bool DoUpdate() {
        _updater.reset(new UpdateProcess());

        _updater->Init(_initialTargetMani, _initialProvidedMani, _rootInplaceDir);
        for (const auto &zipPair : _initialInplaceState)
            _updater->AddManagedZip(_rootInplaceDir + "/" + zipPair.first);

        bool success = _updater->DevelopPlan(_updateType);
        _numCasesShouldSucceed += _shouldUpdateSucceed;
        _numCasesActualSucceed += success;

        if (!success) {
            ZipSyncAssert(!_shouldUpdateSucceed);
            return false;
        }
        //sometimes same file with different compression level is packed into same compressed data
        //hence our "should succeed" detection is not perfect, so we allow a bit of errors of one type
        double moreSuccessRatio = double(_numCasesActualSucceed - _numCasesShouldSucceed) / std::max(_numCasesValidated, 200);
        ZipSyncAssert(moreSuccessRatio <= 0.05);    //actually, this ratio is smaller than 1%
        
        if (_remoteEnabled)
            _updater->DownloadRemoteFiles();

        _updater->RepackZips();

        return true;
    }

    void CheckOutput() {
        _finalComputedProvidedMani = _updater->GetProvidedManifest();

        //analyze the actual state of inplace/update directory
        auto resultPaths = stdext::recursive_directory_enumerate(_rootInplaceDir);
        _finalActualTargetMani.Clear();
        _finalActualProvidedMani.Clear();
        Manifest actualProvidedMani;
        for (stdext::path filePath : resultPaths) {
            if (!stdext::is_regular_file(filePath))
                continue;
            if (!stdext::starts_with(filePath.filename().string(), "__reduced__") && !stdext::starts_with(filePath.filename().string(), "__download")) {
                _finalActualTargetMani.AppendLocalZip(filePath.string(), _rootInplaceDir, "default");
            }
            _finalActualProvidedMani.AppendLocalZip(filePath.string(), _rootInplaceDir, "default");
        }

        //check: the actual state exactly matches what we wanted to obtain (compressed hash may differ depending on options)
        AssertManifestsSame(
            _initialTargetMani.WriteToIni(), "target_expected.ini",
            _finalActualTargetMani.WriteToIni(), "target_obtained.ini",
            _updateType == UpdateType::SameContents,    //compressed data may not match unless requested explicitly
            _updateType == UpdateType::SameContents     //byterange may not match if recompression happened
        );

        //check: the provided manifest computed by updater exactly represents the current state
        Manifest inplaceComputedProvidedMani = _finalComputedProvidedMani.Filter([&](const FileMetainfo &f) {
            return f.zipPath.GetRootDir() == _rootInplaceDir;
        });
        AssertManifestsSame(
            inplaceComputedProvidedMani.WriteToIni(), "provided_computed.ini",
            _finalActualProvidedMani.WriteToIni(), "provided_actual.ini"
            //note: both compressed hashes and byteranges must be exactly equal!
        );

        //check: all files previously available are still available, if we take reduced zips into account
        for (int i = 0; i < _initialProvidedMani.size(); i++) {
            const FileMetainfo &oldFile = _initialProvidedMani[i];
            bool available = false;
            for (int j = 0; j < _finalComputedProvidedMani.size() && !available; j++) {
                const FileMetainfo &newFile = _finalComputedProvidedMani[j];
                if (oldFile.compressedHash == newFile.compressedHash)
                    available = true;
            }
            ZipSyncAssertF(available, "File %s with hash %s is no longer available", GetFullPath(oldFile.zipPath.abs, oldFile.filename).c_str(), oldFile.compressedHash.Hex().c_str());
        }
    }
};

//==========================================================================================

void Fuzz(std::string where, int casesNum, bool enableRemote) {
    static const int SPECIAL_SEEDS[] = {0};
    int SK = sizeof(SPECIAL_SEEDS) / sizeof(SPECIAL_SEEDS[0]);
    if (casesNum < 0)
        casesNum = 1000000000;  //infinite
    Fuzzer fuzz;
    fuzz.SetRemoteEnabled(enableRemote);
    for (int attempt = -SK; attempt < casesNum; attempt++) {
        int seed = (attempt < 0 ? SPECIAL_SEEDS[attempt + SK] : attempt);
        bool duplicate = false;
        for (int i = 0; i < SK; i++)
            if (SPECIAL_SEEDS[i] == seed)
                duplicate = true;
        if (attempt >= 0 && duplicate)
            continue;

        fuzz.GenerateInput(where + "/" + std::to_string(seed), seed);
        if (!fuzz.ValidateInput())
            continue;
        fuzz.WriteInput();
        if (fuzz.DoUpdate())
            fuzz.CheckOutput();
    }
}

}
