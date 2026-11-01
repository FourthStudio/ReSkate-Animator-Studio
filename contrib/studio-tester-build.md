# Build and validate Animator Studio

Studio 1.1.0 on ReSkate 2.1.3 combines the Library/modifier workspace, Fourth's Market/editor/preview work and Norms' release fixes. See the [integration record](studio-library-integration.md), [README](../README.md) and [release acceptance](studio-release-checklist.md).

## Local Windows build

Install Visual Studio 2022's C++ desktop tools, CMake/Ninja and Python 3.11 or newer for the Python tests and packaging helpers. Use a Developer PowerShell, or dot-source `contrib/enter-msvc.ps1` in the shell that builds.

C++ dependencies are vendored. If Cargo is present, the Release runtime also builds the optional Spotify helper; its initial Cargo dependency fetch needs network access. Without Cargo the runtime builds without that helper. Python native-pipeline tests need the packages in `contrib/animator-studio/requirements-test.txt`.

```powershell
. ./contrib/enter-msvc.ps1
python -m pip install -r contrib/animator-studio/requirements-test.txt
# Optional, when building the Spotify helper:
# cargo fetch --locked --manifest-path SpotifyHelper/Cargo.toml
cmake --preset ci-windows
cmake --build build/ci-windows --config Release --target dingosdk_runtime dingosdk_launcher dingosdk_regression_tests --parallel 4
ctest --test-dir build/ci-windows -C Release --output-on-failure
python -m unittest discover -s contrib/animator-studio -p 'test_native*.py'
python -m unittest discover -s contrib/Test -p '*tests.py'
```

The preset stamps ReSkate 2.1.3 / AnimatorStudio 1.1.1 and turns binary auto-update off. Its release source is the Studio repository and crash uploads are disabled for this modified build. Outputs are under `build/ci-windows/Release`. Warnings are errors. `dingosdk_regression_tests` builds all enabled `dingosdk_*_tests` executables and the graph-probe self-test: 64 CTest suites with the Studio, plugin and launcher options enabled. Optional plugin fixture folders absent from a checkout are reported as skipped by the plugin suite.

Also run the production interaction checks from a VS developer shell:

```powershell
python contrib/check_studio_modifier_steam.py
python contrib/check_studio_native_modifiers.py
python contrib/check_studio_storage_reports.py
python contrib/preview_studio_library.py build/library-preview
```

## CI and downloads

[Studio tester build](../.github/workflows/studio-tester.yml) runs only when started manually. It builds runtime/launcher/test targets, runs C++ and Python regressions, then uploads ReSkate.dll directly. Artifacts expire after one day and repository access may be required to download them. They are temporary review builds, not permanent public releases.

The complete native-library ZIP is **manual opt-in**: start the workflow with `full_library` enabled. This adds the pinned library download, SHA256 and per-file validation, preview/index/document/name checks, isolated launcher staging and ZIP packaging. Ordinary PR builds do not run those complete-library steps.

```powershell
# GITHUB_TOKEN needs contents-read access while the pinned library is private.
python contrib/fetch_studio_test_library.py --output work/native-library.zip
python contrib/stage_studio_tester.py --build build/ci-windows --library work/native-library.zip --output work/tester-stage
python contrib/package_release_zip.py work/tester-stage ReSkateStudio-Test.zip
```

Use a fresh staging directory. The library asset, SHA256, counts and supported build are pinned in `config/studio-test-library.json`. The helper verifies the archive and each payload, refuses legacy position decoding and excludes personal presets/account files. Never include a development game installation in a public package.

If GitHub reports an artifact storage quota failure, inspect the failed step. Successful compilation/tests do not imply a deliverable was uploaded. Deleting obsolete artifacts and short retention prevent further storage accumulation; billing usage may still block uploads. Do not mark a failed upload as success to hide the problem.

## Validate a supplied library

The studio can rebuild an outdated native library from the installed game. When shipping the optional prebuilt library, validate that exact data with the reader from the intended source commit:

```powershell
$tool = './build/ci-windows/Release/dingosdk_animation_studio_tests.exe'
$library = 'work/tester-stage/ReSkate.Tricks/library'
& $tool --native-catalog $library
& $tool --native-index "$library/native-catalog.json"
& $tool --native-documents $library
& $tool --native-labels "$library/native-catalog.json" work/native-readable-names.json
```

Check every exit code when running these manually; CI does so. These validate decoding and metadata, not visual coherence or live gameplay bindings.

## Tester behavior

Preview speed changes watching speed only. Persistent gameplay selection uses the Library's default playlist and optional held-button playlist. The edited playlist is independent of both assignments. Fixed priority is resolved before a winning pack's Alternate variants; exact stance and Front foot matching must be tested separately. ANY-pop saves and imported packs may use generated fallback versions; your own explicitly single-pop saves retain that binding. Compare the actual pop and source timing.

Gameplay uses native/source timing without catch slowdown or an airborne foot magnet. CATCH HOLD and DROP PREVIEW controls are removed; the optional timeline CATCH marker remains. Supported held native loops can play, while an indexed transition alone does not imply live replacement support.

An old experimental `ReSkate-NativeKickflipLab` mod can still override resources independently of Studio selections. Keep research mods out of release/test installations. Complete the [acceptance checklist](studio-release-checklist.md) on the exact DLL to be distributed.
