cmake_minimum_required(VERSION 3.10)

# ---------------------------------------------------------------------------
# Plugin metadata, in testplugin_pi/Frontend2's conventional settings-block
# form (variable names and structure verified against a real, current
# testplugin_pi checkout) -- kept even though this project doesn't (yet)
# use Frontend2's own opencpn-libs-submodule-based build machinery below,
# since this block is what an eventual catalog metadata.xml is generated
# from, and having it in the standard shape now is direct groundwork for
# that, independent of the rest of the build system.
# ---------------------------------------------------------------------------
set(VERBOSE_NAME "Spotter")
set(COMMON_NAME "Spotter")
set(TITLE_NAME "spotter")
set(PACKAGE_CONTACT "Hansen Johnson")
set(PACKAGE "spotter")
set(SHORT_DESCRIPTION
    "Wildlife survey logging: sightings, effort, events, and a trackline"
)
set(LONG_DESCRIPTION
    "Spotter is a logging tool for marine mammal and wildlife surveys \
aboard a vessel running OpenCPN. It records sightings, on/off-effort \
status and environmental conditions, and general events, alongside an \
automatically-recorded trackline -- all timestamped and geotagged from \
OpenCPN's own GPS feed, with configurable chart markers for sightings \
and events."
)

set(VERSION_MAJOR "2")
set(VERSION_MINOR "0")
set(VERSION_PATCH "0")
set(VERSION_TWEAK "0")
set(VERSION_DATE "06/08/2026") # DD/MM/YYYY format
set(OCPN_MIN_VERSION "ov58")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "18")

set(WX_VER "32")
set(WX_VER_PATCH "4")

set(TP_COMMENT "  * Local/manual builds -- not yet CI-built or catalog-listed")

set(PARENT "opencpn")

# Specifies Cloudsmith upload repository suffix for each catalog
set(PROD "prod") # Standard Repos
set(BETA "beta") # Standard Repos
set(ALPHA "alpha") # Standard Repos

# Not yet set up -- this plugin isn't built via CI or uploaded to
# Cloudsmith at all right now (see README.md's "Windows" and "Official
# catalog" sections for the current, deliberately-manual install path).
# Left here, commented, as the placeholder an eventual CI setup would
# fill in, rather than omitted entirely.
# set(CLOUDSMITH_BASE_REPOSITORY "spotter")
# set(CLOUDSMITH_USER "hansenjohnson")

set(XML_INFO_URL "https://github.com/hansenjohnson/spotter")
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

# ---------------------------------------------------------------------------
# End of testplugin_pi-conventional metadata block.
# ---------------------------------------------------------------------------

project(spotter_pi)

set(PACKAGE_NAME spotter_pi)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# wxWidgets
#
# On macOS, install a version matching OpenCPN's own bundled wxWidgets --
# see README.md's "Matching OpenCPN's exact wxWidgets build" section.
#   brew install wxwidgets@3.2
#   cmake .. -DwxWidgets_CONFIG_EXECUTABLE=$(brew --prefix wxwidgets@3.2)/bin/wx-config-3.2
# ---------------------------------------------------------------------------
set(wxWidgets_USE_LIBS core base adv aui html gl)
find_package(wxWidgets REQUIRED COMPONENTS ${wxWidgets_USE_LIBS})
include(${wxWidgets_USE_FILE})

# The chart overlay's OpenGL rendering path (RenderGLOverlay) needs the
# system OpenGL library. On macOS this comes transitively via wxWidgets'
# own OpenGL.framework dependency; find_package here is mainly to keep
# Linux builds (e.g. this project's own test harness during development)
# linking cleanly too.
find_package(OpenGL)

# ---------------------------------------------------------------------------
# OpenCPN plugin API (bundled locally under libs/api-18 -- no submodules,
# no network access needed to build)
# ---------------------------------------------------------------------------
add_subdirectory(libs/api-18)

# ---------------------------------------------------------------------------
# Plugin sources
# ---------------------------------------------------------------------------
set(SRCS
  src/spotter_pi.cpp
  src/spotter_pi.h
  src/SpotterIcon.h
  src/LogWindow.cpp
  src/LogWindow.h
  src/DataTab.cpp
  src/DataTab.h
  src/GenericGridTable.cpp
  src/GenericGridTable.h
  src/TrackRecorder.cpp
  src/TrackRecorder.h
  src/CsvUtils.cpp
  src/CsvUtils.h
  src/CategoryConfigFile.cpp
  src/CategoryConfigFile.h
  src/ShortcutsFile.cpp
  src/ShortcutsFile.h
  src/DisplaySettings.cpp
  src/DisplaySettings.h
  src/TrackingSettings.cpp
  src/TrackingSettings.h
  src/LatLonFormat.cpp
  src/LatLonFormat.h
  src/TimeZoneSetting.cpp
  src/TimeZoneSetting.h
  src/PositionHeights.cpp
  src/PositionHeights.h
  src/ColumnDefinitions.cpp
  src/ColumnDefinitions.h
  src/ocpn_plugin_defaults.cpp
)

