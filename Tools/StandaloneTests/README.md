# Standalone tests (no Unreal)

Plain C++ tests for the headers in `Source/ORBITAL_SpaceApps/Assembly/`. Kept outside `Source/` so Unreal does not compile them.

    cd Tools/StandaloneTests
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps -I../../Source/ORBITAL_SpaceApps/Assembly assembly_tests.cpp -o assembly_tests && ./assembly_tests exports.json
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps -I../../Source/ORBITAL_SpaceApps/Assembly screen_tests.cpp   -o screen_tests   && ./screen_tests
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps -I../../Source/ORBITAL_SpaceApps/Assembly timeline_tests.cpp -o timeline_tests && ./timeline_tests   (-v prints every event)
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps/Assembly gameflow_tests.cpp -o gameflow_tests && ./gameflow_tests
    python check_exports.py ../../Docs/CLAUDE_RESEARCH/PART_CATALOG.json exports.json
    (MSVC: cl /std:c++17 /EHsc /O2 /I..\..\Source\ORBITAL_SpaceApps /I..\..\Source\ORBITAL_SpaceApps\Assembly <file>.cpp)

Expected: 118, 90, 429 and 86 checks with 0 failures; 9 exported objects valid.

`OrbitalBuiltInCatalog.h` is a test-only compiled copy of the catalog. After a catalog change regenerate it:

    python gen_data.py ../../Docs/CLAUDE_RESEARCH/PART_CATALOG.json OrbitalBuiltInCatalog.h

## Inside Unreal
- Play in Editor prints the TC-A demo on screen and in the Output Log (`LogOrbitalAssembly`). Turn it off with `orbital.AssemblyDemo 0`.
- Session Frontend > Automation > `ORBITAL.Assembly.DemoTCA` runs the same script without Play.

## Catalog in the game
The game loads `Docs/CLAUDE_RESEARCH/PART_CATALOG.json` through `LoadOrbitalCatalog` (`UOrbitalAssemblyScreen::InitFromProjectCatalog`). A packaged build has to stage that file and pass its real path to `FOrbitalAssemblyData::LoadFile`.
