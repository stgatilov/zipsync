#include "Fuzzer.h"
#include <string>
#include <vector>
#include <random>
#include <set>
#include <map>
#include "StdString.h"
#include "StdFilesystem.h"
#include "tsassert.h"
#include "TdmSync.h"
#include "zip.h"


namespace TdmSync {

typedef std::uniform_int_distribution<int> IntD;
typedef std::uniform_real_distribution<double> DblD;

struct FuzzerGenerator {
    std::mt19937 _rnd;
    UpdateType _updateType = UpdateType::SameCompressed;

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
        TdmSyncAssert(sum >= 0);
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
        int len = IntD(3, 10)(_rnd);
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
        res.method = (IntD(0, 1)(_rnd) ? 8 : 0);
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
        if (_updateType == UpdateType::SameCompressed && !(a.params == b.params))
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
        if (leaveMisses)
            targetFiles.resize(IntD(k/2, k-1)(_rnd));

        auto filePaths = GenPaths(targetFiles.size());
        for (int i = 0; i < targetFiles.size(); i++) {
            DirState *dst = RandomFrom(provided);
            std::string zipPath = GenPaths(1, ".zip")[0];
            InZipState &inzip = (*dst)[zipPath];
            std::string path = filePaths[i];
            int pos = IntD(0, inzip.size())(_rnd);
            inzip.insert(inzip.begin() + pos, std::make_pair(path, *targetFiles[i]));
        }
        return !leaveMisses;
    }

    void WriteState(const std::string &rootPath, const DirState &state, TargetManifest *targetMani, ProvidedManifest *providedMani) {
        for (const auto &zipPair : state) {
            PathAR zipPath = PathAR::FromRel(zipPair.first, rootPath);
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

            if (targetMani)
                targetMani->AppendLocalZip(zipPath.abs, rootPath, "default");
            if (providedMani)
                providedMani->AppendLocalZip(zipPath.abs, rootPath);
        }
    }
};

void Fuzz(std::string where) {
    for (int attempt = 0; attempt < 1000000000; attempt++) {
        FuzzerGenerator impl;
        impl._rnd.seed(attempt);
        auto updateType = (attempt % 2 ? UpdateType::SameCompressed : UpdateType::SameContents);
        impl._updateType = updateType;
        auto targetState = impl.GenTargetState(50, 10);
        auto provInplaceState = impl.GenMutatedState(targetState);
        auto provLocalState = impl.GenMutatedState(targetState);
        bool willSucceed = impl.AddMissingFiles(targetState, {&provInplaceState, &provLocalState});

        TargetManifest targetMani;
        ProvidedManifest providedMani;
        std::string basePath = where + "/" + std::to_string(attempt);
        impl.WriteState(basePath + "/target", targetState, &targetMani, nullptr);
        impl.WriteState(basePath + "/inplace", provInplaceState, nullptr, &providedMani);
        impl.WriteState(basePath + "/local", provLocalState, nullptr, &providedMani);

        UpdateProcess update;
        update.Init(TargetManifest(targetMani), ProvidedManifest(providedMani), basePath + "/inplace");
        for (const auto &zipPair : provInplaceState)
            update.AddManagedZip(basePath + "/inplace/" + zipPair.first);

        bool success = update.DevelopPlan(updateType);
        TdmSyncAssert(success == willSucceed);
        if (success) {
            update.RepackZips();
        }

        auto resultPaths = stdext::recursive_directory_enumerate(basePath + "/inplace");
        TargetManifest resultMani;
        for (stdext::path filePath : resultPaths) {
            if (!stdext::is_regular_file(filePath))
                continue;
            if (stdext::starts_with(filePath.filename().string(), "__reduced__"))
                continue;   //TODO;
            resultMani.AppendLocalZip(filePath.string(), basePath + "/inplace", "default");
        }

        IniData iniMust = targetMani.WriteToIni();
        IniData iniHas = resultMani.WriteToIni();
        if (updateType == UpdateType::SameContents) {
            auto ClearCompressedHash = [](IniData &ini) {
                for (auto& pSect : ini)
                    for (auto &pProp : pSect.second)
                        if (pProp.first == "compressedHash" || pProp.first == "compressedSize")
                            pProp.second = "(removed)";
            };
            ClearCompressedHash(iniMust);
            ClearCompressedHash(iniHas);
        }
        if (iniMust != iniHas) {
            WriteIniFile((basePath + "/expected.ini").c_str(), iniMust);
            WriteIniFile((basePath + "/obtained.ini").c_str(), iniHas);
        }
        TdmSyncAssert(iniMust == iniHas);
    }
}

}
