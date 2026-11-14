# Build and test

## Build the editor target

Use Unreal Engine 5.8.4, x64 MSVC and Windows SDK 10.0.26100. Close Unreal Editor before building. In PowerShell, set `$engine` to your engine installation and run this from the repo root.

```powershell
$engine = 'D:\UE5-8-4\UE_5.8'
$project = (Resolve-Path '.\ORBITAL_SpaceApps.uproject').Path
& "$engine\Engine\Build\BatchFiles\Build.bat" ORBITAL_SpaceAppsEditor Win64 Development "-Project=$project" -WaitMutex -MaxParallelActions=2
```

Visual Studio's equivalent target configuration is **Development Editor / Win64**. Record the commit and the final result or first error line. If your checkout includes Git LFS assets and they are still pointers, run `git lfs pull` before opening the level.

## Unreal checks

Open the editor's **Session Frontend → Automation** panel and run:

| Test | What to verify |
| --- | --- |
| `ORBITAL.Engineering.CatalogParity` | Load the v2.1 JSON catalog and verify TC-A **33.5111** and TC-B **80.7778**. |
| `ORBITAL.Assembly.DemoTCA` | Assemble TC-A, check its score and timeline, reject a wrong socket placement, and confirm bus removal leaves the launcher intact. |

For the manual check, open `L_Mission`, press **Play**, and confirm the TC-A flight plays. Log the machine, commit, and result.

## Standalone checks

Run the Python reference model from the repo root:

```powershell
python -B .\Docs\CLAUDE_RESEARCH\reference_model.py
```

Compare its output with [TEST_CASES.md](CLAUDE_RESEARCH/TEST_CASES.md). Use [the standalone harness instructions](../Tools/StandaloneTests/README.md) for the assembly, screen-binding, and timeline suites. Standalone engineering, dashboard, and session results are documented in [Build status](BUILD_STATUS.md).