add_library(${PACKAGE_NAME} SHARED ${SRCS})

target_include_directories(${PACKAGE_NAME} PRIVATE src)

if(APPLE)
  # Deliberately NOT linking wxWidgets_LIBRARIES here -- confirmed as
  # the cause of a real crash, via a real beta tester's `otool -L`
  # output on the actual built .dylib, which showed hard dependencies
  # baked in on Homebrew-specific paths
  # (/opt/homebrew/opt/wxwidgets@3.2/lib/libwx_osx_cocoau_core-3.2.dylib
  # and siblings). Those don't exist inside OpenCPN.app's own bundle,
  # which carries its own, separate copy of wxWidgets -- a version/path
  # mismatch matching a real, documented OpenCPN issue (github.com/
  # OpenCPN/OpenCPN/issues/2153) and OpenCPN's own build docs' warning
  # that Homebrew's wxWidgets isn't guaranteed to match an official
  # build. The same `-undefined dynamic_lookup` this project already
  # relies on to defer OpenCPN's own API symbols (InsertPlugInTool
  # etc.) to load-time applies here too: without an explicit link,
  # wxWidgets symbols are deferred the same way, resolved at dlopen()
  # time against whatever wxWidgets OpenCPN.app itself has already
  # loaded, rather than a hardcoded path that varies by machine and
  # Homebrew formula version. Headers/include paths from
  # find_package(wxWidgets) above are still needed and still used --
  # only the *library* linking step is skipped here.
  target_link_libraries(${PACKAGE_NAME} PRIVATE ocpn::api)
else()
  target_link_libraries(${PACKAGE_NAME} PRIVATE ocpn::api ${wxWidgets_LIBRARIES})
endif()
if(OpenGL_FOUND)
  target_link_libraries(${PACKAGE_NAME} PRIVATE OpenGL::GL)
endif()

# Plugins have no "lib" prefix / version suffix conventions to match OpenCPN's
# loader expectations, but the default CMake "lib<name>.dylib" naming is what
# OpenCPN's macOS plugin loader expects, so we leave the defaults in place.

if(APPLE)
  set_target_properties(${PACKAGE_NAME} PROPERTIES
    SUFFIX ".dylib"
    MACOSX_RPATH ON
  )
  # macOS's linker refuses to produce a .dylib with unresolved symbols by
  # default. That's normally a good safety check, but OpenCPN plugins are
  # deliberately built with symbols left unresolved -- OpenCPN itself
  # supplies them (InsertPlugInTool, the opencpn_plugin_XX default virtuals,
  # etc.) at dlopen() time when it loads the plugin. This is the same
  # "plugin dylib" pattern Apple's own bundle/plugin docs describe, and
  # exactly what OpenCPN's own build scripts do for every plugin. This flag
  # is also what makes it possible to skip linking wxWidgets_LIBRARIES
  # directly on this platform (see the APPLE block above, near
  # target_link_libraries) -- without it, leaving wxWidgets symbols
  # unresolved like that wouldn't link at all.
  target_link_options(${PACKAGE_NAME} PRIVATE -undefined dynamic_lookup)
endif()

if(WIN32)
  # OpenCPN plugin DLLs on Windows conventionally have no "lib" prefix
  # (e.g. spotter_pi.dll, not libspotter_pi.dll) -- CMake's default on
  # this platform already omits it for a SHARED library target, so
  # nothing needs setting here explicitly, but noted for clarity since
  # the APPLE block above does need to override CMake's default there.
  #
  # Unlike macOS (which defers resolving a plugin's calls into OpenCPN
  # itself -- InsertPlugInTool and the like -- until the plugin is
  # dlopen()'d at runtime, so the linker never needs to see those
  # symbols), Windows DLLs need every symbol resolved at link time.
  # That means this plugin needs OpenCPN's own opencpn.lib import
  # library on hand to link against, the same requirement every other
  # OpenCPN plugin has on this platform. Point OPENCPN_IMPORT_LIB at it
  # (from an existing OpenCPN Windows build, or a prebuilt one from
  # OpenCPN's own distribution) -- e.g.:
  #   cmake .. -DOPENCPN_IMPORT_LIB=C:/path/to/opencpn.lib
  # This hasn't been build-tested end-to-end on an actual Windows
  # machine as part of this project -- unlike the rest of this plugin,
  # which is continuously built and tested throughout development, no
  # Windows environment has been available to do the same here. Please
  # report any build issues this surfaces.
  if(OPENCPN_IMPORT_LIB)
    target_link_libraries(${PACKAGE_NAME} PRIVATE "${OPENCPN_IMPORT_LIB}")
  else()
    message(WARNING
      "OPENCPN_IMPORT_LIB is not set -- linking this plugin's DLL will "
      "likely fail with unresolved OpenCPN API symbols (InsertPlugInTool "
      "etc). Set it to the path of your OpenCPN build's opencpn.lib, e.g. "
      "-DOPENCPN_IMPORT_LIB=C:/path/to/opencpn.lib")
  endif()
