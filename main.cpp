#include "TdmSync.h"
#include <time.h>
#include <assert.h>

using namespace TdmSync;

static const char *ROOT = R"(C:/TheDarkMod/darkmod_207)";

int main() {
    int t_before = clock();
    ProvidingManifest provMani;
    TargetManifest targMani;
    AppendManifestsFromLocalZip(
        R"(C:/TheDarkMod/darkmod_207/tdm_textures_stone_brick01.pk4)", ROOT,
        ProvidingLocation::Local, "assets",
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
    ProvidingManifest provMani2;
    provMani2.ReadFromIni(data3, ROOT);
    auto data4 = ReadIniFile("targ.ini");
    assert(data4 == data2);
    TargetManifest targMani2;
    targMani2.ReadFromIni(data4, ROOT);
    
    return 0;
}
