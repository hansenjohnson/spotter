# Development Log

This file holds the running development history for this project --
what changed, why, what was verified and how, and known limitations --
moved out of README.md (per direct request) to keep that file focused
on actually using and building the plugin. Entries are in roughly
chronological order (oldest changes near the top, most recent near the
bottom), each written at the time that change was made.

## Real Windows install attempt: DLL built but OpenCPN couldn't load it -- create_pi wasn't exported

First real test of the actual built `spotter_pi.dll` inside a real
OpenCPN installation on Windows, after several rounds of getting the
Windows CI build itself working. The DLL was correctly placed in
OpenCPN's plugin folder and detected as a wxWidgets-compatible
candidate, but failed to load:

```
ERROR dlmsw.cpp:167 Couldn't find symbol 'create_pi' in a dynamic
library (error 127: The specified procedure could not be found.)
```

Root cause, confirmed by reading the actual vendored header rather
than guessing: every OpenCPN plugin must export `create_pi`/
`destroy_pi` (its factory functions) by name, via `DECL_EXP` --
`libs/api-18/ocpn_plugin.h`'s own macro for
`__declspec(dllexport)` on Windows. That macro's platform check is:

```cpp
#if defined(__WXMSW__) || defined(__CYGWIN__)
#define DECL_EXP __declspec(dllexport)
...
```