endif()

message(STATUS "----------------------------------------------------------")
message(STATUS " ${PACKAGE_NAME} will be built as: $<TARGET_FILE:${PACKAGE_NAME}>")
message(STATUS " To install for local testing, copy the built library into")
message(STATUS "  ~/Library/Application Support/OpenCPN/Contents/PlugIns/")
message(STATUS " (see README.md for the exact steps)")
message(STATUS "----------------------------------------------------------")

# ---------------------------------------------------------------------------
# Standalone test harness: a small wx app that fakes the OpenCPN host
# functions so you can open the log window and exercise CSV logging
# without OpenCPN installed. See test_harness/main.cpp.
# ---------------------------------------------------------------------------
option(BUILD_TEST_HARNESS "Build the standalone UI test harness" ON)

# The distributed package (see packaging/make_tarball.sh and README.md's
# "Preparing a release" section) deliberately excludes test_harness/ --
# it's a development-only tool with no purpose for someone who just
# wants to build and run the plugin. Guarded on the directory actually
# existing so that exclusion doesn't turn into a broken build for
# anyone building straight from the distributed source package with
# BUILD_TEST_HARNESS left at its default of ON.
if(BUILD_TEST_HARNESS AND EXISTS "${CMAKE_SOURCE_DIR}/test_harness/main.cpp")
  add_executable(spotter_test_harness
    test_harness/main.cpp
    test_harness/ocpn_stubs.cpp
    src/spotter_pi.cpp
    src/spotter_pi.h
    src/SpotterIcon.h
    src/LogWindow.cpp
    src/LogWindow.h
    src/DataTab.cpp
    src/DataTab.h
    src/GenericGridTable.cpp
    src/GenericGridTable.h
    src/TrackRecorder.cpp
    src/TrackRecorder.h
    src/CsvUtils.cpp
    src/CsvUtils.h
    src/CategoryConfigFile.cpp
    src/CategoryConfigFile.h
    src/ShortcutsFile.cpp
    src/ShortcutsFile.h
    src/DisplaySettings.cpp
    src/DisplaySettings.h
    src/TrackingSettings.cpp
    src/TrackingSettings.h
    src/LatLonFormat.cpp
    src/LatLonFormat.h
    src/TimeZoneSetting.cpp
    src/TimeZoneSetting.h
    src/PositionHeights.cpp
    src/PositionHeights.h
    src/ColumnDefinitions.cpp
    src/ColumnDefinitions.h
    src/ocpn_plugin_defaults.cpp
  )
  target_include_directories(spotter_test_harness PRIVATE src)
  target_link_libraries(spotter_test_harness PRIVATE ocpn::api
                                                              ${wxWidgets_LIBRARIES})
  if(OpenGL_FOUND)
    target_link_libraries(spotter_test_harness PRIVATE OpenGL::GL)
  endif()
  if(MSVC)
    # wxIMPLEMENT_APP() (in test_harness/main.cpp) generates a WinMain()
    # entry point by default on MSVC, for the Windows (GUI) subsystem.
    # This target is linked for the Console subsystem instead (CMake's
    # default for add_executable() when WIN32_EXECUTABLE isn't set) --
    # deliberately, since test output needs to print to stdout/the CI
    # log, not disappear into a windowless GUI-subsystem process. The
    # Console subsystem's CRT startup expects main(), not WinMain().
    #
    # Attempted fix, confirmed NOT sufficient on its own: defining
    # _CONSOLE (wxWidgets' documented mechanism for getting
    # wxIMPLEMENT_APP() to generate main() instead of WinMain()) --
    # left in place since it's harmless, but a second real CI run with
    # this define present hit the exact same LNK2019 (unresolved
    # external symbol _main) as the first, unfixed attempt. Rather than
    # keep debugging an obscure entry-point quirk for a target that's
    # purely a development tool and never shipped, this build is
    # simply skipped on Windows entirely -- see
    # .github/workflows/build-windows.yml and README.md's "Building on
    # Windows", both of which build only the spotter_pi target there.
    target_compile_definitions(spotter_test_harness PRIVATE _CONSOLE)
  endif()
endif()
