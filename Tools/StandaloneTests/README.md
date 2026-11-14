# Standalone tests (no Unreal)

Plain C++ tests for the headers in `Source/ORBITAL_SpaceApps/Assembly/`. Kept outside `Source/` so Unreal does not compile them.

    cd Tools/StandaloneTests
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps/Assembly assembly_tests.cpp -o assembly_tests && ./assembly_tests exports.json
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps/Assembly screen_tests.cpp   -o screen_tests   && ./screen_tests
    g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps/Assembly timeline_tests.cpp -o timeline_tests && ./timeline_tests   (-v prints every event)
    python check_exports.py ../../Docs/CLAUDE_RESEARCH/PART_CATALOG.json exports.json
    (MSVC: cl /std:c++17 /EHsc /O2 /I..\..\Source\ORBITAL_SpaceApps\Assembly <file>.cpp)

Expected: 118, 90 and 418 checks with 0 failures; 9 exported objects valid.

After a catalog change regenerate the compiled-in catalog:

    python gen_data.py ../../Docs/CLAUDE_RESEARCH/PART_CATALOG.json ../../Source/ORBITAL_SpaceApps/Assembly/OrbitalBuiltInCatalog.h

## Inside Unreal
- Play in Editor prints the TC-A demo on screen and in the Output Log (`LogOrbitalAssembly`). Turn it off with `orbital.AssemblyDemo 0`.
- Session Frontend > Automation > `ORBITAL.Assembly.DemoTCA` runs the same script without Play.

## Temporary until ram/engineering is merged
- `Assembly/OrbitalEngineering_v2.1.h`: copy of the Docs header plus `Part::thrustN`. Delete it and use Ram's.
- `Assembly/OrbitalBuiltInCatalog.h` and `FOrbitalAssemblyData::MakeBuiltIn()`: replace with the JSON loader's `FOrbitalCatalog`.
