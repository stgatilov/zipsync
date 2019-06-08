#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <random>
#include <chrono>

#include "StdFilesystem.h"
#include "TdmSync.h"
using namespace TdmSync;

#include "zip.h"
#include "blake2.h"


TEST_CASE("PathAR::IsHttp") {
    CHECK(PathAR::IsHttp("http://darkmod.taaaki.za.net/release") == true);
    CHECK(PathAR::IsHttp("http://tdmcdn.azureedge.net/") == true);
    CHECK(PathAR::IsHttp("C:\\TheDarkMod\\darkmod_207") == false);
    CHECK(PathAR::IsHttp("darkmod_207") == false);
    CHECK(PathAR::IsHttp("/usr/bin/darkmod_207") == false);
}

TEST_CASE("PathAR::From[Abs|Rel]") {
    const char *cases[][3] = {
        {"tdm_shared_stuff.zip", "C:/TheDarkMod/darkmod_207", "C:/TheDarkMod/darkmod_207/tdm_shared_stuff.zip"},
        {"tdm_shared_stuff.zip", "C:/TheDarkMod/darkmod_207/", "C:/TheDarkMod/darkmod_207/tdm_shared_stuff.zip"},
        {"a/b/c/x.pk4", "C:/TheDarkMod/darkmod_207/", "C:/TheDarkMod/darkmod_207/a/b/c/x.pk4"},
        {"tdm_shared_stuff.zip", "http://tdmcdn.azureedge.net/", "http://tdmcdn.azureedge.net/tdm_shared_stuff.zip"},
        {"a/b/c/x.pk4", "http://tdmcdn.azureedge.net/", "http://tdmcdn.azureedge.net/a/b/c/x.pk4"}
    };
    for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PathAR a = PathAR::FromRel(cases[i][0], cases[i][1]);
        PathAR b = PathAR::FromAbs(cases[i][2], cases[i][1]);
        CHECK(a.rel == cases[i][0]);
        CHECK(a.abs == cases[i][2]);
        CHECK(b.rel == cases[i][0]);
        CHECK(b.abs == cases[i][2]);
    }
}

HashDigest GenHash(int idx) {
    std::minstd_rand rnd(idx + 0xDEADBEEF);
    HashDigest res;
    for (int i = 0; i < sizeof(res.data); i++)
        res.data[i] = rnd();
    return res;
}
template<class T> const T &Search(const std::vector<std::pair<std::string, T>> &data, const std::string &key) {
    int pos = -1;
    int cnt = 0;
    for (int i = 0; i < data.size(); i++) {
        if (data[i].first == key) {
            pos = i;
            cnt++;
        }
    }
    REQUIRE(cnt == 1);
    return data[pos].second;
}

TEST_CASE("ProvidingManifest: Read/Write") {
    ProvidingManifest mani;

    ProvidedFile pf;
    pf.filename = "textures/model/darkmod/grass/grass01.jpg";
    pf.zipPath.rel = "subdir/win32/interesting_name456.pk4";
    pf.compressedHash = GenHash(1);
    pf.contentsHash = GenHash(2);
    pf.byterange[0] = 0;
    pf.byterange[1] = 123456;
    mani.AppendFile(pf);
    pf.filename = "models/darkmod/guards/head.lwo";
    pf.zipPath.rel = "basic_assets.pk4";
    pf.compressedHash = GenHash(5);
    pf.contentsHash = GenHash(6);
    pf.byterange[0] = 1000000000;
    pf.byterange[1] = 1000010000;
    mani.AppendFile(pf);
    pf.filename = "textures/model/standalone/menu.png";
    pf.zipPath.rel = "subdir/win32/interesting_name456.pk4";
    pf.compressedHash = GenHash(3);
    pf.contentsHash = GenHash(4);
    pf.byterange[0] = 123456;
    pf.byterange[1] = 987654;
    mani.AppendFile(pf);

    IniData savedIni = mani.WriteToIni();

    ProvidingManifest restored;
    restored.ReadFromIni(savedIni, "nowhere");

    std::vector<int> order = {1, 0, 2};
    for (int i = 0; i < order.size(); i++) {
        const ProvidedFile &src = mani[order[i]];
        const ProvidedFile &dst = restored[i];
        CHECK(src.zipPath.rel == dst.zipPath.rel);
        CHECK(src.filename == dst.filename);
        CHECK(src.compressedHash == dst.compressedHash);
        CHECK(src.contentsHash == dst.contentsHash);
        CHECK(src.byterange[0] == dst.byterange[0]);
        CHECK(src.byterange[1] == dst.byterange[1]);
    }

    for (int t = 0; t < 5; t++) {
        ProvidingManifest newMani;
        newMani.ReadFromIni(savedIni, "nowhere");
        IniData newIni = newMani.WriteToIni();
        CHECK(savedIni == newIni);
    }
}

