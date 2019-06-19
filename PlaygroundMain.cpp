#include "TdmSync.h"
#include <time.h>
#include <assert.h>
#include "StdFilesystem.h"

using namespace TdmSync;

void CreateManifests() {
    static const char *ZIP = R"(C:/TheDarkMod/darkmod_207/tdm_textures_stone_brick01.pk4)";
    static const char *ROOT = R"(C:/TheDarkMod/darkmod_207)";

    int t_before = clock();
    ProvidedManifest provMani;
    TargetManifest targMani;
    AppendManifestsFromLocalZip(
        ZIP, ROOT,
        ProvidedLocation::Local, "assets",
        provMani, targMani
    );
    int t_after = clock();
    printf("Elapsed time: %d ms", t_after - t_before);

    auto data1 = provMani.WriteToIni();
    WriteIniFile("prov.ini", data1);
    auto data2 = targMani.WriteToIni();
    WriteIniFile("targ.ini", data2);
    
    auto data3 = ReadIniFile("prov.ini");
    assert(data3 == data1);
    ProvidedManifest provMani2;
    provMani2.ReadFromIni(data3, ROOT);
    auto data4 = ReadIniFile("targ.ini");
    assert(data4 == data2);
    TargetManifest targMani2;
    targMani2.ReadFromIni(data4, ROOT);
}

void OneZipLocalUpdate() {
    static const char *ROOT         = R"(D:/StevePrograms/tdmsync2/build/__temp__/repack)";
    static const char *ZIP_REL206   = R"(F:/thedarkmod_releases/release206/tdm_base01.pk4)";
    static const char *ZIP_REL207   = R"(F:/thedarkmod_releases/release207/tdm_base01.pk4)";
    static const char *ZIP_206TO207 = R"(F:/thedarkmod_releases/differential/tdm_update_2.06_to_2.07.zip)";

    ProvidedManifest provMani;
    provMani.AppendLocalZip(ZIP_REL206, stdext::path(ZIP_REL206).parent_path().string());
    provMani.AppendLocalZip(ZIP_206TO207, stdext::path(ZIP_206TO207).parent_path().string());
    TargetManifest targMani;
    targMani.AppendLocalZip(ZIP_REL207, stdext::path(ZIP_REL207).parent_path().string(), "");

    stdext::create_directories(stdext::path(ROOT));
    UpdateProcess update;
    update.Init(TargetManifest(targMani), ProvidedManifest(provMani), ROOT);
    update.DevelopPlan(UpdateType::SameContents);
    update.RepackZips();
}

int main() {
    //CreateManifests();
    OneZipLocalUpdate();
    return 0;
}