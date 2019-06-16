#include "Fuzzer.h"
#include <string>
#include <vector>
#include <random>
#include <set>
#include <map>
#include "StdString.h"
#include "tsassert.h"
#include "unzip.h"
#include "zip.h"


namespace TdmSync {

typedef std::uniform_int_distribution<int> IntD;
typedef std::uniform_real_distribution<double> DblD;

class FuzzerImpl {
    std::mt19937 _rnd;

public:
    template<class C> const typename C::value_type & RandomFrom(const C &cont) {
        int idx = IntD(0, cont.size() - 1)(_rnd);
        auto iter = cont.cbegin();
        std::advance(iter, idx);
        return *iter;
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
            if (t == 3) ch = ' ';
            res += ch;
        }
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
    };
    InZipParams GenInZipParams() {
        InZipParams res;
        res.method = (IntD(0, 1)(_rnd) ? 8 : 0);
        res.level = res.method ? IntD(Z_BEST_SPEED, Z_BEST_COMPRESSION)(_rnd) : 0;
        res.dosDate = IntD(INT_MIN, INT_MAX)(_rnd);
        res.internalAttribs = IntD(INT_MIN, INT_MAX)(_rnd);
        res.externalAttribs = IntD(INT_MIN, INT_MAX)(_rnd);
        return res;
    }

    struct InZipFile {
        InZipParams params;
        std::vector<uint8_t> contents;
    };
    typedef std::vector<std::pair<std::string, InZipFile>> InZipState;
    typedef std::map<std::string, InZipState> DirState;

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
        for (const auto &pPZ : source) {
            const auto &files = pPZ.second;
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
};

void Fuzz() {
    FuzzerImpl impl;
    while (1) {
        //auto res = impl.GenPaths(10);
        //auto res = impl.GenFileContents();
        auto target = impl.GenTargetState(50, 10);
        auto providing = impl.GenMutatedState(target);
    }
}

}
