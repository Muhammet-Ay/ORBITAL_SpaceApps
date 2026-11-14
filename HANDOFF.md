# ORBITAL engineering handoff

Branch `ram/engineering`. Engineering commit `8dea8f4`.

## What is done

- Added the v2.1 engineering header with `Part::thrustN` in newtons, Unreal JSON loader, dashboard data model, parity fixtures, UI copy, and editor automation test under `Source/ORBITAL_SpaceApps/`.
- Added the module's private `Json` dependency. The loader reads the existing `Docs/CLAUDE_RESEARCH/PART_CATALOG.json`, including `thrust_n`, `socket_tree`, and `default_orbit`.
- The standalone MSVC engineering harness passed 17/17 cases before Unreal integration. The shared-project editor build and Unreal JSON loader test are pending.

## Last build command

PowerShell on Ram's PC

```powershell
$cache = 'C:\Users\ramis\Documents\Codex\2026-11-14\files-mentioned-by-the-user-1\work\OrbitalBuild'
$env:TEMP = $cache
$env:TMP = $cache
$env:DOTNET_CLI_HOME = $cache
& 'D:\UE5-8-4\UE_5.8\Engine\Build\BatchFiles\Build.bat' ORBITAL_SpaceAppsEditor Win64 Development '-Project=D:\ORBITAL_SpaceApps\ORBITAL_SpaceApps.uproject' -NoUBA -NoHotReloadFromIDE -NoPCH -MaxParallelActions=1 "-UBARootDir=$cache\UbaCache" "-Log=$cache\UBT.log" > "$cache\build-output.txt" 2>&1
```

## Last compiler error and attempts

The last compiler error came from the separate AdapterSmoke project, before the JSON key conversion fix.

```text
work/AdapterSmoke/Source/AdapterSmoke/OrbitalCatalogLoader.cpp(390,37): error C2665: 'TMapBase<KeyType,ValueType,SetAllocator,KeyFuncs>::Add': no overloaded function could convert all the argument types
```

UE 5.8 JSON object keys do not implicitly convert to `FString` in this code. The shared project's loader now explicitly constructs an `FString` at the affected lookups and preset insertions. The corrected file has yet to finish an Unreal build.

The sandbox denied access to `C:\ProgramData\Epic\UnrealBuildAccelerator` and the global Live Coding mutex. A workspace-local `-UBARootDir` and `-NoHotReloadFromIDE` moved the build past those errors. The first shared-project attempt queued its actions and wrote a roughly 2.4 GB shared PCH, then stalled. The retry used `-NoPCH` and one compile action at a time. It was stopped during setup at the user's request and emitted no new compiler error. UBT detected Windows SDK 10.0.26100.0 and warned that MSVC 14.52.37029 is newer than preferred 14.50.35717.

Local logs are `work/AdapterSmoke/UBT.log` and `work/OrbitalBuild/build-output.txt` in the Codex workspace.

## What remains

1. Build `ORBITAL_SpaceAppsEditor Win64 Development` in the shared project. Capture the first compiler error with file and line if it fails.
2. Run `ORBITAL.Engineering.CatalogParity` in Unreal Editor Automation. It loads the actual project JSON catalog, checks probe thrust, and verifies **TC-A = 33.5111** and **TC-B = 80.7778**.
3. Report the build and test results. Ram handles the push.