TEST_CASE("TargetManifest: Read/Write") {
    TargetManifest mani;

    TargetFile tf;
    tf.packageName = "interesting";
    tf.zipPath.rel = "subdir/win32/interesting_name456.pk4";
    tf.compressedHash = GenHash(1);
    tf.contentsHash = GenHash(2);
    tf.fhFilename = "textures/model/darkmod/grass/grass01.jpg";
    tf.fhLastModTime = 1150921251;
    tf.fhCompressionMethod = 8;
    tf.fhGeneralPurposeBitFlag = 2;
    tf.fhCompressedSize = 171234;
    tf.fhContentsSize = 214567;
    mani.AppendFile(tf);
    tf.packageName = "assets";
    tf.zipPath.rel = "basic_assets.pk4";
    tf.compressedHash = GenHash(5);
    tf.contentsHash = GenHash(6);
    tf.fhFilename = "models/darkmod/guards/head.lwo";
    tf.fhLastModTime = 100000000;
    tf.fhCompressionMethod = 0;
    tf.fhGeneralPurposeBitFlag = 0;
    tf.fhCompressedSize = 4567891;
    tf.fhContentsSize = 4567891;
    mani.AppendFile(tf);
    tf.packageName = "assets";
    tf.zipPath.rel = "subdir/win32/interesting_name456.pk4";
    tf.compressedHash = GenHash(3);
    tf.contentsHash = GenHash(4);
    tf.fhFilename = "textures/model/standalone/menu.png";
    tf.fhLastModTime = 4000000000U;
    tf.fhCompressionMethod = 8;
    tf.fhGeneralPurposeBitFlag = 6;
    tf.fhCompressedSize = 12012;
    tf.fhContentsSize = 12001;
    mani.AppendFile(tf);

    IniData savedIni = mani.WriteToIni();

    TargetManifest restored;
    restored.ReadFromIni(savedIni, "nowhere");

    std::vector<int> order = {1, 2, 0};
    for (int i = 0; i < order.size(); i++) {
        const TargetFile &src = mani[order[i]];
        const TargetFile &dst = restored[i];
        CHECK(src.zipPath.rel == dst.zipPath.rel);
        CHECK(src.packageName == dst.packageName);
        CHECK(src.compressedHash == dst.compressedHash);
        CHECK(src.contentsHash == dst.contentsHash);
        CHECK(src.fhFilename == dst.fhFilename);
        CHECK(src.fhLastModTime == dst.fhLastModTime);
        CHECK(src.fhCompressionMethod == dst.fhCompressionMethod);
        CHECK(src.fhGeneralPurposeBitFlag == dst.fhGeneralPurposeBitFlag);
        CHECK(src.fhCompressedSize == dst.fhCompressedSize);
        CHECK(src.fhContentsSize == dst.fhContentsSize);
    }

    for (int t = 0; t < 5; t++) {
        TargetManifest newMani;
        newMani.ReadFromIni(savedIni, "nowhere");
        IniData newIni = newMani.WriteToIni();
        CHECK(savedIni == newIni);
    }
}

#include <stdio.h>  /* defines FILENAME_MAX */
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif
//TODO: move it somewhere
stdext::path GetCwd() {
    char buffer[4096];
    char *ptr = getcwd(buffer, sizeof(buffer));
    return ptr;
}
stdext::path GetTempDir() {
    int timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000 % 1000000000;
    static stdext::path where = GetCwd() / "__temp__" / std::to_string(timestamp);
    return where;
}