`__WXMSW__` is defined by wxWidgets' own headers, not by the compiler
or by us -- and `spotter_pi.h` (this project's own file) included
`ocpn_plugin.h` as its very first line, before any wxWidgets header
anywhere in the chain. At the point `ocpn_plugin.h`'s platform check
ran, `__WXMSW__` genuinely wasn't defined yet, even on a real Windows/
MSVC build, so `DECL_EXP` silently fell through to its final, empty
`#else` case -- no export attribute at all. `create_pi`/`destroy_pi`
compiled fine (that's just a function definition) but were never
actually visible to OpenCPN's dynamic loader.

Fixed by adding `#include <wx/wx.h>` immediately before
`#include "ocpn_plugin.h"` in `spotter_pi.h` -- the file that actually
defines `create_pi`/`destroy_pi` (in `spotter_pi.cpp`, which includes
`spotter_pi.h` first). Also fixed the same include-order issue in
`ocpn_plugin_defaults.cpp` and `test_harness/ocpn_stubs.cpp` for
consistency, even though neither is functionally required for this
specific bug: `ocpn_plugin_defaults.cpp` only defines method *bodies*
for a class already declared elsewhere (virtual dispatch within the
same DLL doesn't need `dllexport` to work), and the test harness stubs
file isn't even built on Windows right now. `DataTab.h` and
`LogWindow.h` were already safe -- both already include `<wx/wx.h>`
as their own first line, before anything else. `TrackRecorder.cpp`
was already safe too, since it includes its own header (which already
starts with `<wx/wx.h>`) before separately including `ocpn_plugin.h`
directly.

Verified: confirmed via `nm -D` that `create_pi`/`destroy_pi` are now
exported symbols in the built library (Linux `.so`, where this
specific bug doesn't reproduce -- GCC's visibility attribute doesn't
depend on any wx-defined macro the way MSVC's `__declspec(dllexport)`
path does here, so this only confirms the fix is structurally sound,
not that it resolves the original Windows symptom). Rebuilt and reran
the full test suite locally (242/242, unaffected). The actual fix is
unverified until the next real Windows build is installed into a real
OpenCPN and confirmed to load without the `create_pi` error.

## Code review: removing deprecated/unused functionality

Per direct request, ahead of starting beta distribution. Used
`cppcheck --enable=unusedFunction` across `src/` as a starting point,
then manually verified every flagged item against the real codebase
before touching anything -- cppcheck's cross-file call analysis isn't
fully reliable here (it flagged several genuinely-used functions called
only from a different `.cpp` file than their own, e.g.
`TryStartEditingCurrentCell`, `RefreshDisplay`, `WatchColumnValue`, all
called from `LogWindow.cpp`) and it can't see virtual-override call
sites at all (everything in `ocpn_plugin_defaults.cpp`, wxGridTableBase
overrides in `GenericGridTable.cpp`, wxGridCellEditor overrides in
`DataTab.cpp`, `wxApp::OnInit()` in the test harness -- all correctly
still in use, just invoked by a framework cppcheck can't trace into).

**Confirmed genuinely dead, removed:**
- `CsvUtils::AppendLine` -- zero call sites; looks like a leftover from
  a raw-NMEA-log feature that was never actually built.
- `LatLonFormat::CycleToNext` and `CurrentLabel` -- both dead in the
  real app since an earlier round changed the format selector from a
  cycling button to a dropdown; `CycleToNext` was still being exercised
  by a test, so that test was removed too rather than keep testing dead
  code.
- `GenericGridTable::NumDataCols`, `Cols`, `RemoveDataRow` -- all zero
  call sites.
- `DataTab::GetVesselCog` and its backing member `m_fixCog` -- write-
  only; `SetVesselFix()` stored a vessel course-over-ground value
  nothing ever read. Traced the `cog` parameter through its entire call
  chain (`SpotterPlugin`'s GPS fix handler -> `LogWindow::
  NotifyVesselFix` -> `DataTab::SetVesselFix`) and removed it from all
  three, rather than leave a parameter that's accepted and silently
  dropped (which would also have triggered a real
  `-Wunused-parameter` warning once the one line using it was gone).
- `DataTab::SetupMarkerControls` -- required actually tracing the call
  site, not just the function itself: its only caller was guarded by
  `if (m_surfacing)`, and `m_surfacing` is *only ever assigned* inside
  the `#if 0` block that disables the Surfacings tab (confirmed via
  `grep` -- the sole assignment site is inside that block), so the
  condition is permanently false and the call was unreachable. This
  function was already known to be superseded -- a comment elsewhere in
  `LogWindow.cpp` already described it as "(removed)" when explaining
  where the current `BuildMarkerControlsRow()` came from -- just never
  actually deleted.

**Explicitly not touched:** the Surfacings tab itself (and its `#if 0`
block) -- kept intact and disabled, per an earlier, deliberate decision
pending further thought about how it should relate to Sightings data;
this is a different thing from genuinely dead code, and removing it
wasn't part of this request.

Verified: full rebuild with `-Wall -Wextra` (zero warnings in this
project's own code, same as before), full test suite (242/242 -- 243
minus the one test that only existed to exercise the now-removed
`CycleToNext()`), and a fresh-unzip rebuild+retest to confirm the
packaged state matches.

## Fourth real CI run: _CONSOLE didn't fix it; decided to just skip the test harness on Windows

Direct follow-up to the previous entry. A second real CI run, with
`_CONSOLE` defined for `spotter_test_harness` as attempted there, hit
the *exact same* linker error:

```
MSVCRT.lib(exe_main.obj) : error LNK2019: unresolved external symbol _main
```

So `_CONSOLE` alone isn't sufficient here -- whatever wxWidgets'
actual, current logic for choosing between `main()`/`WinMain()` is (a
newer wxWidgets version, or additional conditions beyond that one
macro), it didn't resolve this. Rather than keep spending CI runs
guessing at an obscure entry-point quirk for a target that's purely a
development convenience -- `spotter_test_harness` is never shipped,
never referenced by OpenCPN, and has no bearing on whether the actual
plugin works -- asked directly whether it could just be skipped on
Windows, and yes: it's been extensively verified on macOS/Linux
throughout this entire project (243 checks, continuously run), so
losing Windows coverage for it specifically costs nothing that matters
for actually using the plugin.

Changed `.github/workflows/build-windows.yml`'s Build step to
`cmake --build build --config Release --target spotter_pi`
(previously building the default target set, which included
`spotter_test_harness`) -- explicitly scoped to just the real
deliverable. Made the same change to README.md's local Windows build
instructions, since anyone building by hand would hit the identical
issue otherwise. Left the `_CONSOLE` define in `CMakeLists.txt` in
place (harmless, and it's possible it's a real part of a fuller fix
someone could build on later) but corrected its comment to honestly
reflect that it did not, on its own, resolve the problem -- rather
than leave a confidently-worded comment claiming a fix that a real CI
run had already contradicted.

Verified: rebuilt and reran the full test suite locally (243/243,
unaffected -- this only touched the Windows-specific workflow file and
documentation, not any C++ source, and the test harness still builds
and runs normally as part of the regular macOS/Linux build/test cycle
this project has used throughout). The next CI run should build only
`spotter_pi.dll` and, having now built successfully twice in a row
before hitting the (now bypassed) test-harness-only failure, should
succeed cleanly.

## Third real CI run: spotter_pi.dll actually built; test harness hit a Windows entry-point mismatch

Milestone: **`spotter_pi.dll` -- the actual deliverable -- built
successfully** (`spotter_pi.vcxproj -> D:\a\spotter\spotter\build\Release\spotter_pi.dll`).
The `opencpn.lib` fix from the previous entry resolved linking
completely for the real plugin target.

The only remaining failure was in `spotter_test_harness` (a dev-only
tool, never shipped to users):

```
MSVCRT.lib(exe_main.obj) : error LNK2019: unresolved external symbol _main
```

Root cause: `test_harness/main.cpp` uses wxWidgets' own
`wxIMPLEMENT_APP(TestApp)` macro for its entry point (not a hand-written
`main()`), which works fine on macOS/Linux but, on MSVC specifically,
generates a `WinMain()` (Windows/GUI subsystem) entry point by default.
This target links as a Console-subsystem executable instead --
deliberately, since test output needs to actually print to stdout/the
CI log, not disappear into a windowless GUI process -- and the Console
subsystem's CRT startup code expects `main()`, not `WinMain()`. Fixed
by defining `_CONSOLE` for this target on MSVC specifically
(`if(MSVC) target_compile_definitions(spotter_test_harness PRIVATE
_CONSOLE) endif()`) -- wxWidgets' own long-documented convention
(predating CMake, from when wxWidgets projects were hand-configured
Visual Studio projects that defined this automatically) for getting
`wxIMPLEMENT_APP()` to generate `main()` instead.

Verified: rebuilt and reran the full test suite locally (243/243,
unaffected, since the `if(MSVC)` guard makes this a no-op everywhere
else). The actual MSVC entry-point resolution is unverified until the
next real CI run -- `_CONSOLE` is wxWidgets' documented mechanism for
this, but "documented" and "confirmed working here" are different
things without an actual MSVC environment to check it in directly.

## Second real CI run: every source file compiled; link failed on a duplicate, hardcoded opencpn.lib path

Strong progress from the previous run's fix -- the stale-`build/`
problem is gone (no CMakeCache mismatch this time), and CMake correctly
found MSVC and wxWidgets 3.2.1 via the `-D` flags `win_deps.bat` sets
up. Every one of this plugin's source files compiled successfully. The
only failure was at the link step:

```
LINK : fatal error LNK1181: cannot open input file
'D:\a\spotter\spotter\libs\api-18\msvc-wx32\opencpn.lib'
```

Root cause: `libs/api-18/CMakeLists.txt` (the vendored OpenCPN API
headers this project bundles) has its own, completely separate,
hardcoded MSVC link requirement --

```cmake
if (WIN32 AND MSVC)
  target_link_libraries(OCPN_API_WX32 INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/msvc-wx32/opencpn.lib")
```

-- entirely independent of this project's own `-DOPENCPN_IMPORT_LIB`
mechanism (`CMakeLists.txt`'s own, correct `if(OPENCPN_IMPORT_LIB)
target_link_libraries(...)` block). Both get pulled into the final
link; `win_deps.bat` was only ever placing the downloaded file at
`cache/opencpn.lib`, which satisfies the first mechanism but not this
second, independent one -- nothing had ever placed a file at the exact
path this vendored library hardcodes.

Fixed by having `win_deps.bat` copy the downloaded `opencpn.lib` to
*both* locations (`cache/opencpn.lib` and
`libs/api-18/msvc-wx32/opencpn.lib`) rather than changing either
CMake mechanism itself -- lower-risk than modifying vendored
third-party code, and this happens to be that vendored library's own,
pre-existing, intended convention (this file structure was itself
originally vendored from the same broader OpenCPN plugin-library
ecosystem `testplugin_pi` belongs to). Added
`libs/api-18/msvc-wx32/` to `.gitignore` too, for the same reason
`cache/` is already there -- a downloaded, per-machine, regeneratable
artifact with no business being committed.

Verified: rebuilt and reran the full test suite locally (243/243,
unaffected, since this only touched `win_deps.bat` and `.gitignore`,
not any C++ source). The actual MSVC link step itself is unverified
until the next real CI run.

## First real CI run: missing .gitignore let a stale build/ directory break the Windows build

The Windows workflow's first actual run (after a live GitHub Actions
outage cleared) got much further than expected -- `win_deps.bat` ran
correctly on a real runner and successfully downloaded wxWidgets 3.2.1
and `opencpn.lib`, confirming that part of the setup genuinely works,
not just in theory. The actual failure was unrelated to any of that:

```
CMake Error: The current CMakeCache.txt directory
D:/a/spotter/spotter/build/CMakeCache.txt is different than the
directory /Users/hjohnson/Projects/spotter_pi/build where
CMakeCache.txt was created.
```

Root cause: this project never had a `.gitignore`, so a `build/`
directory from a local build on a Mac got swept into the initial `git
add .` and committed. A stale `CMakeCache.txt` hardcodes the absolute
path it was configured in; checking that same `build/` folder out onto
a Windows CI runner, with a completely different path, is exactly the
scenario CMake refuses to silently continue with.

Fixed two ways: added a proper `.gitignore` (the real `testplugin_pi`
template's own `.gitignore` doesn't cover this either -- it's minimal
and doesn't exclude `build/` at all, so it wasn't something to copy
verbatim; this one is specific to this project's actual needs,
covering `build/`, and `cache/` from `win_deps.bat`'s own output).
Also made the workflow's Configure step self-healing (`if exist build
rmdir /s /q build` before creating a fresh one) as cheap insurance,
independent of the `.gitignore` fix, so a leftover `build/` directory
from any cause can't break CI the same way again.

Practical note for whoever's managing the actual GitHub repo: adding
`.gitignore` now doesn't retroactively remove `build/` from git's
history/tracking on its own -- the already-committed copy needs `git
rm -r --cached build` (then commit) once, after which `.gitignore`
will keep it out going forward.

## GitHub Actions workflow for a real MSVC Windows build

Direct follow-up to getting Windows testing working without compiling
locally there, and without the official catalog. Added
`.github/workflows/build-windows.yml`, which builds `spotter_pi.dll` on
GitHub's own free, hosted `windows-2022` runners (real Visual Studio
2022/MSVC, matching what OpenCPN's own Windows build uses -- avoids the
C++ ABI risk a MinGW-cross-compiled DLL would carry against an
MSVC-built OpenCPN.exe) on every push, or on demand via
`workflow_dispatch`. The finished DLL is attached to the run as a
downloadable artifact.

This was tractable to write with real confidence specifically because
it doesn't need to replicate `testplugin_pi`'s full CI pipeline
(CircleCI/AppVeyor/Cloudsmith, package-format detection, etc, none of
which this project uses) -- it just runs the exact same, already-
documented local build steps (`msvc/win_deps.bat`, then `cmake -A Win32
-G "Visual Studio 17 2022"` with `wxWidgets_ROOT_DIR`/`LIB_DIR`/
`OPENCPN_IMPORT_LIB`) on GitHub's infrastructure instead of a person's
own machine. Single source of truth: the workflow calls
`msvc/win_deps.bat` directly rather than reimplementing its logic
separately, so the two can't drift out of sync.

One real, worth-noting gotcha caught before it caused any confusion:
the workflow's YAML file, run through a strict YAML 1.1 parser, has its
top-level `on:` key parsed as the boolean `true` rather than the string
`"on"` -- a well-known quirk of that spec (`on`/`off`/`yes`/`no` are all
boolean literals in YAML 1.1) that happens to collide with GitHub
Actions' own trigger-configuration key name. GitHub's own workflow
parser handles this correctly regardless, but the key was quoted
(`"on":`) anyway to remove any ambiguity, rather than relying on that
being fine everywhere the file might ever be parsed.

Updated README.md's "Installing and testing on Windows" checklist to
offer this as the primary path (step 2), with the existing local-build
instructions kept as an explicit alternative for anyone who'd rather
build on their own machine.

Honest status: like the rest of the Windows build path, this workflow
has been written carefully against real, verified commands, but has
not actually been run -- doing so requires this repository to exist on
GitHub and a workflow run to be triggered there, which isn't possible
from within this development environment. Validated only at the level
that's actually checkable here: confirmed the YAML itself parses
correctly and is structurally well-formed.

## Adopting testplugin_pi/Frontend2's conventions -- style, metadata, Windows build

Per direct request, working toward eventual official-catalog readiness
by adopting parts of OpenCPN's standard plugin template
(`testplugin_pi`, aka Frontend2) -- specifically, the parts that don't
require external accounts/services (CircleCI, AppVeyor, Cloudsmith)
this project doesn't have set up, since those can't be meaningfully
adopted or tested without them. The user provided a full, current
`testplugin_pi` checkout directly, which made this possible to do
against real, verified files rather than fragments pieced together from
search results.

**Style**: `.clang-format` and `.cmake-format.yaml` copied verbatim
from the real template (Google C++ style, 2-space indent, 80-column).
`.editorconfig` was *not* copied verbatim -- it actually contradicts
the real `.clang-format` in the same repo (4-space/latin1 vs.
2-space/UTF-8 for the same file types), so a corrected version
consistent with `.clang-format` was written instead of importing a
contradiction. The entire codebase was then reformatted with
`clang-format --style=file`; verified the resulting diff was purely
cosmetic (pointer/reference alignment, e.g. `const wxString &field` ->
`const wxString& field`) with no semantic changes, then rebuilt and
reran the full test suite to confirm.

**`CMakeLists.txt`**: added a settings/metadata block at the top using
the template's real, verified variable names and structure
(`VERBOSE_NAME`, `COMMON_NAME`, `VERSION_MAJOR/MINOR/PATCH/TWEAK`,
`OCPN_API_VERSION_MAJOR/MINOR`, `XML_SUMMARY`/`XML_DESCRIPTION`, etc),
sitting above this project's own existing, working build logic
unchanged. Deliberately did *not* adopt the template's
`opencpn-libs`-git-submodule-based dependency system or its five
`Plugin*.cmake` includes (`PluginSetup`/`PluginConfigure`/
`PluginInstall`/`PluginLocalization`/`PluginPackage`) -- read all of
them in full from the real checkout (2162 lines total across the
`cmake/` directory) and they're heavily built around the full CI/
Cloudsmith/package-format-detection pipeline (deb/rpm/flatpak/Android
target detection, Cloudsmith upload URL construction, etc), none of
which applies to a project that's deliberately staying off that
pipeline for now. Swapping in a large, mostly-untestable dependency
system for a part of the template that doesn't serve this project's
current goals seemed like a bad trade against the risk of breaking the
proven, working build.

**`packaging/metadata.xml`**: rewritten to match the template's actual
`cmake/in-files/plugin.xml.in` field-for-field (found and read directly
from the real checkout) -- `<n>`/`<version>`/`<release>`/`<summary>`/
`<api-version>`/`<open-source>`/`<author>`/`<source>`/`<description>`/
`<target>`/`<build-target>`/`<build-gtk>`/`<target-version>`/
`<target-arch>`/`<tarball-url>`/`<info-url>`, each resolved by hand
against this project's own settings-block values. Validated as
well-formed XML. Still describes only the macOS/arm64 build this
project is actually developed against -- a real catalog submission
needs one such file per supported platform, and real hosted values for
`<source>`/`<tarball-url>` once those exist.

**`<info-url>` (and the same URL wherever else it appeared -- the
in-app plugin description, three README mentions) changed from a
placeholder personal website (`hansenjohnson.org/spotter/`) to
`https://github.com/hansenjohnson/spotter`**, per direct suggestion --
guaranteed to actually exist and stay stable once that repo is pushed
to, unlike a placeholder site that might never go up. Updated
consistently everywhere the old URL appeared, rather than just in the
one place it was first asked about, since having it point to GitHub in
`XML_INFO_URL` while the in-app description still showed the old
placeholder would have been inconsistent.

**Windows build**: added `msvc/win_deps.bat`, adapted directly from the
real, working `testplugin_pi/msvc/win_deps.bat` (simplified -- no
Cloudsmith/NSIS/poedit/localization tooling, since this project isn't
producing a catalog-ready package yet, just a local build) -- fetches a
prebuilt wxWidgets 3.2.1 for MSVC directly from wxWidgets' own GitHub
releases, and OpenCPN's `opencpn.lib` import library from
SourceForge, both via URLs read directly from the real script rather
than guessed at. This also resolved a real gap from an earlier,
from-scratch attempt at cross-compiling this plugin for Windows: at the
time, no known-good source for `opencpn.lib` itself had been found:
turns out it's directly downloadable, no need to build OpenCPN.exe
first. Updated the README's "Building on Windows" section to use this
script and the real MSVC/Visual-Studio-based build command from the
template's own `ci/circleci-build-msvc.bat`, and removed an earlier,
now-misleading mention of MinGW-w64 cross-compilation as a way to
"verify" this code compiles for Windows -- true as far as it goes, but
a MinGW-built DLL is not a safe stand-in for a real MSVC build, given
the C++ ABI risk of loading a MinGW-compiled plugin into an MSVC-built
OpenCPN.exe.

Honest status, same as before: none of this has been verified on an
actual Windows machine as part of this project. What's changed is that
the Windows build steps are now adapted directly from a script that is
independently known to work (real, published OpenCPN plugins use this
same template), rather than written from scratch -- a meaningfully
better starting point, but not the same as having actually run it here.

## On-effort time freezing; Obs initials case bug; new logo

Two real bugs and a logo update, all from direct reports/requests.

**On-effort time in the Summary tab appeared frozen instead of steadily
increasing while still actively on effort.** `ComputeSummary()`'s
on-effort calculation summed gaps *between consecutive* Effort rows
(each row closing the gap opened by the previous one) -- correct for
every gap except the very last one, which has no "next" row yet to
close it against. So if the most recent Effort row was ON, the time
from that row up to the current moment was never added at all; the
figure only ever changed when a *new* row was logged, not continuously.
Fixed by explicitly adding that final, still-open gap (from the last
row's timestamp to `wxDateTime::Now()`) whenever the most recent row's
Effort value is "ON". Verified live (a row logged 10 minutes ago as ON
correctly shows ~10 minutes of on-effort time, computed fresh each
time, not cached) and now a permanent regression test.

**Typing an observer's initials in the Obs column broke on the third
character specifically, when all three were typed in lowercase.** Root
cause: `SearchableChoiceGridCellEditor::OnText()`'s exact-match check
(is what's typed so far already a complete, valid choice, just possibly
different case?) correctly *detected* a case-insensitive match and
stopped trying to suggest further -- but never actually corrected the
text field's casing to match. That mattered specifically because of how
the inline-suggestion-then-type-over interaction works: typing "m" gets
completed inline to a suggested choice (correct casing, since it comes
from the choice list, not the user's own keystroke), with the
unconfirmed remainder highlighted; typing over that highlighted portion
replaces it with whatever case the user actually typed. Three lowercase
keystrokes in, the text was a mix of suggested-casing and typed-casing
characters, still case-insensitively equal to a valid choice, but not
exactly it -- and the check at commit time (`EndEdit()`, matching
against the choice list) *is* case-sensitive, since case-sensitive
matching is what makes the "this doesn't match any of the standard
options" warning meaningful at all. Fixed by having the exact-match
check normalize the text field's casing when it finds a match that's
case-insensitively but not exactly correct, rather than just leaving it
alone. Verified with a real, live grid interaction (not just a unit
check of the underlying string logic) -- opened an actual Obs cell's
combo editor, typed "m", "d", "m" in lowercase against a two-entry
observers.csv specifically chosen to reproduce the bug ("MD", itself a
prefix of "MDM"), and confirmed the field lands on exactly "MDM". Now a
permanent regression test.

**New logo artwork.** Same processing pipeline as the previous logo
change: cropped to content bounds, padded onto a square canvas,
downsized to 256x256, RGB inverted to pure white (alpha unchanged),
embedded as a byte array in `SpotterIcon.h`. Verified via pixel
inspection (opaque white pixels present, no opaque black pixels
remaining) since the image-view tool was intermittently unavailable
during this session.

## Correction: new surveys should have a blank Effort tab, not an auto-added "OFF" row

A direct follow-up correction to the entry just below. The previous
round had `StartNewSurvey()` add one initial Effort row with Effort
explicitly set to "OFF", specifically so the status bar wouldn't show
the ambiguous "not set" for a brand new survey's Effort status.
Reverted per direct request: a new survey's Effort tab should be
genuinely blank (zero rows), and "not set" is the correct, expected
initial status -- not something to engineer around. Removed the
`AddRow()` call from `StartNewSurvey()` entirely; `CurrentEffortStatus()`
naturally returns blank again with zero rows, and the status bar's own
existing "not set" fallback (unchanged) handles the display. Updated the
permanent regression test from the previous round to check for the
now-correct zero-row, blank-status behavior instead. Verified live:
confirmed the Effort tab has zero rows after `StartNewSurvey()`, and
that the actual on-screen status bar labels read "OFF" (Tracking) and
"not set" (Effort).

## New surveys start with Tracking and Effort both OFF; Summary shows survey start/end time

Two direct requests, addressed together since both touch
`StartNewSurvey()`/`ComputeSummary()`.

**Tracking and Effort now both start OFF on a new survey.** An earlier
round had `StartNewSurvey()` automatically turn Tracking on (both the
track recorder's own enabled state and the persisted `TrackingSettings`
preference) the moment a survey was named -- removed per this direct
request; the user now turns it on deliberately once actually
underway, the same as Effort already effectively required. Effort
itself needed a different fix: `GetEffortStatusText()`/
`CurrentEffortStatus()` read the *most recent Effort row's* Effort
value, and a brand new survey has zero Effort rows at all -- so it
returned "not set" (shown neither ON-green nor OFF-red in the status
bar) rather than an explicit "OFF", unlike Tracking (always an explicit
ON or OFF). Fixed by having `StartNewSurvey()` add one initial Effort
row with Effort explicitly set to "OFF" (its already-correct default
for a new row, just previously never actually created until the user
added one themselves). Verified live end-to-end (forced Tracking ON
first, to confirm `StartNewSurvey()` genuinely turns it back off rather
than it just happening to already be off) and now permanent regression
tests.

**Summary tab now shows "Survey period: `<start>` to `<end>`" as its
first line**, sourced from the track's own minimum and maximum
timestamps -- `ComputeSummary()` already computed these internally (as
`firstTime`/`lastTime`, used to derive the existing trackline duration
figure) but didn't expose the absolute timestamps themselves; added
`trackStartTime`/`trackEndTime` to `SurveySummary` to carry them, and a
matching "Survey start"/"Survey end" pair of rows (also first) in the
exported summary CSV. Verified live and now a permanent regression
test -- which surfaced a real, if minor, test-isolation gotcha along
the way: `TimeZoneSetting`'s state is a process-global static variable,
persisted to a shared file across every `SpotterPlugin` instance in the
same test run, so a fresh plugin instance's `Init()` can silently
reload whatever zone an earlier, unrelated test block left set (a
4-hour offset showed up in the first attempt at this test, matching
Eastern Time) unless a test explicitly resets it first rather than
assuming "System Default" just because nothing in that specific test
block changed it.

## Settings tab dropdowns not showing a selection on first view

Four dropdowns on the Settings tab -- Lat/Lon format, Timezone, and the
Sightings/Events marker shape pickers -- showed no selection at all the
first time a user ever opened that tab, reported directly. Root cause:
all four called `wxChoice::SetSelection()` once, immediately at
construction time, the same category of bug already fixed for the
bottom bar's View dropdown and `ApplyLayoutPreset()` -- `SetSelection()`
doesn't reliably stick on a control that isn't yet actually shown. The
View dropdown fix (move the call into a deferred `CallAfter()` at
window construction) wasn't sufficient here, though: these four
dropdowns live *inside the Settings notebook page itself*, which isn't
shown at all until the user switches to it (Sightings is the initially-
selected tab), so a one-time startup `CallAfter()` can fire before that
page has ever been realized.

Fixed by re-applying all four dropdowns' selection in `OnPageChanged()`,
every time the Settings tab actually becomes the visible page, rather
than only once at startup -- the same general pattern this function
already uses for Summary (recomputed fresh every time it's shown) and
for a newly-visible data tab's stale column widths/row heights.
`BuildMarkerControlsRow()` (a free function, called twice, once for
Sightings and once for Events) needed to start returning the `wxChoice*`
it creates so `LogWindow` could keep a reference to each one.

Also considered, in the same conversation, then explicitly reverted per
a direct follow-up correction: changing the Timezone dropdown's default
from "System Default" to "UTC". It stays "System Default".

Verified live end-to-end: switched to Settings for the first time on a
fresh plugin instance and confirmed all four dropdowns show a real
selection (Decimal/DDM/DMS index 1 for Lat/Lon format matching its
actual default, System Default for Timezone, Diamond/Square correctly
for the two marker shapes) -- not just that the underlying settings
values were correct, which they always were; the bug was specifically
about what the dropdown *displayed*. Now a permanent regression test.

## This round's changes (10 items)

- **Zip export removed again** -- reverted back to a plain,
  uncompressed `<prefix>` folder as the only export option, per direct
  request, right after the previous round had added a zip-vs-folder
  choice. The zip-specific code (`wxZipOutputStream`, the format-choice
  dialog) is gone entirely; `OnExportClicked()` now does exactly what
  the "Folder" branch used to.
- **The "View:" dropdown not showing Split Horizontal as selected on
  startup was the same class of bug as `ApplyLayoutPreset()` itself
  needing a deferred `CallAfter()`** -- `wxChoice::SetSelection()`
  doesn't reliably stick before this window has actually been shown
  either. Fixed by moving it into the same deferred block.
- **`current_survey.txt` appearing to list the same survey twice, and a
  loaded survey's name not matching its own prefix, traced to the same
  root cause**: `ParseSurveyNameFromPrefix()`, a leftover from when
  survey prefixes were automatically date-stamped, was still stripping
  a leading date-shaped portion off of loaded survey names -- fine for
  an auto-added date, actively wrong for a survey a user had
  deliberately named something date-shaped themselves (confirmed with
  the reported example, "2026-07-23_test3"). Removed entirely; the
  prefix is now used as the survey name verbatim when loading, matching
  what `StartNewSurvey()` has done for a while. Extracted the non-UI
  portion of "Load Survey..." into a new, directly-testable
  `ApplyLoadedSurveyPrefix()` method along the way, since the original
  `OnLoadSurveyClicked()` is otherwise entirely modal-dialog-driven and
  couldn't be exercised from a test at all.
- **"Load Survey..." not finding existing surveys turned out to be a
  migration gap, not a fresh bug**: anyone who used this plugin before
  an earlier round's `data/` subfolder split would have their existing
  survey files stranded at the old, top-level location -- invisible to
  "Load Survey..." (which only ever looks in the new subfolder) even
  though the files were still sitting right there. Added a one-time,
  per-load migration (`SpotterPlugin::MigrateSurveyDataFilesToSubfolder()`)
  that moves any leftover sightings/effort/events/surfacings/track
  files into the new location automatically. Verified live (a
  deliberately-planted "leftover" file gets moved, keeps its content,
  and becomes findable again) and now a permanent regression test.
- **Settings files (`current_survey.txt`, `latlon_format.txt`,
  `timezone.txt`, `tracking.csv`) moved into their own `settings`
  subfolder**, per direct request, to distinguish files this plugin
  alone ever writes from ones a user is meant to edit directly
  (species.csv, shortcuts.csv, etc, which stay directly in the base
  config folder). Same one-time migration treatment as the survey-data
  subfolder got in an earlier round. A hidden `.settings/` folder was
  considered and not implemented -- macOS/Linux's leading-dot hidden-
  file convention doesn't carry over to Windows at all (a dot-prefixed
  folder there is just an ordinarily-visible one with an odd name), so
  a plain `settings` name seemed like the more consistent, predictable
  choice across all three platforms rather than being hidden on some
  and not others.
- **The column description text sticking around after switching to a
  tab with no column-selection concept at all (Summary, Settings)** --
  confirmed as a real, reported bug. Fixed by clearing it unconditionally
  at the start of every tab switch (`OnPageChanged`); a newly-selected
  data tab's own `on_cell_selected` naturally repopulates it if
  applicable.
- **This file (`dev.md`) split out of README.md**, per direct request,
  specifically so README.md can stay focused on using and building the
  plugin rather than accumulating an ever-growing development history
  alongside it.
- **`SleepPreventer` removed entirely**, per direct request -- it
  wasn't reliably preventing idle sleep in practice, and manually
  managing this at the OS level (e.g. the system's own "prevent sleep"
  settings, or a dedicated utility) is more predictable than a plugin
  trying to do it quietly in the background. Removed the class
  entirely (`SleepPreventer.h/.cpp`), its member on `SpotterPlugin`, and
  the macOS-only IOKit/CoreFoundation framework linking in
  `CMakeLists.txt` that existed only to support it.
- **Tab-navigation shortcuts (cycling with PageUp/PageDown, jumping
  directly to a tab with 1-5) moved from being hardcoded into
  `shortcuts.csv`** as ordinary, user-editable actions (`NextTab`/
  `PrevTab`/`GoToSightings`/`GoToEffort`/`GoToEvents`/`GoToSummary`/
  `GoToSettings`), per direct request, since other users may want to
  remap these too. Required adding `PageUp`/`PageDown` as recognized
  key names in the shortcuts parser (alongside the existing `Space`/
  `Tab`/`F1`-`F12`). Verified live end-to-end via simulated key events
  (Cmd+PageDown/PageUp cycling correctly, Cmd+1/4/5 jumping to the
  right tabs) -- confirming the full path (shortcuts.csv parsing, key
  matching, dispatch) works, not just the underlying tab-switching
  logic in isolation.
## Notes / limitations

- **Export now offers a choice between a zip file and a plain
  (uncompressed) folder**, per a direct follow-up question after the
  zip-only version shipped -- asked first, each time "Export Data..."
  is clicked (`SetYesNoCancelLabels`, so the choice reads as "Zip
  file"/"Folder"/"Cancel" rather than a generic Yes/No/Cancel). Both
  paths share the exact same staging logic (every existing per-file
  export function, unchanged); they differ only in what happens to the
  staged files afterward -- zipped up with `wxZipOutputStream` (as
  before), or copied directly into a freshly-created `<prefix>` folder
  under a chosen destination via `wxCopyFile`. Verified live, thorough
  and independent of the zip path's own earlier verification: created
  an actual destination folder, copied all 7 expected files into it,
  confirmed their on-disk byte sizes, read one file's content back and
  confirmed it exactly matched the source data, and confirmed no stray
  zip file was created alongside it.

- **Survey data files moved into their own `data` subfolder**, per
  direct request, separate from this plugin's settings/config files
  (species.csv, shortcuts.csv, timezone.txt, etc), which stay directly
  in the config directory. `SpotterPlugin::ResolveSurveyDataDir()`
  resolves (and creates if needed) this subfolder; every `DataTab` and
  the track recorder are now constructed pointed at it instead of the
  base config directory, so this required no changes at all to their
  own internal file-path logic -- just what directory they're told
  about at construction time. Load Survey's directory-scanning and
  external-folder-copy destination, and the Settings tab's "Open data
  folder" link, were updated to match. Verified live by directly
  inspecting the resulting folder structure after starting a survey:
  every config file stays at the top level, and every survey data file
  (including the still-unprefixed `track.csv` that exists before any
  survey is started) correctly lands in `data/`.
- **Bottom bar buttons (View, Start New Survey, Load Survey, Clear
  Survey Data, Export Data) now wrap onto additional rows** instead of
  overflowing/getting clipped in a narrow window (e.g. Split Vertical,
  or docked side-by-side with the chart), per direct request --
  switched from a plain `wxBoxSizer` to a `wxWrapSizer`, the same
  treatment the status bar's own fields already had. The
  `AddStretchSpacer` previously used to push the survey-management
  buttons to the right of the View dropdown doesn't have a sensible
  equivalent in a wrapping layout, so it's removed -- everything now
  just flows left-to-right and wraps as needed. Verified live: at a
  wide window size all five controls share one row; narrowed to 320px,
  they correctly split across two rows, and every button stays fully
  within the visible window rather than being clipped.
- **Export now produces a single `.zip` file, not loose files copied
  into a chosen folder**, per direct request, with a save-file dialog
  (defaulting to `<prefix>.zip`) letting the user choose exactly where
  it's saved, rather than only a destination folder. Every file ends up
  nested inside a folder named for the current survey *within* the
  zip, matching "a compressed folder named `<prefix>`" directly --
  extracting the zip produces a `<prefix>` folder containing everything,
  not loose files at the zip's own top level. Implemented by staging
  every existing export function's output (all unchanged) in a
  temporary directory, then building the zip from that via
  `wxZipOutputStream`, then removing the temporary directory either way
  (successful export or not). Verified thoroughly, live: confirmed the
  actual zip file is created with a sensible non-trivial size; read its
  entries back out and confirmed all of them are present, each
  correctly nested inside the expected `<prefix>/` folder; and read
  back one entry's actual CSV content and confirmed it matches the data
  that was in the grid, byte for byte.
- **Added a full "Installing and testing on Windows" walkthrough** to
  the README, per direct request -- pulls the existing build-focused
  "Building on Windows" steps together with everything else needed
  around them (installing OpenCPN itself, installing the built plugin,
  confirming it loaded, and a concrete smoke test). The same honest
  caveat as the existing Windows build section applies: written
  carefully, but not verified end-to-end on a real Windows machine as
  part of this project.

- **Toolbar icon reverted back to white-only**, per direct request --
  every other OpenCPN toolbar icon uses a single white-on-transparent
  style regardless of the active color scheme, so the previous round's
  attempt at switching between a black and a white variant based on
  color scheme (via the documented `SetColorScheme(PI_ColorScheme cs)`
  plugin API hook) was reverted in favor of just matching that existing
  convention directly. `SpotterPlugin::SetColorScheme()` and the black
  PNG variant are both removed; `SpotterIcon.h` now embeds only the
  white artwork. Verified live: the icon still decodes correctly and
  has opaque white pixels, and confirmed no opaque black pixels remain
  anywhere in it.
- **Website link added to the plugin's entry in OpenCPN's Options >
  Plugins list**, per direct request -- surfaced a real gap along the
  way: `packaging/metadata.xml`'s `<info-url>` (added in an earlier
  round) only applies to plugins installed through OpenCPN's catalog/
  plugin-manager system, not the manually-copied "legacy" install
  method this project's own README instructs (dropping the built
  library directly into OpenCPN's PlugIns folder) -- for a
  legacy-installed plugin, that file is never even read. Added the URL
  to `GetLongDescription()` instead, confirmed as the actual runtime
  API method (`opencpn_plugin::GetLongDescription()`) OpenCPN calls to
  populate a legacy-installed plugin's list entry.
- **track.csv occasionally missing the survey prefix was a real,
  confirmed bug, not just a first-run artifact.** Traced to: the track
  recorder was always constructed pointed at the plain, unprefixed
  `track.csv`, and nothing ever automatically restored a saved survey's
  prefix onto it on a normal plugin startup -- that only ever happened
  via the Start New Survey/Load Survey button handlers. So every
  OpenCPN restart mid-survey had a window (potentially the entire
  session, if those buttons were never clicked again) where new track
  points were silently recorded to the wrong file, mixed in with
  whatever was already there. Fixed by reading `current_survey.txt`'s
  prefix directly in `SpotterPlugin::Init()` and restoring it onto the
  track recorder immediately, before tracking can be enabled or any GPS
  fix recorded. Verified live (a fresh, simulated-restart plugin
  instance's track file path correctly includes the prefix immediately
  after `Init()`, without the log window needing to be interacted
  with) and now a permanent regression test.
- **Settings tab's tracking checkbox not updating after Load Survey
  turns tracking off** -- confirmed as a real, reported bug: Load
  Survey correctly disabled tracking, but the checkbox stayed visually
  checked until the Settings tab was rebuilt some other way. Fixed by
  keeping a reference to the checkbox (`LogWindow::m_trackingEnabledCheck`)
  and explicitly syncing it alongside the existing disable calls in
  `OnLoadSurveyClicked()`. Honestly noted: the full click-through path
  (actually clicking "Load Survey...", picking a survey from the modal
  dialog it shows, and observing the checkbox) isn't something this
  test environment can drive end-to-end, the same limitation other
  modal-dialog-driven flows in this project have had -- verified
  instead that the checkbox exists, is found correctly, and that the
  fix's code follows the identical pattern already used (and verified)
  elsewhere for keeping other Settings tab controls in sync.
- **DistUnit now defaults to "nm" on every new Sightings row**, per
  direct request -- the distance calculations already assumed nm by
  default, it just wasn't showing/selected in the cell itself. Added a
  new `ColumnDef::defaultValue` field (a column can have its own
  explicit default, applied regardless of the tab-wide
  `DataTabConfig::defaultChoicesToFirstOption` flag) rather than
  enabling that flag tab-wide for Sightings, which would have also
  auto-filled Species to its first alphabetical option -- exactly the
  outcome that flag was deliberately turned off to avoid, since a
  not-yet-identified sighting could then look like it was actually
  identified. The dropdown itself is unaffected -- every DistUnit
  option (m, nm, reticles) is still fully selectable, this only changes
  what a brand new row starts with. Verified live and now a permanent
  regression test.
  request -- the monospace font added in the previous round has
  different metrics (ascent/descent) than the labels' own proportional-
  width font, which made each value visibly sit slightly higher than
  its label under center alignment. Bottom alignment isn't sensitive to
  that difference the same way, since it's anchored to a fixed edge
  instead of each widget's own full line height. Verified live by
  measuring actual on-screen widget rectangles: the "Time:" label and
  its value now share the exact same bottom pixel (0px difference,
  down from a visible offset before this fix).

- **Single combined package going forward** -- per direct request, this
  plugin is now always delivered as one `spotter_pi.zip` including
  `test_harness/`, rather than a separate "distribution" (without it)
  and "dev" (with it) package. `CMakeLists.txt`'s guard around building
  the test harness (only if `test_harness/main.cpp` actually exists)
  stays in place regardless, so removing that folder later (if that
  ever becomes worth doing) still won't break a build from what's left.
- **Toolbar icon replaced with the actual logo artwork** (binoculars
  with a whale fluke in one eyepiece and a dorsal fin in the other),
  per direct request -- previously a simple spreadsheet icon drawn
  programmatically at runtime. The source artwork was cropped to its
  content bounds, padded onto a square canvas, and downsized to a
  256x256 base resolution; embedded directly as a PNG byte array
  (`SpotterIcon.h`, matching this plugin's existing "no dependency on
  external resource files being findable at some particular install-
  time path" approach) rather than shipped as a separate file.
  `MakeLogIcon()` decodes it at runtime and scales it to whatever size
  is actually needed. Verified live: the decoded bitmap is valid,
  32x32, has an alpha channel, and its pixel content matches the source
  artwork (compared directly, not just visually). (The solid-black
  silhouette's poor visibility against a dark toolbar, flagged here
  initially, was confirmed and fixed in the same session -- see the
  color-scheme-aware icon switching note further down.)
- **New "General" section in Settings** (per direct request), currently
  holding Lat/Lon format (moved here from its own standalone row) and
  the new Timezone setting below.
- **Explicit Timezone override for recorded timestamps** (`TimeZoneSetting.h/.cpp`),
  per direct request -- defaults to "System Default" (this plugin's
  original behavior: whatever the computer's own configured timezone
  is), so nothing changes unless a specific zone is chosen. Added
  specifically to avoid a survey's timestamps becoming inconsistent if
  the computer's own timezone changes mid-survey (some systems auto-
  adjust it based on location) -- exactly the concern raised alongside
  the request. Options: System Default, UTC, Eastern, Atlantic,
  Central, Mountain, Pacific -- UTC/Eastern/Atlantic were specifically
  requested; Central/Mountain/Pacific were included too since, once the
  DST-computation logic exists for one US/Canada zone, adding the
  others costs almost nothing. All of them observe DST on the same
  schedule (2nd Sunday of March to 1st Sunday of November, in effect
  since the Energy Policy Act of 2005), computed directly here rather
  than relying on the platform's own timezone database -- so the result
  doesn't depend on the computer's own timezone data being present or
  current. Applies going forward only, to both every data tab's Time
  column and track.csv -- doesn't retroactively change timestamps
  already recorded, since those are written once at row-creation time,
  not reformatted live the way Lat/Lon values are. Verified thoroughly:
  9 independently-computed reference cases (both DST transitions, in
  both directions, for two different zones, checked against externally
  computed transition dates for 2026) plus a live check confirming a
  newly-added row's actual Time value reflects the selected zone, all
  now permanent regression tests; also verified live that the setting
  persists correctly across a full plugin reload.
- **Tracking status reverted back to plain "ON"/"OFF"** (was "ON (614
  pts, 30s interval)", added in an earlier round and now removed per
  direct request).
- **Status bar's computed value fields now use a fixed-width
  (monospace) font**, per direct request -- confirmed as a real,
  reported issue: with the previous proportional-width font, Time
  updating every second could shift the whole field (and everything
  placed after it by the wrap layout) visibly left/right as digit
  widths varied slightly. Applied to every computed value field
  uniformly (Time, Vessel Position, Speed, the GPS warning slot,
  Effort check countdown, Survey, Tracking, Effort), not just Time,
  via the single shared field-building helper they all already went
  through -- only the value text is monospace, not the static labels
  before each one. `wxFONTFAMILY_TELETYPE` is used rather than a
  specific named font, so it resolves to whatever generic monospace
  font is available on the current platform. Verified live.

- **Lat/Lon format is now a dropdown** (Decimal Degrees / Degrees
  Decimal Minutes / Degrees Minutes Seconds), not a button that cycles
  through them one click at a time, per direct request -- lets you jump
  straight to the one you want and see which one is active without
  guessing.
- **Map settings split into separate "Sightings" and "Events" boxes**
  (was one shared "Map" box) per direct request.
- **A space added between weather conditions** in the Summary tab's
  display (`RefreshSummaryTab()`) -- was joined with a bare comma,
  built with a manual loop now since `wxJoin()` only accepts a
  single-character separator, not a multi-character one like ", ".
- **The Cmd/Ctrl clarifying note removed** from under "Edit Keyboard
  Shortcuts" per direct request.
- **Marker color pickers removed from the Settings tab's Sightings/
  Events Map sections** (shape picker stays) -- color is no longer a
  separately configurable, per-tab setting for these two tabs; see the
  next point.
- **Reverted the brief Color-column experiment back to a plain BOOL Map
  column** (show/hide only) on Sightings and Events, per direct
  request. An intermediate version of this plugin had replaced Map with
  a per-row CHOICE "Color" column (Default/None/a specific named
  color); that's gone now, but the species/event-based color lookup it
  introduced stays -- a charted row's marker color is still always
  resolved from its own Species/Event via species.csv/event_types.csv,
  there's just no way to override an individual row's color separately
  from its Species/Event anymore. See "How markers get their color"
  above for the full explanation.
- **species.csv/event_types.csv now store human-readable color names**
  (Orange, Blue, Navy, Teal, Purple, Red, Black, Green, Gray, Yellow,
  White, ...) instead of hex codes, per direct request -- the named-
  color palette (`NamedColorToColour()` in `DataTab.cpp`) was expanded
  with a few more names (Navy, Teal, Pink, Brown) specifically so the
  12 default species and 8 default event types could get reasonably
  distinguishable colors without needing hex codes at all.
- **On this round's testing:** the accumulated automated test suite
  (`test_harness/`) was not available going into this round -- as
  before, it's correctly excluded from the distributed package, and
  this time there was no separate copy to restore from. Rather than
  build a large test suite from scratch mid-round, every change in this
  round was verified with focused, live, ad hoc verification scripts
  (checking actual rendered UI state, actual CSV file contents, and
  actual `GetChartedPoints()` output) rather than the usual ~200-check
  regression suite. All of the following were directly, live-confirmed
  working, not just reasoned through: the Lat/Lon dropdown's exact
  options and current selection; both Map sections' presence; the
  weather-conditions spacing; the parenthetical note's removal; the
  absence of any color-picker button in Settings; the Map column's
  presence (and the Color column's absence) on Sightings; a new row's
  Map defaulting to checked; a charted point's color correctly
  resolving to its species' configured color; and that color changing
  correctly when the underlying species.csv value does. If you have a
  saved copy of `test_harness/` from a recent round, keeping it handy
  (or sending it back, the way this project recovered once before)
  would restore the fuller regression coverage for future rounds.

- **Survey names no longer get a date prefix** -- `StartNewSurvey()`
  now uses the survey name alone (sanitized) as the file prefix, per
  direct request.
- **Sightings/Events marker shape and color pickers moved** from each
  tab's own toolbar to the Settings tab's Map section, next to that
  tab's map-label picker (`BuildMarkerControlsRow()`, replacing
  `DataTab::SetupMarkerControls()` for these two tabs specifically --
  Surfacing, currently disabled, still uses the old per-tab version).
- **Lat/Lon format button moved** from every tab's own toolbar to the
  top of the Settings tab -- it's a single global format, not a
  per-tab setting, so repeating it on every tab was more clutter than
  it was worth.
- **All tab buttons (Add Row, Delete Row, Undo, Redo) left-justified**,
  reversing an earlier round's right-justified Undo/Redo.
- **Column auto-resize at larger grid font sizes was a real, confirmed
  bug, not just a minor cosmetic issue.** `SetGridFontSize()` used to
  reset every column back to its static, hardcoded base width
  (designed around the *default* font size) regardless of the new font
  size's actual space needs -- at 14pt, Time and Lat/Lon cells were
  genuinely getting clipped. Fixed by re-measuring actual cell content
  at the new font size (`UpdateContentMinWidths()`, via wxGrid's own
  `AutoSizeColumns()`) and applying `ResizeColumnsToFit()` instead of
  the old static reset. Verified live (measured rendered text width
  against column width directly at 14pt) and with a permanent
  regression test.
- **Helper text added next to each Settings tab file link**, briefly
  describing what each one is for.
- **`dropdowns.csv` split into four separate config files**
  (`species.csv`, `event_types.csv`, `observers.csv`,
  `behaviors.csv`), each with its own extra columns (a default map
  color for species/events, a species code, an observer's full name, a
  behavior code) -- see the dedicated section above for the full
  layout and reasoning. Implemented as one generic, reusable
  `CategoryConfigFile` class rather than four near-identical ones.
  **A real bug caught by the test suite while building this:** the
  events list was initially just named `events.csv`, which collided
  with the Events tab's own unprefixed data file of the same name
  (used before "Start New Survey" is ever clicked) -- the existing
  "file created on first run" test failed immediately, confirming the
  overwrite before it could cause any actual data loss. Renamed to
  `event_types.csv` to fix it. This is a good example of exactly why
  this project's test-everything discipline matters, especially in an
  experimental round like this one.
- **Map column (BOOL: show/hide) replaced with a Color column (CHOICE:
  Default/None/a specific named color)** on Sightings and Events -- see
  the dedicated section above for the full behavior. `ChartPoint`
  gained a per-point `color` field (previously, `spotter_pi.cpp`'s
  marker-drawing code used one color for an entire tab's worth of
  points at once); `DataTab::chart_default_color_lookup` resolves
  "Default" against the relevant `CategoryConfigFile`, falling back to
  that tab's own configured marker color if the species/event name
  isn't found there. Verified with 5 dedicated automated checks (a
  known species' Default color resolves correctly, None removes the
  point, a specific named color overrides both) plus a live check of
  the actual dropdown contents and ordering. The Color column's own
  choices needed the same order-preservation treatment as Glare in an
  earlier round (`ColumnDef::preserveChoiceOrder`) -- caught this one
  proactively this time, since "Default" and "None" being alphabetized
  in among the plain color names would have buried the two most
  commonly used options in the middle of the list.
- **This was an unusually large, explicitly experimental round**
  (nine substantial, partly interdependent changes requested together,
  with an explicit request to be prepared to revert). A checkpoint was
  saved before starting and updated after each completed item, so any
  single change could have been rolled back independently if it had
  caused problems -- in the end, every item completed cleanly with the
  full test suite passing throughout, so no revert was needed. Worth
  noting for anyone reviewing this diff: the accumulated ~190-check
  test suite was very nearly lost partway through this session (a
  packaging step earlier in this project's history deliberately
  excludes `test_harness/` from the distributed zip, which is correct
  for end users but meant it wasn't part of what was available to
  build on going into this round) -- it was recovered from a copy the
  person requesting these changes happened to still have on hand.
  Please hang onto a copy of `test_harness/` separately from the
  distributed package if you want to guarantee it stays available for
  future development.

- **Renamed to "Spotter" throughout** (was "Whale Sightings" internally,
  "Spotter Log" in the window title) per direct request -- the project
  directory, every source file (`whale_sightings_pi.cpp/h` →
  `spotter_pi.cpp/h`), the class name (`WhaleSightingsPlugin` →
  `SpotterPlugin`), the CMake project/target name, the data directory
  (`~/Library/.../whale_sightings/` → `.../spotter/` -- safe to rename
  outright since this plugin has no existing user base yet to migrate),
  and the window title (now reads "Spotter", not "Spotter Log", exactly
  as requested) all changed together. Verified with a full rebuild and
  the complete test suite (renamed along with everything else -- the
  test executable is now `spotter_test_harness`) passing cleanly
  afterward, plus a manual check for any stray "whale_sightings"/"Whale
  Sightings" references left anywhere in the source, docs, or packaging
  scripts (a few remaining "whale" mentions are legitimate -- actual
  whale species names, both in the species dropdown and in test
  variable names referencing them).
- **Plugin list website link and Preferences dialog.** Added
  `<info-url>hansenjohnson.org/spotter/</info-url>` to
  `packaging/metadata.xml` (confirmed as the correct field name by
  checking OpenCPN's actual published plugin metadata XSD schema,
  rather than guessing) -- shown as a website link next to this plugin
  in Options > Plugins' list. Also implemented
  `SpotterPlugin::ShowPreferencesDialog()`, shown when clicking
  "Preferences" there -- per direct request, just links to the same
  CSV settings files the Settings tab links to (dropdowns, shortcuts,
  positions, column definitions, display), with a note that the full
  settings UI lives on that tab, reached by actually launching the
  plugin.
- **Glare (and, on inspection, a couple of other dropdowns) were being
  silently alphabetized out of their intended order -- a real,
  reported bug, not just a Glare-specific request.** An earlier round
  added alphabetical sorting to every CHOICE column's dropdown, to help
  navigate long, naturally-unordered lists (Species, Observers) -- but
  applied it indiscriminately, which scrambled the *few* columns whose
  defined order is itself meaningful: Glare's None/Mild/Moderate/Severe
  came out as Mild/Moderate/None/Severe, and the same root cause was
  quietly affecting SpecConf and NumConf's Definite/Probable/Possible
  confidence ordering too, even though only Glare was reported. Fixed
  with a new `ColumnDef::preserveChoiceOrder` flag, set for these three
  columns specifically, that skips the sorting step entirely and keeps
  the exact as-defined order. Verified live and with a permanent
  regression test reading each dropdown's actual item order back out of
  its wxComboBox.
- **`test_harness/` excluded from the distributed package.**
  `CMakeLists.txt`'s test-harness build is now guarded on that
  directory actually existing (`if(BUILD_TEST_HARNESS AND EXISTS
  ".../test_harness/main.cpp")`), confirmed by building a copy of the
  project with `test_harness/` removed and checking it still configures
  and builds cleanly (producing just the plugin library, no test
  executable) rather than failing on a missing source file.
- **Windows compatibility.** `SleepPreventer` now has a Windows
  implementation (`SetThreadExecutionState`, the standard mechanism
  apps use to keep a machine awake during a long operation -- the same
  role `IOPMAssertionCreateWithName` plays on macOS), and
  `CMakeLists.txt` gained a Windows-specific block (DLL naming, and
  linking against OpenCPN's `opencpn.lib` import library, which Windows
  requires at link time unlike macOS's deferred symbol resolution --
  see "Building on Windows" above). Also added a short in-app note
  where `shortcuts.csv` is linked from, clarifying that its "Cmd+..."
  key combinations read as "Ctrl+..." on Windows and Linux (the
  underlying shortcut-matching logic already handled this correctly
  via wx's own `CmdDown()`; only the on-disk file's text was
  Mac-flavored). Honestly: none of this has been build-tested on an
  actual Windows machine, since none was available during development
  -- the `SleepPreventer.cpp` Windows code path was verified with a
  real MinGW-w64 cross-compiler (compiles cleanly, zero warnings, on
  actual Windows API headers, not just reviewed by eye), which is a
  genuine check but a narrower one than this project's usual "built and
  tested every round." Tablet compatibility specifically wasn't
  addressed beyond this -- Windows tablets run the same OS Windows
  builds already target, but touch-specific UI sizing/interaction
  wasn't reviewed or changed.
- **Preparing for official OpenCPN catalog launch**: see the dedicated
  section above for what's in place (metadata.xml, the Preferences
  dialog, the packaging script, `test_harness/` exclusion) and what a
  real submission would still need (multi-platform builds, a hosted
  tarball URL, a real website, following OpenCPN's actual submission
  process, and an ongoing versioning scheme).

- **Prevents the computer from idle-sleeping while this plugin is
  loaded** (`SleepPreventer.h`/`.cpp`), per direct request -- a laptop
  running a survey shouldn't nod off partway through and silently stop
  recording GPS fixes/track points. Implemented with macOS's IOKit
  power-management assertions (`IOPMAssertionCreateWithName`), the same
  mechanism behind the `caffeinate` command-line tool; only prevents
  *idle* sleep (the normal "no activity for N minutes" timeout), not an
  explicit user action like closing the lid or choosing Sleep from the
  Apple menu, which is the correct behavior for this kind of assertion.
  No-op on every other platform (compiles and does nothing, rather than
  assuming macOS unconditionally) -- can't be verified end-to-end in
  this development environment (no way to test IOKit power assertions
  outside of actual macOS), so this is implemented carefully and
  reviewed against Apple's own documented API contract, but hasn't been
  watched-working the way most other fixes in this project have been.
  Held for as long as the plugin is loaded (`Init()` to `DeInit()`), not
  scoped to just an active survey, so it doesn't depend on remembering
  to do anything extra.
- **Tracking's definition (Settings tab) now leads with "Record the
  vessel position at a given interval and display on the map"** per
  direct request, with the previous, more implementation-detail-heavy
  wording (internal vs. OpenCPN's native tracking, when it turns on
  automatically) kept as follow-up context after that.

- **"Status" label removed** (switched from a `wxStaticBoxSizer` to a
  plain `wxBoxSizer` for the status fields), reclaiming the vertical
  space its title text and border padding used, per direct request.
- **Column definition display moved to its own row**, above the View/
  Start New Survey/etc. buttons, with wrapping applied and re-applied
  on every resize (the same treatment the status bar's own wrap sizer
  already needed) -- a shared row meant a long definition either got
  clipped or squeezed the buttons for space, especially in Split
  Vertical. `LogWindow::m_columnDefRawText` tracks the unwrapped
  original text separately from what's currently displayed, since
  `wxStaticText::Wrap()` rewrites the label's own text in place with
  embedded newlines, and re-wrapping already-wrapped text on a second
  resize would fragment it further each time rather than re-wrapping
  cleanly from the original. Verified live: a long DistUnit definition
  correctly wraps to multiple lines in a narrow (400px) window rather
  than overflowing.
- **"Load Survey...":** lets you switch to a different, already-
  existing survey (by its `<date>_<survey>` prefix, discovered by
  scanning this plugin's data folder for `*_sightings.csv` files), or
  browse to an external folder -- files for the chosen survey are
  copied into this plugin's own data directory first, so it's then
  modified in place like any other survey, not left pointing at the
  external copy. Tracking is turned off on load (with an explanation),
  so new GPS fixes don't silently start appending to what's likely a
  historical survey's track -- has to be deliberately turned back on
  (Settings tab) to resume recording. `DataTab::LoadSurvey()` (distinct
  from `StartNewFile()`, which always starts empty) reuses the same
  `LoadCsv()` used at startup, so a survey that already has data for a
  given tab loads it, and one that doesn't (e.g. no Events were ever
  logged) just starts that tab empty, the same as a brand new survey
  would; a schema mismatch is handled the same way already established
  elsewhere: backed up under its own name rather than silently dropped
  or corrupted. `TrackRecorder::StartNewFile()` needed no changes to
  support this, since it already correctly loaded an existing track
  file's points (for resuming a survey after a restart). Originally
  built when survey prefixes still had a separate vessel name baked in
  (`<date>_<survey>_<vessel>`, making the survey name alone
  unrecoverable from the prefix, so the *whole* prefix had to stand in
  for it); that ambiguity is gone now that the vessel name has been
  dropped entirely (see below), so loading a survey now correctly
  recovers and displays its actual survey name
  (`LogWindow::ParseSurveyNameFromPrefix()`). Verified with a dedicated
  test: created a second survey, confirmed both are discovered by
  `FindSurveyPrefixesInDir()`, confirmed `LoadSurvey()` correctly
  switches between each one's actual data (not just its row count), and
  confirmed the survey name is correctly recovered from a few different
  prefix shapes.
- **"Status" label/border removed** (a plain `wxBoxSizer` instead of a
  `wxStaticBoxSizer`) to reclaim the vertical space its title and
  border padding used, per direct request.
- **Column definition display moved to its own row**, above the View/
  Start New Survey/etc. buttons, with wrapping enabled and re-applied
  on every window resize -- a shared row meant a long definition either
  got clipped or squeezed the buttons for space, especially in Split
  Vertical. Implementation note for future changes to this: `wxStaticText
  ::Wrap()` rewrites the label's own text in place with embedded
  newlines, so re-wrapping on every resize has to work from a
  separately-stored raw (unwrapped) copy of the text
  (`LogWindow::m_columnDefRawText`) -- wrapping already-wrapped text
  would fragment it further with each resize.
- **Vessel Name dropped, leaving a single Survey Name** (shown in the
  status bar, prompted by "Start New Survey...", and used as the whole
  non-date portion of the file prefix) per direct request -- mainly to
  make "Load Survey..." (above) smoother, since the old
  `<date>_<survey>_<vessel>` scheme couldn't be split back into its
  three parts once joined (spaces become underscores when a prefix is
  built, indistinguishable from an original underscore in either name).
  `current_survey.txt`'s on-disk format also changed (two lines --
  survey, then prefix -- instead of three); reading it stays backward-
  compatible with a file saved by an older version of this plugin,
  distinguishing the two formats by their line count specifically.

- **All keyboard shortcuts breaking after switching to the Settings tab
  (until clicking some other tab) was a real, confirmed bug, now fixed
  and verified live end-to-end.** Root cause: unlike the grid-based
  tabs (Sightings/Effort/Events), where the wxGrid naturally receives
  keyboard focus, Settings is just a wxScrolledWindow full of assorted
  controls (checkboxes, spin controls, links) with no single "main"
  control that automatically grabs focus when the tab is switched to.
  So after switching to it, `wxWindow::FindFocus()` could return null --
  which `OnCharHook`'s very first guard (added in an earlier round for
  an unrelated "+' stopped zooming the chart" fix) treats as "this
  keystroke isn't meant for this window at all" and rejects
  unconditionally, breaking every shortcut until clicking some other
  tab gave a different control focus. Fixed by explicitly focusing the
  newly-selected page both in `OnPageChanged` and directly in the
  Cmd+1-5 handler (`wxNotebook::SetSelection()` isn't guaranteed to
  reliably fire `wxEVT_NOTEBOOK_PAGE_CHANGED`, where the same fix also
  lives, on every platform -- belt and suspenders). Verified live by
  simulating the exact reported sequence: switch to Settings via a
  synthetic Cmd+5, confirm `FindFocus()` now returns a window that's
  actually part of this LogWindow, then simulate Cmd+1 and confirm it
  correctly switches back to Sightings.

- **Sightings Breakdown NA logic was incomplete, not just missing a
  label.** A species where not a single contributing sighting had a
  known NumCalf (or Num) was showing "0 calves" -- technically the sum
  of nothing, but indistinguishable from "confirmed zero calves
  observed," which isn't the same claim at all. Fixed by tracking how
  many sightings actually had a known value
  (`SurveySummary::SpeciesRow::knownIndividualsCount`/
  `knownCalvesCount`), so the three genuinely different cases are shown
  differently: zero known values at all → "NA calves" (no number
  shown); some known and some missing → "3 calves (+ NA)" (a floor, not
  a confirmed total); all known → plain "3 calves". Verified with a
  dedicated test using a species with a single sighting and a
  deliberately blank NumCalf.
- **Tracking/Effort reminder interval controls**: also applied (and any
  lingering text-selection highlight cleared) on losing focus, not just
  via the spin arrows -- reported as unclear whether a typed-in value
  had taken effect, and that the field couldn't be "deselected."
  `wxEVT_SPINCTRL` isn't guaranteed to fire for every way a value can
  change on every platform; re-applying `GetValue()` on focus-loss
  means clicking or tabbing away always commits whatever's currently
  shown. Pairs with the status-bar change below, which now gives a
  direct, visible way to confirm the current tracking interval and
  point count are what's expected.
- **Tracking status bar restored to include point count and interval**
  (an earlier round had simplified it to bare "ON"/"OFF"): now reads
  "ON (614 pts, 30s interval)" per direct request, with the ON=green/
  OFF=red color-coding adjusted to check the text's prefix rather than
  requiring an exact "ON"/"OFF" match.
- **A mistake made and caught during this round's own editing, not
  shipped:** while reorganizing `LogWindow.h` for the above, an edit
  accidentally flipped `CurrentEffortStatus()`/`CurrentEffortSegNo()`
  from public to private, breaking the build for
  `spotter_pi.cpp` (which calls them from a different class).
  Caught immediately by the build step that runs after every change in
  this project, before it ever reached the test suite or a packaged
  zip -- noting it here mainly because it's a good illustration of why
  that build-after-every-change discipline matters, not because
  anything shipped broken.

- **Dropdown navigation skipping/losing options for values that are a
  prefix of a longer valid option (Beaufort's "1" vs "10"/"11"/"12";
  observers like "MD" vs "MDM", "KDM" vs "KDMR"; same in Species) was a
  real bug in this plugin's own autocomplete logic, now fixed and
  confirmed live.** `SearchableChoiceGridCellEditor::OnText` suggests a
  completion whenever the current text is a prefix of a *longer*
  choice -- but it wasn't checking whether the current text was
  *already* a complete, valid choice on its own first. So landing
  exactly on "1" (arrow-key navigation or typing) immediately got
  "completed" into "10" (the first "1"-prefixed choice found), making
  "1" itself unreachable/unselectable, and the same for any other
  value that happens to double as a prefix of something longer. Fixed
  by skipping the suggestion entirely whenever the current text already
  exactly matches a choice -- there's nothing left to complete in that
  case. Verified live: typing "1" into a Beaufort cell now stays
  exactly "1", with no completion/selection applied, and typing a full,
  exact species name still works normally.
- **Summary tab's Sightings Breakdown now includes every sighting
  regardless of SpecConf/NumConf** (an earlier round had restricted it
  to Probable/Definite only; reversed per direct request), and **a
  blank Num or NumCalf is now treated as genuinely unknown, not
  silently added as zero** -- `SurveySummary::SpeciesRow` gained
  `hasUnknownIndividuals`/`hasUnknownCalves` flags, set whenever at
  least one contributing sighting had a blank value; the displayed and
  exported totals get a "(+ NA)" suffix in that case, so a total is
  never presented as more complete/precise than the underlying data
  actually supports. Verified with a dedicated test using a row with
  deliberately blank Num/NumCalf alongside two rows with values,
  confirming the sum only reflects the known values and the NA flag is
  set and surfaced in the export.

- **Mouse wheel not scrolling the Summary tab while hovering over
  either table -- two prior attempts (forwarding the wheel event, then
  explicitly computing/applying the scroll) didn't resolve it, so the
  tables were removed entirely.** `wxListCtrl` natively captures
  `wxEVT_MOUSEWHEEL` for its own possible internal scrolling and
  doesn't let it propagate to its parent; rather than keep fighting
  that from outside, the Sightings/Events breakdowns are now rendered
  as plain text (`LogWindow::RefreshSummaryTab()`) alongside the rest
  of the tab's figures, in a single `wxStaticText`. A `wxStaticText`
  has no scrolling behavior of its own to compete with the panel's, so
  this removes the problem at its root rather than working around it.
  Per direct request, formatted as e.g. "Right whale: 3 sightings, 3
  individuals, 0 calves" and "CTD cast: 3", with correct singular/
  plural wording (Plural() in LogWindow.cpp).

- **Dropdown Enter-key handling: reverted a change that broke arrow-key
  navigation, back to the simpler behavior that worked correctly.** An
  attempt at fixing "Enter needs two presses to select-and-close" by
  binding `wxEVT_COMBOBOX` (instead of the existing `wxEVT_KEY_DOWN`-
  based `Dismiss()` call) broke something more important: arrow-key
  navigation through the popup stopped working at all, immediately
  closing the list on every up/down press. Root cause: `wxEVT_COMBOBOX`
  fires on *every* highlight change while arrow-keying through an open
  popup, not just on a final Enter-to-commit -- so every arrow press
  was immediately dismissing the popup. Reverted per direct request
  (arrow-key navigation matters more than avoiding a second Enter
  press) -- back to just the `wxEVT_KEY_DOWN`-based, `CallAfter()`-
  deferred `Dismiss()` on Enter/Tab. The double-Enter-to-close behavior
  remains as a known, accepted minor annoyance; further attempts at
  fixing it should be tested very carefully against arrow-key
  navigation specifically, since that's what broke last time.
- **Summary tab**: "Species Breakdown" renamed to "Sightings Breakdown"
  per direct request. New "Events Breakdown" section right after it --
  a tally of how many times each Event type has occurred, computed the
  same way as the species breakdown (fresh from events.csv on disk) and
  included in both the tab's display and `<prefix>_summary.csv`.
  Verified with a dedicated test using controlled Event values.

- **The dropdown autocomplete's alternating-character bug is fixed, and
  the root cause was genuinely subtle.** Reported pattern: typing "K"
  suggested "Killer whale," but "Ki" showed no suggestion, "Kil" showed
  one again, alternating with every keystroke. Cause: whether the user
  was "deleting" (and should skip suggesting) was inferred by comparing
  the new text's length to the previous text's length -- but typing a
  character while an inline suggestion's remainder is selected *also*
  shortens the text (the keystroke replaces the whole selection), which
  that heuristic wrongly read as deletion. Fixed by tracking whether the
  actual key pressed was Backspace/Delete (`SearchableChoiceGridCellEditor
  ::OnComboKeyDown`) instead of inferring it from length. Verified live:
  typing "K", "i", "l", "l" in sequence now suggests "Killer whale
  (Orca)" after every single keystroke, not every other one.
- **Behaviors dialog hint text** updated to the exact wording requested.
- **Nautical miles traveled since the previous point** (`nm_since_prev`)
  added as a new column to both the exported track file and the merged
  CSV -- computed at export time (`TrackRecorder::ExportCopyTo()`, via
  the plugin API's `DistanceBearingMercator_Plugin()`) rather than
  recorded live into track.csv as each fix comes in, since it's just as
  easy to compute in one pass over what's already on disk, without
  changing the format of an already-append-only, crash-safe-by-design
  file. `ExportMergedCsv()` reads the already-exported (enhanced)
  track file rather than the raw source, since `ExportCopyTo()` always
  runs first in the real export flow and already has the column.
- **New Summary tab** (between Events and Settings): trackline and
  on-effort nm/time, sightings/species counts, a bounding box of all
  recorded positions (track + Sightings + Effort + Events), visibility/
  Beaufort range, unique weather conditions, and a species-breakdown
  table (sightings/individuals/calves) restricted to rows with both
  SpecConf and NumConf set to Probable or Definite. Recomputed fresh
  from the CSVs on disk (the same pattern as the merged CSV/GPX
  exports) every time the tab becomes visible, rather than kept live-
  updated in the background. Also exported as `<prefix>_summary.csv`
  alongside the other files. The Probable/Definite filtering was
  verified with a dedicated test using deliberately mixed-confidence
  rows, confirming both that qualifying rows are correctly included and
  that a non-qualifying row's inflated individual count doesn't leak
  into the total.
- **Cmd+1 through Cmd+5** now cover all five tabs (Sightings/Effort/
  Events/Summary/Settings) -- Summary added at Cmd+4, Settings moved
  from Cmd+4 to Cmd+5.

- **A real, confirmed bug in the column-definition display was found and
  fixed via direct testing.** `wxEVT_GRID_RANGE_SELECTED` (bound
  alongside `wxEVT_GRID_SELECT_CELL` to update the column-definition
  text at the bottom of the window) fires a spurious event reporting
  column 0 as part of perfectly normal single-cell selection -- e.g.
  right as editing starts -- which incorrectly reset the displayed
  definition to column 0's, regardless of which cell was actually
  selected. Reproduced directly (selecting a cell, then checking what
  the RANGE_SELECTED handler reported) before removing it; confirmed
  fixed by rerunning the same reproduction afterward.
- **Enter/typing a character not starting cell editing: a genuine,
  confirmed-but-only-partially-fixed problem.** Direct testing (driving
  the grid with real synthetic keyboard events, not just calling
  `EnableCellEditControl()` programmatically) confirmed that wxGrid's
  supposed native "typing or Enter starts editing" behavior does not
  reliably trigger in this development environment -- only F2 reliably
  opens an editor on its own; Enter and plain letter keys do nothing at
  all. Two explicit fixes were attempted (`DataTab::OnGridKeyDown`
  calling `EnableCellEditControl()` directly, then a second attempt at
  the `LogWindow` CHAR_HOOK level, both including an explicit
  `SetFocus()`/`SetGridCursor()` call first) -- **neither was confirmed
  working in this environment's automated testing**, even though the
  RANGE_SELECTED fix above, tested the same way, clearly did take
  effect. The CHAR_HOOK-level attempt is left in place since it's
  reasoned through carefully and doesn't regress anything, and real
  keyboard input on macOS may behave differently than this
  environment's synthetic-event testing -- but this should be verified
  directly rather than taken on faith. **F2 is confirmed, reliably
  working** for starting an edit while preserving existing content, in
  both this testing and via double-click, if Enter continues not to
  work.
- **The Behaviors (MULTI_CHOICE) dialog's interaction model was revised**
  per direct feedback: Enter (not Space) toggles the highlighted
  behavior, works the same whether focus is in the filter box or (after
  Tab) the list itself, and Cmd+Enter saves and closes -- bound at the
  dialog level via CHAR_HOOK so it works regardless of which control has
  focus. The hint text at the top of the dialog now explains this,
  including that Tab moves focus to the list for arrow-key browsing.
- **Split Horizontal not auto-applying on launch, and row heights
  collapsing on reopen** both got the same class of fix: deferring the
  relevant call via `CallAfter()` so it runs after this window has
  actually been shown/laid out at least once, not immediately during
  construction. Same reasoning as an earlier round's row-height fix,
  now also applied to the initial layout-preset application.
- **White label backgrounds removed from Sightings/Events chart
  markers**, per direct request. The DC (non-OpenGL) path just had a
  filled rectangle removed. The OpenGL path was more involved: labels
  are rendered to an offscreen bitmap and drawn as a textured quad, and
  that texture had no alpha channel at all (solid RGB, opaque white
  background baked in) -- switched to rendering onto a chroma-key color
  (pure magenta, unlikely to collide with black text or realistic chart
  colors) and computing per-pixel alpha from distance to that color when
  building the texture, so the background renders transparent instead
  of as an opaque box, with reasonably smooth (not jagged) edges around
  the text. Verified visually, not just by code review: rendered a
  label over a solid-color background and confirmed the background
  shows through with no white box.
- **Sortable CHOICE columns now sort numerically when every option is a
  plain number** (Beaufort: 0-12), rather than always alphabetically --
  a plain string sort put "10" and "11" before "2" through "9". Falls
  back to alphabetical for anything that isn't purely numeric (species,
  behaviors, etc.), which is everything else.
- Events tab's Event column now defaults to blank on a new row, the
  same reasoning Sightings already uses for Species and other columns
  where auto-filling the first dropdown option would misrepresent an
  not-yet-classified row.

- **Row heights collapsing on reopen: the previous fix only caught
  whichever tab happened to be visible first.** A one-time, deferred
  `AutoSizeRows()` call in `LogWindow`'s constructor fixed the initially
  -selected tab, but every *other* tab's grid was still being measured
  while hidden (confirmed elsewhere in this codebase already, for
  column widths: "hidden tabs can report a stale, tiny client size").
  Fixed by also calling `DataTab::ReapplyRowHeights()` from
  `OnPageChanged()` (so every tab gets correctly measured the first
  time it's actually shown, not just whichever one happened to be
  selected at startup) and from the window-resize debounce timer (so a
  column getting narrower, which can wrap more text onto more lines,
  doesn't leave a row too short for what it now contains).
- **Undo/redo fail silently** when there's nothing to undo/redo -- the
  informational popup was removed per direct request.
- **The Behaviors (and any other MULTI_CHOICE column's) selection
  dialog was rebuilt from scratch**, replacing the stock
  `wxMultiChoiceDialog` with a custom `MultiSelectSearchDialog`
  (`DataTab.cpp`). Reported problems: the dialog wouldn't reliably open
  via the keyboard (double-click was the only reliable path), and once
  open, there was no way to toggle a behavior on/off without a mouse.
  The new dialog has a filter text box (type to narrow the list, the
  same searchable-dropdown convention as regular CHOICE columns) above
  a real `wxCheckListBox` (native Space-to-toggle and arrow-key
  navigation once it has focus); Enter in the filter box also toggles
  whichever item is currently highlighted, so "type enough to narrow to
  one match, hit Enter" works without ever needing to leave the filter
  box or reach for the mouse. Checked state is tracked independently of
  what's currently visible, so filtering the list down and back up
  never loses a selection that's temporarily out of view. Also widened
  the CHAR_HOOK-level keyboard-opening backup (see below) to cover
  Enter/F2 in addition to Space, on the theory that whatever was
  intercepting the keyboard-open path might not have been specific to
  Space. Verified live end-to-end: opening the dialog via the keyboard
  path, filtering to "Traveling", toggling it via Enter, clearing the
  filter and confirming both the full list and the checked state are
  intact, then confirming the saved cell value.
- **Regular (single-select) dropdowns: two more fixes.** The popup now
  closes on Enter, matching what already happened on a mouse click --
  wxGrid's own Enter handling commits the cell edit but doesn't itself
  tell the combo's popup to dismiss if it's still open, so this is done
  explicitly (`SearchableChoiceGridCellEditor::OnComboKeyDown`, calling
  `wxComboBox::Dismiss()`). Every CHOICE column's options are now
  sorted alphabetically (`SearchableChoiceGridCellEditor::SortedCopy()`)
  -- both for the popup itself and internally for the inline-
  autocomplete suggestion -- so options sharing a first letter sit
  together, which also makes native OS letter-jump navigation (if used)
  more predictable, though that native behavior otherwise remains
  outside this plugin's direct control.
- **A reported "jumps over Photos" issue in the Img dropdown likely
  isn't something this plugin's own code can fix directly.** This
  plugin's inline-autocomplete suggestion (typing into the text field)
  is unaffected by this and should work as normal; the reported
  behavior sounds specific to the native OS popup list's own built-in
  letter-jump navigation (used when arrow-keying through an *open*
  popup rather than typing into the text field), which isn't something
  wx exposes a way to override or customize. Alphabetical sorting
  (above) may incidentally help, since same-letter options are now
  adjacent rather than scattered. If this is still a problem, typing
  directly into the cell (rather than opening the popup and arrowing
  through it) should reliably sidestep it, since that path is this
  plugin's own code, not the native listbox's.
- **"No" renamed to "None" in the Img column**, per direct request.

- **Row heights collapsing for existing rows after reopening the
  plugin was a real, confirmed bug, now fixed.** `SetGridFontSize()`'s
  `AutoSizeRows()` call (part of the per-tab setup that runs while this
  window is being constructed) could end up measuring row heights
  against a not-yet-real grid size, since it runs before the window has
  ever actually been shown/laid out -- rows loaded from an existing CSV
  got sized against that not-yet-real measurement and then never
  re-measured later; brand new rows added during the same session
  didn't have this problem, since by the time you're adding a row
  interactively the window is already showing with a real size. Fixed
  by re-running `AutoSizeRows()` again (`DataTab::ReapplyRowHeights()`)
  deferred via `CallAfter()` in `LogWindow`'s constructor, so it runs on
  a later idle cycle after the window has genuinely been shown at least
  once. Manual row-height dragging was also enabled
  (`EnableDragRowSize(true)`, previously disabled for no recorded
  reason) as a fallback/usability feature, not the primary fix.
- **The exported track file wasn't getting the survey prefix --
  confirmed as a real bug via a direct test, not assumed.**
  `TrackRecorder`'s own current file (`GetCsvPath()`) was already
  correctly prefixed after Start New Survey; the actual problem was in
  `ExportCopyTo()`, which used a raw `wxCopyFile()` call that silently
  does nothing if the destination directory doesn't already exist --
  unlike each `DataTab::ExportCopyTo()`, which (via `CsvUtils::WriteAll()`)
  creates the destination directory if needed. Fixed by adding the same
  directory-creation step to `TrackRecorder::ExportCopyTo()`.
- **Undo/redo are now unlimited stacks**, not single-level -- repeatable
  back and forth through a whole editing session, the same convention
  as most other applications' undo/redo. Also moved out of hardcoded
  keyboard shortcuts and into `shortcuts.csv` as ordinary, remappable
  actions ("Undo"/"Redo", defaulting to Cmd+Shift+U/Cmd+Shift+Y) per
  direct request, so a default combination that doesn't work well on a
  given machine can be changed without a code change. The "Undo"/"Redo"
  toolbar buttons remain as a keyboard-independent fallback either way.
- **Searchable dropdowns were redesigned** after two related problems
  were reported: arrow keys not navigating the option list (defaulting
  to the first option instead), and the full option list not
  reappearing once a cell already held a value (only visible again
  after deleting the text first). Root cause: the previous
  implementation filtered the dropdown's own item list on every
  keystroke (`Clear()` + `Append()` with only the matches), which reset
  the list's internal navigation state and meant a cell that already
  held an exact match showed a "list" of just that one item. Redesigned
  around inline autocomplete-as-you-type instead (browser-address-bar
  style: the first matching choice is suggested by appending the rest
  of it to the text field with that appended part shown selected, so
  the next keystroke either narrows the typed prefix or, if it's
  correct, Enter accepts it immediately) -- the dropdown's item list
  itself is never touched, so arrow keys always navigate the complete,
  unmodified list (native wx/OS behavior, nothing extra needed), and
  reopening any cell always shows the full list regardless of its
  current content. Verified live, not just by code review: typing "H"
  into a 12-option Species list correctly suggests "Humpback whale"
  with "umpback whale" shown as a selected/highlighted suggestion,
  while the list itself stays at all 12 options throughout, including
  after reopening a cell that already contains a committed value.
- **Cmd+Shift+R still not adding "Photos" to Img, despite an earlier
  round already changing the *default* shortcut to include it.**
  `shortcuts.csv` is only ever created once, on first run -- an
  already-existing file from an earlier round never picks up later
  changes to what the *default* would have been, the same way none of
  this plugin's other config files (species.csv, display.csv, etc.)
  get silently overwritten with new defaults on an upgrade. The fix
  here is the same as it would be for any of those: edit the line
  directly, or see the fully up-to-date shortcuts.csv reference in the
  chat response accompanying this round.
- **Cmd+1/2/3/4 jump directly to a specific tab** (Sightings/Effort/
  Events/Settings) -- a fixed, built-in shortcut, not configurable via
  shortcuts.csv (unlike Undo/Redo above). Worth flagging: this
  plugin's own `shortcuts.csv` comments already warned that Cmd+1
  through Cmd+9 are frequently reserved by macOS itself or by the
  containing app's own menu for window/tab switching, which can
  silently swallow the keystroke before it ever reaches this plugin --
  implemented exactly as requested, but if these don't work reliably,
  that reservation is the likely reason, and there's no config-file
  workaround for a *built-in* shortcut the way there is for Undo/Redo.
- **The Sightings/Events map labels are both configurable now**
  (Settings tab's Map section), Events defaulting to Event + ID,
  alongside Sightings' existing Species + FieldID default.
- **`wxGetActiveWindow()` guard removal (from last round) seems to have
  worked** -- new-row/tab-cycling shortcuts are confirmed working
  again. Still can't be independently re-verified in this development
  environment (no way to reproduce macOS's specific window-activation
  semantics for a plugin hosted inside another app's process), so this
  is based on direct confirmation rather than something provable here.

- **All keyboard shortcuts breaking was a real regression, and the
  likely cause has been identified and reverted.** An earlier round
  added a second guard in the CHAR_HOOK handler
  (`wxGetActiveWindow() != this`), on top of the existing `FindFocus()`
  check, as a second attempt at a reported "+' stopped zooming the
  chart" issue -- flagged at the time as speculative and never actually
  confirmed to fix anything. Given every shortcut in this plugin
  (new-row shortcuts, tab cycling, undo) broke at once, this guard is
  the prime suspect: in a plugin hosted inside another application's
  process (OpenCPN's), `wxGetActiveWindow()` evidently doesn't reliably
  agree that a focused floating/utility window like this one counts as
  "active," even when it genuinely has keyboard focus. Removed, keeping
  just the `FindFocus()` check. This could only be reasoned through, not
  reproduced locally -- this development environment can't replicate
  macOS's specific window-activation semantics for a plugin window
  hosted inside another app.
- **Searchable/type-to-filter dropdowns, and why they're not built on
  `wxTextEntry::AutoComplete()`.** That API would have been a one-line
  fix, but its support is inconsistent across platforms and wx
  versions, with known gaps on macOS specifically -- exactly the
  platform this plugin needs to work reliably on, and exactly the kind
  of platform-specific gap that's caused real problems elsewhere in
  this project. Instead, `SearchableChoiceGridCellEditor`
  (`DataTab.cpp`) manually re-populates the dropdown's own item list on
  every keystroke (`Clear()` + `Append()` with whatever currently
  matches), using only portable, always-available `wxComboBox` methods
  -- more code, but not dependent on a platform-specific feature
  actually working. Verified live (not just by code review): typing
  "hump" into a Species cell with 12 choices correctly narrows the
  dropdown to just "Humpback whale", with the typed text preserved.
  Typed text that doesn't exactly match one of the valid choices is
  flagged with a confirmation before being saved (same pattern as the
  lat/lon editor's unparseable-text warning), so the search-to-narrow
  workflow doesn't come at the cost of the original dropdown's
  data-integrity guarantee.
- **Redo joins undo as a second, independent single-level snapshot.**
  `DataTab::Undo()` now also captures what it's about to overwrite (into
  a separate redo snapshot) before restoring the previous state, and
  `Redo()` restores that -- both single-level (calling either twice in a
  row only does something the first time), and making any new edit
  clears the redo snapshot, the same convention a spreadsheet or text
  editor would follow. Both got a toolbar button (pushed to the right
  side of each tab's toolbar row via a stretch spacer) and a keyboard
  shortcut: Cmd+Shift+Backspace for undo, Cmd+Shift+Delete for redo.
  Cmd+Shift+Z (the more conventional macOS "redo") was deliberately
  avoided for redo, for the same reason plain Cmd+Z was avoided for
  undo in an earlier round -- native text-editing controls' own Cocoa
  undo managers commonly recognize both.
- **The Sightings map label is now configurable** (Settings tab's new
  Map section: checkboxes for SightNo/Species/SpecConf/FieldID/Num/
  NumCalf, space-separated in the order checked), defaulting to Species
  + FieldID. Implemented via a new `DataTabConfig::labelTextFn` hook
  (`DataTab.h`) rather than hardcoding multi-column support into the
  existing single-column `overlayTextCol` -- it's re-evaluated fresh
  for every charted point, so changing the setting takes effect
  immediately without rebuilding the tab.
- **Reticle distance now genuinely produces NAN before any observer
  height is known**, rather than silently falling back to the vessel's
  own position. `DataTab::m_observerHeightFt` defaults to
  `std::numeric_limits<double>::quiet_NaN()` (was 20.0); NAN propagates
  cleanly through the trig (no crash), but the position-calculation
  code explicitly checks for it and writes literal "nan" into SigLat/
  SigLon rather than letting `distNm > 0.0` evaluate false (which NaN
  comparisons always do) and quietly reusing the vessel's own lat/lon
  as if that were a real answer.
- **GPX export** (`LogWindow::ExportGpxLayer()`, `<prefix>_layer.gpx`
  alongside the other exports): the trackline as a `<trk>`, Sightings as
  `<wpt>` waypoints (named from Species + FieldID, falling back to
  "Sighting <SightNo>"), meant to be loaded into OpenCPN as a layer --
  e.g. to overlay a previous survey day's data while looking at today's.
  GPX `<time>` elements are converted from this plugin's local-time
  strings to UTC via `wxDateTime::ToUTC()`, which assumes the timezone
  the data was recorded in matches the one this plugin is currently
  running in -- fine for the common case, potentially off for much
  older data from a different timezone (not otherwise recoverable from
  a bare "EST"-style abbreviation).
- **Output file naming**: survey-prefixed files now use `YYYY-MM-DD_`
  (was `YYYYMMDD_`), and the merged CSV was renamed from a fixed
  `combined_by_timestamp.csv` to `<prefix>_merged.csv`, following the
  same `<date>_<survey>_<vessel>_<type>.csv` convention as every other
  export instead of being the one inconsistent filename.

- **Track and effort segments not appearing on the chart after closing
  and reopening the log window was a real, confirmed bug, now fixed.**
  `DrawOverlayDC()`/`DrawOverlayGL()` used to bail out immediately if
  `m_logWindow` was null -- which it genuinely is between closing the
  log window (its own close button sets it back to null) and reopening
  it, not just before the very first open. That early return sat
  *above* the trackline/effort-segment drawing, which only actually
  needs `m_trackRecorder` (alive for as long as the plugin itself is),
  not the log window at all -- only the marker-drawing further down
  genuinely needs it. Fixed by moving the `m_logWindow` check to just
  before the marker section, so the trackline and effort segments now
  draw regardless of whether the log window happens to be open.
  Verified live by reproducing the exact scenario (record some track
  points, close the window, call `RenderGLOverlay()`, confirm it
  succeeds and still has the track data available) rather than just by
  code review, given this touches the same rendering code that's had
  real, confirmed bugs before. The GL path also has a
  `glPushAttrib`/`glPopAttrib` pair that has to stay balanced even on
  this early exit -- worth knowing if this code gets touched again.
- **The "reticles" DistUnit now uses the observer height that was in
  effect when each row was *created*, not whatever's currently live.**
  An earlier version read `DataTab::GetObserverHeightFt()` directly at
  recompute time, which meant editing Dist on an old row well after the
  observer had since moved to a different Position would silently use
  today's height instead of the height that was actually in effect at
  the time of that sighting. Fixed by capturing the height into a new
  hidden, trailing `ObsHeightFt` column at row-creation time
  (`DataTab::OnRowAdded()`), and having the reticle calculation read
  that stored value instead of the live one (falling back to the live
  value only for rows logged before this column existed). Currently
  only wired up for Sightings -- Surfacings is disabled for now (see
  below), so it doesn't need this yet, but the same column should be
  added there whenever it comes back.
- **The undo keyboard shortcut changed from Cmd+Z to
  Cmd+Shift+Backspace,** reported as not working. Cmd+Z is macOS's
  universal, system-wide undo convention -- if a native text-editing
  control (very plausibly a grid cell's in-place editor, right after
  finishing an edit, the most natural moment to reach for undo) has
  focus, its own built-in Cocoa undo manager can intercept Cmd+Z before
  this plugin's CHAR_HOOK handler, or wx's event system generally, ever
  sees it. Same category of problem as the earlier "+' stopped zooming
  the chart" and Cmd+Shift+Left/Right issues -- a shortcut collided
  with something else that got to it first. An "Undo" button was also
  added next to "Delete Selected" on every tab, so undo doesn't depend
  on any keyboard shortcut working at all.
- **Spacebar for opening the behaviors multi-select dialog now has a
  backup path.** The primary path (`DataTab::OnGridKeyDown`, bound
  directly on the grid) is unchanged and should already work; a second
  binding was added at the top-level CHAR_HOOK handler
  (`LogWindow::OnCharHook`) as a fallback, in case something between
  that handler and the grid's own key processing was intercepting a
  bare Space first. Harmless to have both -- whichever fires first
  consumes the event.
- **The Surfacings tab is disabled for now**, pending more thought
  about how it should actually relate to Sightings data -- the "every
  new Sightings row auto-creates a linked Surfacings row, sharing a
  few columns" design (see the two rounds of back-and-forth already
  documented elsewhere in this file) didn't hold up well once
  attempted. The tab's setup code, and the Sightings→Surfacings
  auto-creation wiring, are both kept intact but wrapped in `#if 0` in
  `LogWindow::BuildTabs()` rather than deleted, specifically so this is
  easy to revisit later without starting over. `m_surfacing` stays
  null; everything else in the codebase that touches it already
  null-checks (drawing, exporting, marker controls), so nothing else
  needed to change to disable it here.
- **"Export Data..." now also writes `combined_by_timestamp.csv`**
  (`LogWindow::ExportMergedCsv()`): every Sightings row, every Effort
  row, and every recorded track point, merged into one file and sorted
  chronologically. Column names are prefixed by source
  (`Sightings_SightNo`, `Effort_SegNo`, `Track_lat`, etc.) since the
  sources have overlapping raw names, and most rows will have only the
  `Track_*` columns filled in, since track points are recorded far more
  often than Sightings/Effort rows are logged -- expected, not a bug.
  Timestamps are compared as plain `YYYY-MM-DD HH:MM:SS` text for
  sorting (correctly sortable as-is, since every source uses that same
  local-time format), ignoring each row's trailing timezone
  abbreviation, which isn't a reliably parseable fixed-width field.

- **The "reticles" DistUnit's constant has not been verified and should
  be checked before relying on it.** Distance is derived from the
  standard "dip of horizon" formula (the observer's eye height gives an
  angle down to the visible horizon) plus the reticle reading times a
  radians-per-reticle constant, then basic trigonometry
  (`distance = height / tan(total angle)`) -- the geometry itself is
  standard and should be correct. The radians-per-reticle constant
  (currently 5 mrad, in `RecomputeBearingDistancePosition()` in
  `LogWindow.cpp`) is instrument-specific and was *not* looked up
  against any particular reticle binoculars' actual specification --
  this development environment doesn't have web access to verify it.
  5 mrad/reticle is a commonly cited convention for reticle-scale marine
  binoculars, used as a reasonable default, but a wrong value here would
  systematically bias every reticle-based distance by the same factor.
  Worth confirming against whatever binoculars are actually in use, and
  changing the constant in that one function if it doesn't match --
  there's no user-facing setting for it yet.
- **Bearings are now magnetic-only; relative-bearing support (DegRel/
  ClockRel, and the BearUnit column) was removed.** An earlier version
  converted relative bearings to absolute using course-over-ground (COG)
  as a vessel-heading proxy, but COG can reflect current/leeway drift
  rather than the vessel's actual heading, with no real heading source
  available through the plugin API to use instead -- rather than keep a
  systematically-unreliable conversion, relative bearings were dropped
  entirely. `Bearing` was renamed to `BearingMag` to make the assumption
  explicit; still not corrected for magnetic variation (no declination
  source available), so it's the vessel's raw compass reading, used
  directly.
- **Effort's Position column feeds the reticles calculation via a
  1-second poll, not an event.** `LogWindow::OnStatusTick()` (already
  running every second for the status bar) reads the Effort tab's most
  recent Position entry, looks up its height in `positions.csv`
  (`PositionHeights`), and pushes it into Sightings/Surfacings via
  `DataTab::SetObserverHeightFt()`. A dedicated change-triggered
  mechanism was considered but a 1-second lag between changing Position
  and it taking effect isn't practically significant here, so the
  simpler polling approach (reusing an already-running timer) was used
  instead.
- **Effort's SegNo is entirely derived** (blank while Effort is OFF,
  incrementing only on an OFF→ON transition) -- see
  `RecomputeEffortSegNo()` in `LogWindow.cpp`.
- **Columns now persistently auto-size to their widest cell,** not just
  momentarily after each edit. An earlier version called
  `wxGrid::AutoSizeColumns()` after data changes, but a later window
  resize could still shrink columns back below their content width,
  since the proportional resize-to-window logic didn't know about
  content width at all. Fixed by tracking each column's content-based
  minimum width (`DataTab::UpdateContentMinWidths()`, refreshed after
  every add/edit/undo/load) and using it as an additional floor in
  `ResizeColumnsToFit()`, alongside the existing header-title floor.
- **A column-definitions system was added**: `column_definitions.csv`
  (linked from the Settings tab, editable the same way as
  species.csv/shortcuts.csv/display.csv) holds a short definition for
  every column on every tab, generated once from this plugin's own
  built-in defaults on first run. Selecting any cell shows that column's
  definition in italics between the View dropdown and "Start New
  Survey..." at the bottom of the window (`DataTab::on_cell_selected`,
  wired in `LogWindow::BuildTabs()`). Not regenerated on later runs even
  if columns change in a future version -- if a definition goes stale
  after an update, edit the file directly, the same as the other config
  files.
- **Testing approach going forward:** earlier rounds included a
  standalone test harness (`test_harness/`, `--selftest` mode) exercised
  extensively as part of verifying each change. Per direct feedback,
  that's no longer needed for user-facing testing (testing now happens
  directly in OpenCPN) -- the harness is kept in the repository and
  still gets a basic build+run check when convenient, but isn't being
  expanded with new test coverage for every change going forward the
  way it was in earlier rounds.

- **Sightings → Surfacings linking is a one-time copy at row-creation
  time, not an ongoing sync.** Every new Sightings row automatically
  creates a linked Surfacings row (replacing an earlier version's
  manually-triggered "+ Surf" button), sharing Time/BearingMag/Dist/
  DistUnit/SightNo -- but only with whatever those values are at the
  *instant* the Sightings row is added. In normal use, BearingMag/Dist
  are usually filled in on Sightings *after* adding the row, so the
  auto-created Surfacings row's copies will typically still be blank;
  they don't retroactively update if the Sightings row is edited
  afterward. A true two-way sync was considered but not implemented
  given the added complexity (bidirectional watching, avoiding update
  loops, handling deletions on either side) relative to how this
  actually gets used in practice -- if this turns out to matter, it's a
  distinct follow-up worth discussing rather than assumed.
- **Surfacings' vessel Lat/Lon are visible columns again** -- an
  intermediate version made them HIDDEN (still present, needed
  internally to derive SurfLat/SurfLon from BearingMag+Dist, just not
  shown), reverted back to visible per direct feedback. Still kept
  deliberately *trailing* in the column list regardless of visibility:
  an earlier round added a hidden Date column elsewhere and, by not
  keeping it trailing, silently broke every column lookup declared
  after it (`GenericGridTable::FindColByName()` returns a raw index;
  `wxGrid` expects a visible one; they only coincide when hidden
  columns are last). That Date column was later reverted, but the
  lesson (and the trailing-order convention) stuck.
- **Track and effort-segment display are both configurable from the
  Settings tab** (color swatch + show/hide checkbox each, under
  Tracking and Effort respectively) -- persisted via `display.csv`'s
  `track_color`/`track_visible`/`effort_segment_color`/
  `effort_segments_visible` keys. Effort segments are drawn as a second,
  distinctly-colored line directly over the trackline, covering only
  the consecutive runs of recorded points where Effort was ON (grouped
  by SegNo, so two separate on-effort periods don't visually merge into
  one line even if they retrace similar ground) -- this required
  extending `TrackRecorder::Point` to carry each point's effort status
  and SegNo alongside its position, and `RecordFix()` to accept and
  store them (sourced from `LogWindow::CurrentEffortStatus()`/
  `CurrentEffortSegNo()`, reading the Effort tab's most recent row).
  Verified against a real, current OpenGL context (not just the DC
  path), given this touches the same rendering code that had a real,
  confirmed crash in an earlier round.
- **track.csv is now included in "Export Data..."** -- an earlier
  version deliberately excluded it (documented at the time as "copy it
  directly if you need it"); reversed per direct request. It's also
  still created fresh per survey by "Start New Survey..." (unchanged
  from before) and now carries Effort/SegNo columns alongside the
  position data.

- **Lat/lon edits silently not saving was a real, confirmed bug --
  found and fixed via a live test that drives the actual grid editor,
  not just SetCellValue().** `wxGridCellTextEditor::ApplyEdit()`
  (unless overridden) sources the value it writes back from the text
  control directly (`Text()->GetValue()`), not from what `EndEdit()`
  computed into `*newval` -- so the custom lat/lon editor's parsed
  decimal-degrees value was being computed correctly and then silently
  discarded, with the base class writing the user's literal typed text
  back instead. Fixed by explicitly overriding `ApplyEdit()` to write
  the parsed value directly, removing any dependency on the base
  class's exact internal behavior. Verified by programmatically driving
  a real `wxGrid` editor (`EnableCellEditControl` →
  set the control's text → `SaveEditControlValue()`, the same sequence
  a real keystroke-driven edit goes through) and checking the resulting
  stored value, not just calling `SetCellValue()` directly (which
  wouldn't have caught this, since it bypasses the editor entirely).
- **A hidden "Date" column (added, then reverted, this round) exposed a
  real, previously-latent bug in how column names are looked up.**
  `GenericGridTable::FindColByName()` returns a *raw* index into the
  full column list (HIDDEN columns included), but `wxGrid`'s own
  `SetCellValue()`/`GetCellValue()` expect a *visible* index -- these
  only coincide when every HIDDEN column is trailing (a constraint the
  code already documented, but didn't enforce). Inserting a hidden
  column anywhere else silently shifted every later column's visible
  index out from under `SetCellValueByName()`/`GetCellValueByName()`,
  which combine `FindColByName()`'s raw index directly with `wxGrid`
  calls. The Date column itself was reverted per feedback (date and
  time are staying combined in one column, as before), but this class
  of bug is worth knowing about if a future change ever needs a hidden
  column again: keep it trailing, or fix the raw/visible mismatch
  properly rather than relying on ordering.
- **Single-level undo (Cmd+Z) needed two additional fixes beyond the
  basic snapshot/restore logic, both found via a live test.** (1)
  Restoring rows via `GenericGridTable::AppendDataRow()` (chosen
  specifically so undo restores the exact snapshotted values, without
  re-running `OnRowAdded()`'s auto-fill/auto-increment/inheritance
  logic on top of them) doesn't itself notify the attached `wxGrid` of
  the new row count the way `AppendRows()` does -- confirmed via
  testing that skipping this reliably produces a `wxGrid` assertion
  failure ("invalid row index") the moment the grid tries to do almost
  anything afterward. Fixed with an explicit
  `NotifyGridRowCountChanged()` call. (2) The per-cell setup every row
  needs (read-only MULTI_CHOICE/BUTTON cells, custom geo/choice
  editors, BUTTON styling) is applied in `OnRowAdded()` and the
  constructor, but undo's `AppendDataRow()`-based restore bypasses both
  -- fixed by extracting that setup into a shared
  `ApplyPerCellSetup(row)` method and calling it after undo's restore
  loop too. Separately: `DataTab::SetCellValueByName()` -- used by
  keyboard-shortcut field population, among other things -- goes
  through `wxGrid::SetCellValue()`, which (confirmed via testing) does
  *not* fire `wxEVT_GRID_CELL_CHANGING` the way an interactive in-place
  edit does, so it was silently bypassing the undo snapshot entirely;
  fixed by saving a snapshot explicitly inside
  `SetCellValueByName()` too, so programmatic edits are undoable the
  same as interactive ones.
- **The status bar's GPS-unchanged warning could visually overlap
  neighboring fields** -- a real reported bug. The warning used to be
  appended directly onto the Vessel Position field's own text, so that
  field's effective width changed at runtime (short most of the time,
  much longer when something was wrong) in a way the wrap sizer didn't
  always accommodate cleanly. Fixed by giving the warning its own
  dedicated field/slot in the status bar (blank most of the time), and
  by explicitly calling `Layout()` on the whole status bar after any
  tick where label text changed length -- `SetLabel()` alone doesn't
  reliably trigger a `wxWrapSizer` to recompute on its own, the same
  underlying class of issue as the resize-time `Layout()` calls
  documented elsewhere in this file.
- **The Settings tab is now a scrollable `wxScrolledWindow`** (not a
  plain `wxPanel`) -- reported as needed since not every control fit on
  screen in Split Horizontal, where this tab only gets 40% of the
  screen's height. Its descriptive paragraphs re-`Wrap()` themselves to
  the panel's current width on every resize (bound via `wxEVT_SIZE`),
  rather than a fixed pixel width, since a fixed width would either
  overflow in Split Vertical (40% of the screen's *width* there) or
  waste space in a wide window.
- **The "Tick Chart to show a row on the chart" and multi-select hint
  text were both removed** from every tab's toolbar, per direct
  feedback that they weren't wanted.
- **Lat/lon cells now edit in whatever the current display format is,
  parsing back to decimal degrees on save.** An earlier version always
  showed/accepted raw decimal degrees while editing specifically to
  avoid storing formatted text verbatim -- correct, but reported as
  confusing (the display format visibly "reverting" the moment you
  tried to edit a cell). Fixed by having `FormattedLatLonGridCellEditor`
  seed the edit box from the *formatted* display value instead, and
  parse the typed text back to decimal degrees in `EndEdit()` via
  `LatLonFormat::ParseValue()` before it's ever stored -- tolerant of
  punctuation (degree signs, quotes, spacing) so it doesn't require an
  exact match to the current display format, just up to three numbers
  in order (degrees[, minutes[, seconds]]) plus an optional hemisphere
  letter or leading minus sign. Unparseable text shows a warning and
  keeps the previous value rather than silently storing something wrong.
- **Effort Status = ON now also turns tracking on**, the same as "Start
  New Survey" does -- implemented via a small generic
  `DataTab::WatchColumnValue(colName, value, callback)` mechanism
  (checked after both a direct cell edit and a newly-added row, since
  Effort Status commonly arrives at "ON" via inheritance from the
  previous row rather than a fresh edit) rather than hardcoding this one
  case, in case a similar column-value-triggers-an-action need comes up
  elsewhere later.
- **Tab-cycling shortcut changed from Ctrl+Left/Right to
  Cmd+Shift+Left/Right** -- reported as not working on macOS despite
  testing fine on Linux. Ctrl+Left/Right is one of the most common
  macOS *system*-level shortcuts (Mission Control / Spaces switching),
  intercepted by the OS before any application ever sees the keystroke;
  no application-level fix can override a system-level shortcut like
  that, so the combination itself had to change. Matches the
  Cmd+Shift+<key> pattern already used for the row-adding shortcuts.
- **A second guard layer was added to the shared keyboard handler**
  (`wxGetActiveWindow() != this`, on top of the existing `FindFocus()`
  check) as a further attempt at the reported "+ chart zoom-in stopped
  working" issue -- a different, independent query for "is this
  specific window currently the frontmost/active one," not guaranteed
  to agree with "what has keyboard focus" on every platform in every
  situation. Flagged honestly: this is a second attempt building on one
  that evidently wasn't sufficient by itself, and still couldn't be
  fully verified without live testing inside a real, running OpenCPN.
- **Dropdown type-ahead: a second attempt, still unverified.** Choice
  columns now get an explicitly-constructed `wxGridCellChoiceEditor`
  (matching the pattern already used for geo/multi-select columns) in
  place of wx's automatic "choice:..." type-name-based editor creation,
  on the theory that these might go through different internal wx code
  paths despite the visible behavior being meant to be equivalent.
  `allowOthers` stays `false` -- type-ahead is about *jumping to* an
  existing option while it's focused, not about permitting arbitrary
  free text, which would undermine the point of a restricted dropdown.
  Couldn't be verified either way without live testing on a real Mac.
- **Status bar wrap in Split Vertical: fixed the same way the grid
  column resize was, since it turned out to need the same fix.** A
  previous attempt (a plain `wxEVT_SIZE`-time `Layout()` nudge) reliably
  fixed the notebook/grid but evidently wasn't enough on its own for the
  status bar's `wxWrapSizer`. It's now *also* re-laid-out from inside
  the existing debounce timer (the one that recalculates grid column
  widths), after the platform's own resize negotiation has had a moment
  to settle -- the same reasoning, and the same timer, that made the
  grid columns reliably. Verified via a live test that resizes a real
  window and checks the actual screen Y-positions of two known status
  fields before/after, confirming they move to different rows once the
  window is narrow enough that they no longer fit on one line.
- **Horizontal split ratio fixed to also be 60/40 (map/plugin).** An
  earlier version used a plain 50/50 split for Split Horizontal, only
  applying the 60/40 reasoning to Split Vertical; now both use the same
  ratio, just split along different axes.
- **Second text-size setting**, `ui_font_size` (`display.csv`),
  separate from the existing `grid_font_size` -- applied recursively to
  every control in the window except `wxGrid`s, so it doesn't fight
  with the grid-specific size.
- **Settings tab** consolidates what used to be scattered across the
  window: the Tracking controls (previously their own tab, now folded
  in and renamed), the Effort tab's reminder interval (previously a
  spinctrl in the status bar -- the live countdown itself stays in the
  status bar, since that's something you want visible while working,
  not a setting), and the file path links (previously their own row
  between the tabs and the bottom bar).
- **Status bar moved back to the top** of the window, above the tabs --
  an earlier version moved it below the tabs to maximize vertical space
  for the grids; reverted per direct feedback.
- **The "Tick Chart to show a row on the chart" hint text was removed**
  from every tab's toolbar; the multi-select hint ("double-click to
  select multiple"), where applicable, is unaffected.
- **Decimal Degrees display now rounds to 4 decimal places**, not 6 --
  the underlying stored value is unaffected (still 6 decimal places in
  the CSV), this only changes what's shown on screen in that one display
  format.

- **Lat/lon display format: three real bugs found via direct testing,
  not just code review.** (1) `wxString::Format()` in this wx 3.2 build
  reproducibly segfaults when a "%0*d"-style dynamic-width specifier is
  used, and *separately* crashes whenever a numeric conversion and a
  wxString conversion are combined as arguments to the *same* `Format()`
  call -- confirmed via a full backtrace pointing at
  `wxFormatString::AsWChar()`. Fixed by building each piece (degrees,
  minutes, seconds) as its own single-argument `Format()` call and
  concatenating with `operator+`, with degree zero-padding done by hand
  rather than via a dynamic width specifier. (2) A `\u00B0` (degree
  sign) escape embedded directly in a narrow string literal was
  silently corrupted or dropped depending on this build's default
  string-encoding assumptions; fixed by constructing it via
  `wxUniChar(0x00B0)` instead, which builds the character from its
  actual Unicode code point rather than depending on literal encoding.
  Grid cells for lat/lon columns always *edit* as plain decimal degrees
  regardless of the display format, via a small custom
  `wxGridCellTextEditor` subclass that seeds the edit box from the raw
  stored value rather than the formatted display value (`GenericGridTable::
  GetValue()` is what applies formatting, and would otherwise seed the
  editor with formatted text that could get saved back verbatim,
  silently corrupting the stored value).
- **Ctrl+Left/Right tab cycling had a real bug caught by live testing,
  not visible from code review.** The condition originally also required
  `!evt.CmdDown()` (intending "literal Ctrl, not some other modifier
  combo"), but since `CmdDown()` is documented as literally identical to
  `ControlDown()` on every platform except macOS, that condition could
  never be satisfied on non-Mac platforms -- pressing Ctrl there always
  makes `CmdDown()` true too, so `!evt.CmdDown()` was always false.
  Confirmed broken, then confirmed fixed, via a live test that
  synthesizes real `wxEVT_CHAR_HOOK` events against a genuinely-focused
  window and checks the resulting notebook selection index.
- **A focused window guard was added to the shared keyboard handler**
  (`OnCharHook`), in response to a reported issue where OpenCPN's own
  "+" chart zoom-in shortcut stopped working after this plugin's
  shortcut system was introduced. The guard makes it structurally
  impossible for this handler to process (or even look at) a keystroke
  unless the currently focused control is actually part of this
  plugin's own window -- so if this was ever consulted for a keystroke
  intended for OpenCPN's own window, it now cannot be. Flagged
  honestly: "+" was never one of this plugin's own shortcuts, so if this
  fixes the reported issue, the exact prior mechanism was something
  about this handler being reachable when it shouldn't have been,
  rather than a matching bug -- this couldn't be fully confirmed without
  live testing inside a real, running OpenCPN.
- **Dropdown type-ahead (typing a letter to jump to a matching option)
  was investigated, not fixed.** Choice columns use wx's built-in
  `wxGridCellChoiceEditor`, which wraps a native OS combo/list control --
  type-ahead-to-jump is normally a platform-native behavior of that
  control, not something this plugin's code renders or intercepts. No
  code-level lever was found to control this from the plugin side, and
  it couldn't be verified one way or the other without live testing on
  a real Mac. If it's still not working, the most useful next step
  would be confirming whether type-ahead works in an *unrelated* native
  macOS combo box outside this plugin, to narrow down whether this is a
  wxWidgets/wx-grid-specific limitation or something else.
- **Per-tab marker shape/color, not per-row.** Sightings, Events, and
  Surfacing each get one shape/color choice (via each tab's own
  "Marker:" control), read once per chart repaint -- a meaningfully
  different, simpler design than the removed per-*row* "Mark Type"
  dropdown (see below), which looked up a shape per plotted point and
  was where an unrelated, separately-diagnosed crash lived.
  Surfacing's "SightNo" is a free-text field rather than a dropdown
  populated from the live Sightings tab: this plugin's column lists are
  declared once, up front, and SightNos accumulate as a survey
  progresses, so a static choice list would go stale immediately. Type
  the same number shown on the corresponding Sightings row by hand.
- **Track lifecycle now tied to survey lifecycle.** Tracking used to
  default to on; it now defaults to *off*, and only turns on (persisting
  that across restarts) the first time "Start New Survey" is used --
  see `tracking.csv` above. Nothing retroactively fixes a track.csv
  that already accumulated points before this change shipped; that
  file, if present from an earlier version, is left as-is.
- **Status bar wrapping in Split Vertical was a real, reported bug,**
  traced to the same underlying issue as the notebook/grid resize fixes
  from an earlier round: an explicit re-layout nudge existed for the
  notebook on every frame resize, but not for the status bar's
  `wxWrapSizer` (a sibling under the same root sizer, not a descendant
  of the notebook) -- so the notebook resized correctly while the status
  bar's wrapping didn't recompute. Fixed by widening that nudge to the
  whole root panel rather than narrowly targeting just the notebook.
- **Environmental and Effort were merged into one tab (titled
  "Effort"), and the internal C++ names didn't fully follow.** The
  `LogWindow::Environmental()` accessor and `m_environmental` member
  still exist under those original names -- the data is still primarily
  environmental conditions, just with an added Effort Status column --
  rather than renaming everything for what's ultimately a UI-label
  change. `RunShortcutAction()` treats `AddEffort` as an alias for
  `AddEnvironmental` (both add a row to the same merged tab), so an
  existing `shortcuts.csv` referencing the old standalone Effort tab's
  shortcut keeps working rather than silently going stale.
- **Sea State is now a bare number (1-9), no description text** -- an
  earlier version spelled out the Beaufort scale ("3 - Slight") for
  each option; removed since the number alone is enough for anyone who
  knows the scale, and it's meaningfully more compact.
- **The Settings tab isn't a DataTab.** It's a plain panel (checkboxes,
  spinctrls, links) added directly to the notebook, not backed by a CSV
  grid like the others -- added as the *last* notebook page specifically
  because `OnPageChanged()`'s bounds check (`sel < m_tabs.size()`)
  relies on every non-DataTab page coming after all the real ones, to
  safely skip trying to resize a grid that isn't there. Originally
  titled "Tracking" and holding only the tracking controls; renamed and
  expanded to also hold the Effort tab's reminder interval and the file
  path links (data folder, dropdowns, shortcuts, display), all
  previously scattered elsewhere in the window.
- **Two distinct "something's wrong with GPS" signals, not one.** The
  original 30-second "no fix received at all" watchdog is unchanged, but
  there's now a second, independent check: if fixes keep arriving right
  on schedule but the position value itself hasn't moved by more than
  roughly 5 meters in over 60 seconds, the status bar flags it separately
  (amber, not red -- a genuinely stationary vessel would look identical,
  so this is a caution, not a hard error). The ~5m threshold exists so
  ordinary GPS jitter alone doesn't itself look like "the position kept
  changing."
- **Multi-select columns (Behaviors) are read-only via the normal text
  editor** -- `wxGrid::SetReadOnly()` is set on every cell in a
  MULTI_CHOICE column (including newly-added rows -- this has to be
  reapplied per row, not just once at construction, since new rows don't
  automatically inherit an earlier `SetReadOnly()` call). Double-click
  or Enter/F2 opens the checklist dialog instead; typing directly into
  the cell is intentionally not possible, to avoid the comma-separated
  storage format getting hand-typed incorrectly.
- **Environmental's reminder interval control moved out of the tab and
  into the status bar**, next to the countdown it controls; the old
  "Reset Timer" button was removed since adding a row already resets
  the countdown (the same thing the button did, just automatically, at
  the actual moment a check gets logged).
- **View presets can move OpenCPN's own window,** not just this
  plugin's -- see "View presets" above for what Split Vertical/
  Horizontal actually do and why that's more invasive than typical
  plugin behavior.
- **Mark Type dropdown removed.** An earlier version let each Sightings/
  Events row pick its own marker shape (Diamond/Circle/Square/Triangle/
  Star). It caused a crash on selection that couldn't be reliably
  reproduced or root-caused -- a separate, unrelated wxMemoryDC crash
  in the same rendering code *was* found and fixed (below), and was
  directly verified fixed via live GL-context testing across all five
  shapes, but the user-reported crash persisted regardless. Given the
  feature's low importance at this stage, it was removed entirely rather
  than continuing to chase an elusive bug blind. Sightings/Events now
  always draw as diamonds/squares respectively.
- **Chart-overlay crash when selecting a Mark Type -- a distinct,
  confirmed-and-fixed bug found along the way, kept here for the
  record even though the feature itself is now removed.** The GL
  label-rendering path (used for SightNo labels) called
  `GetTextExtent()` on a `wxMemoryDC` with no bitmap selected into it --
  invalid on essentially every platform, since a memory DC needs a
  valid bitmap before any drawing/measurement call. Verified fixed by
  actually exercising `RenderGLOverlay` against a real, current OpenGL
  context (a `wxGLCanvas` under Mesa software rendering), not just code
  review.
- **Window closing.** The log window genuinely closes (and its C++
  object is destroyed) when you click its close button, rather than
  merely hiding — `SpotterPlugin::EnsureLogWindow()` transparently
  recreates a fresh one (reloading the CSVs, a fast operation) the next
  time the toolbar button is clicked.
- Waypoints/markers are purely visual (drawn by this plugin's own chart
  overlay), created/moved/removed live as you tick "Chart" or edit a
  row's position. Sightings markers track the *computed sighting
  position* (vessel position + bearing/distance), not the vessel's own
  position.
- **Keyboard shortcuts fixed via two confirmed bugs, verified with real
  synthetic keystrokes.** (1) `wxKeyEvent::CmdDown()` is documented as
  literally identical to `ControlDown()` on every platform except
  macOS -- so on those platforms, requiring both flags to hold the
  exact values implied by a shortcut's modifier tokens could never be
  satisfied. Ctrl and Cmd are now treated as interchangeable when
  matching. (2) The originally-shipped defaults (`Cmd+1` through
  `Cmd+4`) are exactly the pattern macOS and many apps reserve for
  window/tab switching; defaults now use `Cmd+Shift+<letter>` instead.
- **Column resize-to-fit needed more than an ordinary `wxEVT_SIZE`
  handler to actually work, confirmed via live testing.** `wxGrid`'s
  natural "best size" was being enforced by the sizer chain as a hard
  minimum on the whole window (fixed with explicit `SetMinSize()` on
  the grid and notebook), and reading a child's client size immediately
  during its own resize (even via `CallAfter()`/`wxSafeYield()`) could
  still see a stale, pre-layout size. The resize handler lives on the
  top-level frame (reliable; a deeply-nested child's own `wxEVT_SIZE`
  was confirmed via testing not to reliably fire when the frame
  resizes) and debounces the actual recalculation behind a short timer.
  Columns additionally never shrink below what their own header title
  needs, computed via `wxWindow::GetTextExtent()` (safe -- unlike a
  standalone, bitmap-less `wxMemoryDC`, see above).
- The GPS-lost check is a 30-second-since-last-fix watchdog (there's no
  push notification for signal loss in the plugin API), polled every
  second (same timer that drives the live clock/position/speed display
  at the top of the window) -- not instantaneous, but avoids false
  positives from a single dropped NMEA sentence.
- The Environmental tab's reminder-overdue tint restores the grid's
  *actual* captured default background color (whatever the current
  theme/dark-mode setting is) rather than hardcoding white, so it
  doesn't fight with dark mode.
- The "Track log" status shown is purely informational (always
  recording, throttled, whenever a GPS fix is available) -- there's no
  pause/resume control for it currently, unlike Effort. The "Effort"
  status shown is just the most recently logged Effort row's Status
  value, not a separately-tracked live toggle.
- CSV rewriting: each tab keeps its data in memory and rewrites its
  whole CSV file (via a write-to-temp-then-rename, so a crash mid-write
  can't corrupt the file) after every edit, add, or delete. This is
  simple and safe for the row counts a survey season produces (hundreds
  to low thousands of rows); it's not designed for huge datasets. The
  trackline overlay similarly redraws all recorded points on every
  repaint with no viewport culling -- fine for a normal survey day, but
  a good place to optimize first if a very long multi-day trackline ever
  becomes visibly slow to render.
- No settings panel yet — the handful of tunables (reminder interval
  default, GPS-lost threshold, dropdown lists, keyboard shortcuts,
  marker/label/line sizes) are either editable at runtime (reminder
  interval, species/events/observers/behaviors lists, shortcuts.csv, display.csv) or require a
  source edit (GPS-lost threshold, column layout, marker colors/shapes,
  resize debounce delay).
