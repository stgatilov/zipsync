#include "ZipSync.h"
#include <time.h>
#include <assert.h>
#include "StdFilesystem.h"

using namespace ZipSync;

void CreateManifests() {
    //static const char *ZIP = R"(C:/TheDarkMod/darkmod_207/tdm_textures_stone_brick01.pk4)";
    //static const char *ROOT = R"(C:/TheDarkMod/darkmod_207)";
    static const char *ZIP = R"(F:/thedarkmod_releases/differential/tdm_update_2.06_to_2.07.zip)";
    static const char *ROOT = R"(F:/thedarkmod_releases/differential)";

    int t_before = clock();
    Manifest mani;
    AppendManifestsFromLocalZip(
        ZIP, ROOT,
        FileLocation::Local, "assets",
        mani
    );
    int t_after = clock();
    printf("Elapsed time: %d ms", t_after - t_before);

    auto data1 = mani.WriteToIni();
    WriteIniFile("test.iniz", data1);
    
    auto data3 = ReadIniFile("test.iniz");
    assert(data3 == data1);
    Manifest mani2;
    mani2.ReadFromIni(data3, ROOT);
}

void OneZipLocalUpdate() {
    static const char *ROOT         = R"(D:/StevePrograms/zipsync/build/__temp__/repack)";
    static const char *ZIP_REL206   = R"(F:/thedarkmod_releases/release206/tdm_base01.pk4)";
    static const char *ZIP_REL207   = R"(F:/thedarkmod_releases/release207/tdm_base01.pk4)";
    //static const char *ZIP_206TO207 = R"(F:/thedarkmod_releases/differential/tdm_update_2.06_to_2.07.zip)";
    static const char *ZIP_206TO207 = R"(http://tdmcdn.azureedge.net/test/tdm_update_2.06_to_2.07.zip)";

    Manifest provMani;
    provMani.AppendLocalZip(ZIP_REL206, stdext::path(ZIP_REL206).parent_path().string(), "");
    //provMani.AppendLocalZip(ZIP_206TO207, stdext::path(ZIP_206TO207).parent_path().string());
    Manifest remoteMani;
    remoteMani.ReadFromIni(ReadIniFile(R"(F:\thedarkmod_releases\differential\prov.ini)"), "http://tdmcdn.azureedge.net/test");
    provMani.AppendManifest(remoteMani);
    Manifest targMani;
    targMani.AppendLocalZip(ZIP_REL207, stdext::path(ZIP_REL207).parent_path().string(), "");

    stdext::create_directories(stdext::path(ROOT));
    UpdateProcess update;
    update.Init(targMani, provMani, ROOT);
    update.DevelopPlan(UpdateType::SameContents);
    update.DownloadRemoteFiles();
    update.RepackZips();
}

int main() {
    CreateManifests();
    //OneZipLocalUpdate();
    return 0;
}