TEST_CASE("AppendManifestsFromLocalZip") {
    std::string rootDir = GetTempDir().string();
    std::string zipPath1 = (GetTempDir() / stdext::path("a/f1.zip")).string();
    std::string zipPath2 = (GetTempDir() / stdext::path("amt.pk4")).string();

    std::string fnPkgJson = "data/pkg.json";
    std::string fnRndDat = "rnd.dat";
    std::string fnSeqBin = "data/Seq.bin";
    std::string fnDoubleDump = "aRMy/Of/GoOd/WiLl/DoUbLe.dump";

    std::string cntPkgJson = R"(
# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/tdmsync2")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
    )";
    std::vector<int> cntSeqBin;
    for (int i = 0; i < 10000; i++)
        cntSeqBin.push_back(i);
    std::vector<int> cntRndDat;
    std::mt19937 rnd;
    for (int i = 0; i < 1234; i++)
        cntRndDat.push_back(rnd());
    std::vector<double> cntDoubleDump;
    for (int i = 0; i < 1000; i++)
        cntDoubleDump.push_back(double(i) / double(1000));

    #define PACK_BUF(buf) zipWriteInFileInZip(zf, buf.data(), buf.size() * sizeof(buf[0]));
    stdext::create_directories(stdext::path(zipPath1).parent_path());
    zipFile zf = zipOpen(zipPath1.c_str(), 0);
    zipOpenNewFileInZip(zf, fnPkgJson.c_str(), NULL, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    PACK_BUF(cntPkgJson);
    zipCloseFileInZip(zf);
    zipOpenNewFileInZip(zf, fnRndDat.c_str(), NULL, NULL, 0, NULL, 0, NULL, 0, 0);
    PACK_BUF(cntRndDat);
    zipCloseFileInZip(zf);
    zip_fileinfo info; info.dosDate = 123456789;
    zipOpenNewFileInZip(zf, fnSeqBin.c_str(), &info, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_COMPRESSION);
    PACK_BUF(cntSeqBin);
    zipCloseFileInZip(zf);
    zipClose(zf, NULL);
    stdext::create_directories(stdext::path(zipPath2).parent_path());
    zf = zipOpen(zipPath2.c_str(), 0);
    zipOpenNewFileInZip(zf, fnDoubleDump.c_str(), NULL, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_SPEED);
    PACK_BUF(cntDoubleDump);
    zipCloseFileInZip(zf);
    zipClose(zf, NULL);
    #undef PACK_BUF

    ProvidingManifest providing;
    TargetManifest target;
    AppendManifestsFromLocalZip(
        zipPath1, rootDir,
        ProvidingLocation::Local, "default",
        providing, target
    );
    AppendManifestsFromLocalZip(
        zipPath2, rootDir,
        ProvidingLocation::RemoteHttp, "chaos",
        providing, target
    );

    REQUIRE(providing.size() == 4);
    REQUIRE(target.size() == 4);
    REQUIRE(providing[0].filename == fnPkgJson   );
    REQUIRE(providing[1].filename == fnRndDat    );
    REQUIRE(providing[2].filename == fnSeqBin    );
    REQUIRE(providing[3].filename == fnDoubleDump);
    for (int i = 0; i < target.size(); i++)
        REQUIRE(target[i].fhFilename == providing[i].filename);

    CHECK(providing[0].zipPath.abs == zipPath1);  CHECK(target[0].zipPath.abs == zipPath1);
    CHECK(providing[1].zipPath.abs == zipPath1);  CHECK(target[1].zipPath.abs == zipPath1);
    CHECK(providing[2].zipPath.abs == zipPath1);  CHECK(target[2].zipPath.abs == zipPath1);
    CHECK(providing[3].zipPath.abs == zipPath2);  CHECK(target[3].zipPath.abs == zipPath2);
    CHECK(providing[0].location == ProvidingLocation::Local);       CHECK(target[0].packageName == "default");
    CHECK(providing[1].location == ProvidingLocation::Local);       CHECK(target[1].packageName == "default");
    CHECK(providing[2].location == ProvidingLocation::Local);       CHECK(target[2].packageName == "default");
    CHECK(providing[3].location == ProvidingLocation::RemoteHttp);  CHECK(target[3].packageName == "chaos"  );
    CHECK(providing[0].contentsHash.Hex() == "8ec061d20526f1e5ce56519f09bc1ee2ad065464e3e7cbbb94324865bca95a45"); //computed externally in Python
    CHECK(providing[1].contentsHash.Hex() == "75b25a4dd22ac100925e09d62016c0ffdb5998b470bc685773620d4f37458b69");
    CHECK(providing[2].contentsHash.Hex() == "54b97c474a60b36c16a5c6beea5b2a03a400096481196bbfe2202ef7a547408c");
    CHECK(providing[3].contentsHash.Hex() == "009c0860b467803040c61deb6544a3f515ac64c63d234e286d3e2fa352411e91");
    for (int i = 0; i < target.size(); i++)
        CHECK(target[i].contentsHash == providing[i].contentsHash);
    CHECK(target[0].fhLastModTime == 0);
    CHECK(target[1].fhLastModTime == 0);
    CHECK(target[2].fhLastModTime == 123456789);
    CHECK(target[3].fhLastModTime == 0);
    CHECK(target[0].fhCompressionMethod == Z_DEFLATED);
    CHECK(target[1].fhCompressionMethod == 0);
    CHECK(target[2].fhCompressionMethod == Z_DEFLATED);
    CHECK(target[3].fhCompressionMethod == Z_DEFLATED);
#define SIZE_BUF(buf) buf.size() * sizeof(buf[0])
    CHECK(target[0].fhContentsSize == SIZE_BUF(cntPkgJson));
    CHECK(target[1].fhContentsSize == SIZE_BUF(cntRndDat));
    CHECK(target[2].fhContentsSize == SIZE_BUF(cntSeqBin));
    CHECK(target[3].fhContentsSize == SIZE_BUF(cntDoubleDump));
#undef SIZE_BUF
    CHECK(target[0].fhGeneralPurposeBitFlag == 0);     //"normal"
    CHECK(target[1].fhGeneralPurposeBitFlag == 0);     //no compression (stored)
    CHECK(target[2].fhGeneralPurposeBitFlag == 2);     //"maximum"
    CHECK(target[3].fhGeneralPurposeBitFlag == 6);     //"super fast"

    double RATIOS[4][2] = {
        {0.5, 0.75},        //text is rather compressible
        {1.0, 1.0},         //store: size must be exactly same
        {0.3, 0.4},         //integer sequence is well-compressible
        {0.3, 0.4},         //doubles are well-compressible
    };
    for (int i = 0; i < 4; i++) {
        double ratio = double(target[i].fhCompressedSize) / target[i].fhContentsSize;
        CHECK(ratio >= RATIOS[i][0]);  CHECK(ratio <= RATIOS[i][1]);
    }

    for (int i = 0; i < 4; i++) {
        const uint32_t *br = providing[i].byterange;
        std::vector<char> fdata(br[1] - br[0]);
        int totalSize = target[i].fhCompressedSize + target[i].fhFilename.size() + 30;
        CHECK(fdata.size() == totalSize);

        FILE *f = fopen(providing[i].zipPath.abs.c_str(), "rb");
        REQUIRE(f);
        fseek(f, br[0], SEEK_SET);
        CHECK(fread(fdata.data(), 1, fdata.size(), f) == fdata.size());
        fclose(f);

        CHECK(*(int*)&fdata[0] == 0x04034b50);   //local file header signature
        CHECK(memcmp(&fdata[30], target[i].fhFilename.c_str(), target[i].fhFilename.size()) == 0);

        HashDigest digest;
        int offs = fdata.size() - target[i].fhCompressedSize, sz = target[i].fhCompressedSize;
        blake2s(digest.data, sizeof(digest), fdata.data() + offs, sz, NULL, 0);
        CHECK(target[i].compressedHash == digest);
        CHECK(target[i].compressedHash == providing[i].compressedHash);
    }
}

TEST_CASE("UpdateProcess::DevelopPlan") {
    ProvidingManifest providing;
    TargetManifest target;

    struct MatchAnswer {
        int bestLocation = int(ProvidingLocation::Nowhere);
        std::vector<std::string> filenames;
    };
    //for each target filename: list of providing filenames which match
    std::map<std::string, MatchAnswer> correctMatching[2];

    //generate one file per every possible combination of availability:
    //  target file is: [missing/samecontents/samecompressed] on [inplace/local/remote]
    int mode[3] = {0};
    for (mode[0] = 0; mode[0] < 3; mode[0]++) {
        for (mode[1] = 0; mode[1] < 5; mode[1]++) {
            for (mode[2] = 0; mode[2] < 5; mode[2]++) {
                int targetIdx = target.size();
                TargetFile tf;
                tf.contentsHash = GenHash(targetIdx);
                tf.compressedHash = GenHash(targetIdx + 1000);
                tf.zipPath = PathAR::FromRel("target" + std::to_string(targetIdx % 4) + ".zip", "nowhere");
                tf.fhFilename = "file" + std::to_string(targetIdx) + ".dat";
                //note: other members not used

                MatchAnswer *answer[2] = {&correctMatching[0][tf.fhFilename], &correctMatching[1][tf.fhFilename]};
                for (int pl = 0; pl < 3; pl++) {
                    int modes[2] = {mode[pl], -1};
                    if (modes[0] >= 3) {
                        modes[1] = mode[0] - 2;
                        modes[0] = 1;
                    }

                    for (int j = 0; j < 2; j++) if (modes[j] >= 0) {
                        int providedIdx = providing.size();
                        int m = modes[j];

                        ProvidedFile pf;
                        pf.byterange[0] = (providedIdx+0) * 100000;
                        pf.byterange[1] = (providedIdx+1) * 100000;
                        pf.contentsHash = (m >= 1 ? tf.contentsHash : GenHash(providedIdx + 2000));
                        pf.compressedHash = (m == 2 ? tf.compressedHash : GenHash(providedIdx + 3000));
                        if (pl == 0) {
                            pf.location = ProvidingLocation::Local;     //Inplace must be set of DevelopPlan
                            pf.zipPath = tf.zipPath;
                            pf.filename = tf.fhFilename;
                        }
                        else if (pl == 1) {
                            pf.location = ProvidingLocation::Local;
                            pf.zipPath = PathAR::FromRel("other" + std::to_string(providedIdx % 4) + ".zip", "nowhere");
                            pf.filename = "some_file" + std::to_string(providedIdx);
                        }
                        else if (pl == 2) {
                            pf.location = ProvidingLocation::RemoteHttp;
                            pf.zipPath = PathAR::FromRel("other" + std::to_string(providedIdx % 4) + ".zip", "http://localhost:7123");
                            pf.filename = "some_file" + std::to_string(providedIdx);
                        }
                        providing.AppendFile(pf);

                        for (int t = 0; t < 2; t++) {
                            bool matches = (t == 0 ? pf.contentsHash == tf.contentsHash : pf.compressedHash == tf.compressedHash);
                            if (!matches)
                                continue;
                            if (pl < int(answer[t]->bestLocation)) {
                                answer[t]->filenames.clear();
                                answer[t]->bestLocation = pl;
                            }
                            if (pl == int(answer[t]->bestLocation)) {
                                answer[t]->filenames.push_back(pf.filename);
                            }
                        }
                    }
                }

                target.AppendFile(tf);
            }
        }
    }

    std::mt19937 rnd;
    for (int attempt = 0; attempt < 10; attempt++) {

        //try to develop update plan in both modes
        for (int t = 0; t < 2; t++) {

            TargetManifest targetCopy = target;
            ProvidingManifest providingCopy = providing;
            if (attempt > 0) {
                //note: here we assume that files are stored in a vector =(
                std::shuffle(&targetCopy[0], &targetCopy[0] + targetCopy.size(), rnd);
                std::shuffle(&providingCopy[0], &providingCopy[0] + providingCopy.size(), rnd);
            }

            UpdateProcess update;
            update.Init(std::move(targetCopy), std::move(providingCopy), "nowhere");
            update.DevelopPlan(t == 0 ? UpdateType::SameContents : UpdateType::SameCompressed);

            for (int i = 0; i < update.MatchCount(); i++) {
                UpdateProcess::Match match = update.GetMatch(i);
                const MatchAnswer &answer = correctMatching[t][match.target->fhFilename];
                if (answer.filenames.empty())
                    CHECK(match.provided == nullptr);
                else {
                    CHECK(match.provided != nullptr);
                    std::string fn = match.provided->filename;
                    CHECK(std::find(answer.filenames.begin(), answer.filenames.end(), fn) != answer.filenames.end());
                    if (attempt == 0) {
                        //non-shuffled manifests: check that first optimal match is chosen (to be deterministic)
                        CHECK(fn == answer.filenames.front());
                    }
                }
            }
        }
    }
}
