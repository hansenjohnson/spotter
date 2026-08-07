// Standalone test harness for the Spotter plugin.
//
// This is NOT part of the OpenCPN plugin itself. It links the plugin's
// actual source files together with fake ("stub") versions of the handful
// of OpenCPN host functions the plugin calls, so you can open the log
// window, edit spreadsheet rows, and inspect the CSV files it writes --
// all without having OpenCPN installed. It's the fastest way to iterate
// on the grid layout and logging logic.
//
// Run it with:  ./spotter_test_harness

#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <functional>
#include <stdexcept>
#include <numeric>
#include <cmath>
#include "spotter_pi.h"
#include "LogWindow.h"
#include "TrackRecorder.h"
#include "CsvUtils.h"
#include "CategoryConfigFile.h"
#include "ShortcutsFile.h"
#include "TrackingSettings.h"
#include "LatLonFormat.h"
#include "TimeZoneSetting.h"
#include "DisplaySettings.h"
#include "PositionHeights.h"
#include "ColumnDefinitions.h"

class TestFrame : public wxFrame {
public:
  TestFrame()
      : wxFrame(nullptr, wxID_ANY, "Spotter - Test Harness", wxDefaultPosition,
                wxSize(460, 260)) {
    m_plugin = new SpotterPlugin(nullptr);
    m_plugin->Init();  // creates (hidden) log window + CSV files on disk

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(
        new wxStaticText(panel, wxID_ANY,
                         "This harness fakes OpenCPN just enough to\n"
                         "exercise the plugin's log window and CSV log."),
        0, wxALL, 10);

    wxButton* fixBtn =
        new wxButton(panel, wxID_ANY, "Simulate GPS Fix (42.35N, -70.05W)");
    fixBtn->Bind(wxEVT_BUTTON, &TestFrame::OnFix, this);
    sizer->Add(fixBtn, 0, wxALL | wxEXPAND, 10);

    wxButton* logBtn = new wxButton(panel, wxID_ANY, "Open Spotter Log");
    logBtn->Bind(wxEVT_BUTTON, &TestFrame::OnOpenLog, this);
    sizer->Add(logBtn, 0, wxALL | wxEXPAND, 10);

    m_status = new wxStaticText(panel, wxID_ANY, "No GPS fix yet.");
    sizer->Add(m_status, 0, wxALL, 10);

    panel->SetSizer(sizer);

    printf("Spotter test harness\n");
    printf("CSV files are being written to:\n  %s\n\n",
           (const char*)m_plugin->GetDataDir().mb_str());
  }

  ~TestFrame() { m_plugin->DeInit(); }

private:
  SpotterPlugin* m_plugin;
  wxStaticText* m_status;

  void OnFix(wxCommandEvent&) {
    PlugIn_Position_Fix fix;
    fix.Lat = 42.35;
    fix.Lon = -70.05;
    fix.Cog = 180.0;
    fix.Sog = 6.5;
    fix.Var = 0.0;
    fix.FixTime = time(nullptr);
    fix.nSats = 8;
    m_plugin->SetPositionFix(fix);
    m_status->SetLabel("Fix set: 42.35000, -70.05000");
  }

  void OnOpenLog(wxCommandEvent&) {
    m_plugin->OnToolbarToolCallback(m_plugin->GetLogToolId());
  }
};

class TestApp : public wxApp {
public:
  bool OnInit() override {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (wxApp::argc > 1 && wxString(wxApp::argv[1]) == "--selftest") {
      RunSelfTest();
      ExitMainLoop();
      return false;
    }

    TestFrame* frame = new TestFrame();
    frame->Show(true);
    return true;
  }

private:
  // Exercises the plugin entirely through its public API -- no simulated
  // mouse clicks needed -- and prints PASS/FAIL for each step. Useful for
  // quick regression checks after editing the code.
  void RunSelfTest() {
    int failures = 0;
    auto check = [&](bool cond, const wxString& what) {
      printf("%s %s\n", cond ? "PASS" : "FAIL", (const char*)what.mb_str());
      fflush(stdout);
      if (!cond) failures++;
    };

    try {
      RunSelfTestBody(check);
    } catch (const std::exception& e) {
      printf("EXCEPTION: %s\n", e.what());
      fflush(stdout);
      failures++;
    } catch (...) {
      printf("UNKNOWN EXCEPTION\n");
      fflush(stdout);
      failures++;
    }

    printf("\n%s (%d failure%s)\n",
           failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures,
           failures == 1 ? "" : "s");
    fflush(stdout);
    std::exit(failures == 0 ? 0 : 1);
  }

  void RunSelfTestBody(std::function<void(bool, const wxString&)> check) {
    SpotterPlugin plugin(nullptr);
    plugin.Init();
    printf("Data dir: %s\n", (const char*)plugin.GetDataDir().mb_str());

    // Checked immediately after Init(), before anything else in this test
    // could touch it (e.g. setting Effort Status to ON later on now
    // itself turns tracking on -- see the dedicated check for that further
    // down -- which would make a later "starts off" check here misleading
    // if it ran after that).
    {
      TrackRecorder* freshTrack = plugin.GetTrackRecorder();
      check(freshTrack != nullptr, "TrackRecorder created");
      check(!freshTrack->IsEnabled(), "Tracking starts OFF by default");
      check(freshTrack->GetPoints().empty(),
            "No points recorded yet since tracking hasn't been enabled");
      TrackingSettings freshTs(plugin.GetDataDir());
      check(!freshTs.Enabled(),
            "TrackingSettings itself also defaults to disabled");
    }

    PlugIn_Position_Fix fix;
    fix.Lat = 42.35;
    fix.Lon = -70.05;
    fix.Sog = 6.5;
    fix.Cog = 180.0;
    fix.FixTime = time(nullptr);
    plugin.SetPositionFix(fix);

    double gotLat = 0, gotLon = 0;
    plugin.GetLastFix(gotLat, gotLon);
    check(gotLat == 42.35 && gotLon == -70.05,
          "GetLastFix returns the fix just set");
    check(plugin.GetLastSog() == 6.5, "GetLastSog returns the fix's speed");

    // --- Lat/Lon display format: exercise all three formats directly.
    // This is exactly the function that crashed/produced silently wrong
    // output during development -- a "%0*d"-style dynamic-width
    // specifier mixed with other argument types in one wxString::
    // Format() call reliably segfaulted, and separately, a "\u00B0"
    // escape embedded in a plain narrow string literal (whether inside
    // a Format() call combined with a numeric specifier, or just
    // concatenated via operator+) was silently corrupted/dropped by
    // this wx build's implicit const char* -> wxString conversion.
    // Fixed by building the degree sign via wxUniChar(0x00B0) instead,
    // which is also why the *expected* strings below are built the same
    // way rather than also embedding "\u00B0" as a literal escape.
    {
      const wxString kDeg(wxUniChar(0x00B0));

      LatLonFormat::Set(LatLonFormat::Format::DegreesDecimalMinutes);
      wxString ddmLat = LatLonFormat::FormatValue(42.35, true);
      wxString expectedDdmLat = "42" + kDeg + " 21.000' N";
      check(ddmLat == expectedDdmLat, "DDM format for 42.35N is \"" +
                                          expectedDdmLat +
                                          "\" (got: " + ddmLat + ")");
      wxString ddmLon = LatLonFormat::FormatValue(-70.05, false);
      wxString expectedDdmLon = "070" + kDeg + " 03.000' W";
      check(ddmLon == expectedDdmLon, "DDM format for -70.05 (lon) is \"" +
                                          expectedDdmLon +
                                          "\" (got: " + ddmLon + ")");

      LatLonFormat::Set(LatLonFormat::Format::DecimalDegrees);
      wxString ddLat = LatLonFormat::FormatValue(42.35, true);
      wxString expectedDdLat = "42.3500" + kDeg + " N";
      check(ddLat == expectedDdLat, "DD format for 42.35N is \"" +
                                        expectedDdLat + "\" (got: " + ddLat +
                                        ")");

      LatLonFormat::Set(LatLonFormat::Format::DegreesMinutesSeconds);
      wxString dmsLat = LatLonFormat::FormatValue(42.35, true);
      wxString expectedDmsLat = "42" + kDeg + " 21' 00.0\" N";
      check(dmsLat == expectedDmsLat, "DMS format for 42.35N is \"" +
                                          expectedDmsLat +
                                          "\" (got: " + dmsLat + ")");

      wxString dmsLonNeg = LatLonFormat::FormatValue(-70.05, false);
      check(dmsLonNeg.EndsWith("W"), "Negative longitude formats as W");

      LatLonFormat::Set(LatLonFormat::Format::DegreesDecimalMinutes);  // reset

      // --- ParseValue: what actually makes editing in the current
      // display format safe -- typed text gets parsed back to decimal
      // degrees before being stored (see FormattedLatLonGridCellEditor
      // in DataTab.cpp). Tolerant of format/punctuation details rather
      // than requiring an exact match to any one display format.
      double parsed = 0.0;
      check(LatLonFormat::ParseValue("42.35", true, &parsed) &&
                std::abs(parsed - 42.35) < 0.0001,
            "ParseValue reads plain decimal degrees");
      check(LatLonFormat::ParseValue("42 21.000 N", true, &parsed) &&
                std::abs(parsed - 42.35) < 0.0001,
            "ParseValue reads degrees+decimal-minutes with N suffix");
      check(
          LatLonFormat::ParseValue("42" + kDeg + " 21.000' N", true, &parsed) &&
              std::abs(parsed - 42.35) < 0.0001,
          "ParseValue reads DDM with degree sign and quote punctuation");
      check(LatLonFormat::ParseValue("42 21 00.0 N", true, &parsed) &&
                std::abs(parsed - 42.35) < 0.0001,
            "ParseValue reads degrees+minutes+decimal-seconds (DMS)");
      check(LatLonFormat::ParseValue("70.05 W", false, &parsed) &&
                std::abs(parsed - (-70.05)) < 0.0001,
            "ParseValue applies a negative sign for W longitude");
      check(LatLonFormat::ParseValue("-42.35", true, &parsed) &&
                std::abs(parsed - (-42.35)) < 0.0001,
            "ParseValue also accepts a literal leading minus sign");
      check(!LatLonFormat::ParseValue("not a position", true, &parsed),
            "ParseValue rejects text with no numbers at all");
      check(!LatLonFormat::ParseValue("999 21 N", true, &parsed),
            "ParseValue rejects an out-of-range latitude (>90)");
    }

    LogWindow* log = plugin.GetLogWindow();
    check(log != nullptr, "LogWindow created");

    check(wxFileExists(plugin.GetDataDir() + wxFileName::GetPathSeparator() +
                       "species.csv"),
          "species.csv created on first run");
    check(wxFileExists(plugin.GetDataDir() + wxFileName::GetPathSeparator() +
                       "event_types.csv"),
          "event_types.csv created on first run");
    check(wxFileExists(plugin.GetDataDir() + wxFileName::GetPathSeparator() +
                       "observers.csv"),
          "observers.csv created on first run");
    check(wxFileExists(plugin.GetDataDir() + wxFileName::GetPathSeparator() +
                       "behaviors.csv"),
          "behaviors.csv created on first run");
    check(wxFileExists(plugin.GetDataDir() + wxFileName::GetPathSeparator() +
                       "shortcuts.csv"),
          "shortcuts.csv created on first run");

    // --- Sightings tab: add a row, fill it in, verify derived columns.
    DataTab* sightings = log->Sightings();
    int before = sightings->RowCount();
    sightings->AddRow();
    check(sightings->RowCount() == before + 1, "Sightings AddRow");
    int row = sightings->RowCount() - 1;
    sightings->SetCellValueByName(row, "Species", "Humpback whale");
    sightings->SetCellValueByName(row, "Num", "3");
    sightings->SetCellValueByName(row, "NumCalf", "1");
    sightings->SetCellValueByName(row, "IDrel", "Definite");
    sightings->SetCellValueByName(row, "BearingMag", "090");
    sightings->SetCellValueByName(row, "DistUnit", "reticles");
    sightings->SetObserverHeightFt(20.0);
    sightings->SetCellValueByName(row, "Dist", "10");
    double sigLatAt20ft = 0, sigLonAt20ft = 0;
    sightings->GetCellValueByName(row, "SigLat").ToDouble(&sigLatAt20ft);
    sightings->GetCellValueByName(row, "SigLon").ToDouble(&sigLonAt20ft);

    // A different row, created fresh at 80ft with the *same* 10-reticle
    // reading, should compute a meaningfully different position --
    // establishing that the height genuinely affects the result at all
    // (otherwise the test below wouldn't actually prove anything).
    sightings->SetObserverHeightFt(80.0);
    sightings->AddRow();
    int heightTestRow = sightings->RowCount() - 1;
    sightings->SetCellValueByName(heightTestRow, "BearingMag", "090");
    sightings->SetCellValueByName(heightTestRow, "DistUnit", "reticles");
    sightings->SetCellValueByName(heightTestRow, "Dist", "10");
    double sigLatAt80ft = 0, sigLonAt80ft = 0;
    sightings->GetCellValueByName(heightTestRow, "SigLat")
        .ToDouble(&sigLatAt80ft);
    sightings->GetCellValueByName(heightTestRow, "SigLon")
        .ToDouble(&sigLonAt80ft);
    check(std::abs(sigLatAt80ft - sigLatAt20ft) > 0.0001 ||
              std::abs(sigLonAt80ft - sigLonAt20ft) > 0.0001,
          "sanity: the same 10-reticle reading computes a different "
          "position at 80ft than at 20ft, so height genuinely matters "
          "here");

    // Now the actual bug fix: re-editing the *original* row (created
    // back when the live height was 20ft) with that same 10-reticle
    // reading, *after* the live height has since changed to 80ft,
    // should reproduce the original 20ft-based position -- not the
    // 80ft one. An earlier version read the live height at recompute
    // time instead of a value captured per-row at creation, which
    // would make this fail (it would match the 80ft position instead).
    sightings->SetCellValueByName(row, "Dist", "10");
    double sigLatReEdited = 0, sigLonReEdited = 0;
    sightings->GetCellValueByName(row, "SigLat").ToDouble(&sigLatReEdited);
    sightings->GetCellValueByName(row, "SigLon").ToDouble(&sigLonReEdited);
    check(std::abs(sigLatReEdited - sigLatAt20ft) < 0.00001 &&
              std::abs(sigLonReEdited - sigLonAt20ft) < 0.00001,
          "Re-editing an old row's Dist after the live observer height "
          "has since changed still uses the height stored at that "
          "row's own creation time, not today's live value");
    sightings->SetObserverHeightFt(20.0);  // restore for the rest of
                                           // this test run
    sightings->SetCellValueByName(row, "Dist", "1.0");
    sightings->SetCellValueByName(row, "DistUnit", "nm");
    sightings->SetCellValueByName(row, "BearingMag", "090");
    sightings->SetCellValueByName(row, "Behavs", "Traveling");
    sightings->SetCellValueByName(row, "Obs", "Observer 1");
    sightings->SetCellValueByName(row, "Img", "Photos");
    sightings->SetCellValueByName(row, "Map", "1");
    check(wxFileExists(sightings->GetCsvPath()), "sightings.csv exists");
    check(sightings->GetCellValueByName(row, "Img") == "Photos",
          "Images Collected column works");
    sightings->SetCellValueByName(row, "NumConf", "At Least");
    check(sightings->GetCellValueByName(row, "NumConf") == "At Least",
          "NumConf column works, including the 'At Least' option");

    check(sightings->GetCellValueByName(row, "SightNo") == "1",
          "First sighting auto-numbered 1");
    sightings->AddRow();
    int row2 = sightings->RowCount() - 1;
    check(sightings->GetCellValueByName(row2, "SightNo") == "3",
          "Third sighting (heightTestRow, added earlier for the "
          "observer-height test, took SightNo 2) auto-numbered 3");
    check(sightings->GetCellValueByName(row2, "Img") == "",
          "New Sightings row leaves Images Collected blank (not "
          "auto-filled), so an unidentified sighting can't look "
          "identified by accident");
    check(sightings->GetCellValueByName(row2, "Species") == "",
          "New Sightings row leaves Species blank too");

    wxString timeVal = sightings->GetCellValueByName(row, "Time");
    check(!timeVal.IsEmpty() && !timeVal.EndsWith("Z"),
          "Time column (full local timestamp) is not UTC-suffixed");
    // A numeric UTC offset like "+0000" or "-0400" should be present --
    // %z, not %Z (a timezone abbreviation, whose exact output depends
    // on the platform's timezone database/locale, more than a plain
    // numeric offset does).
    check(timeVal.Contains("+") || timeVal.Contains("-"),
          "Time column includes a numeric UTC offset (timezone)");

    // --- Chart overlay: Sightings/Events default to Chart=on and should
    // show up in GetChartedPoints() (with the Sighting # label);
    // unchecking should remove them immediately.
    auto chartedBefore = sightings->GetChartedPoints();
    check(chartedBefore.size() == 3,
          "Sightings GetChartedPoints has 3 charted points (row, row2, "
          "and heightTestRow from the observer-height test)");
    bool foundLabel = false;
    for (const auto& pt : chartedBefore) {
      // Default map label is Species + FieldID (see the Settings tab's
      // Map section) -- no longer SightNo. This row has Species set to
      // "Humpback whale" and no FieldID, so the label should be just
      // the Species value.
      if (pt.labelText == "Humpback whale") foundLabel = true;
    }
    check(foundLabel,
          "Charted point carries its configured map label "
          "(Species, by default)");
    sightings->SetCellValueByName(row, "Map", "0");
    check(sightings->GetChartedPoints().size() == 2,
          "Unchecking Chart removes the point from GetChartedPoints "
          "immediately");
    sightings->SetCellValueByName(row, "Map", "1");
    check(sightings->GetChartedPoints().size() == 3,
          "Re-checking Chart restores the point");

    // --- Effort tab (merged with Environmental: environmental conditions
    // + Effort Status column, one tab, titled "Effort" in the UI).
    DataTab* env = log->Environmental();
    check(!plugin.GetTrackRecorder()->IsEnabled(),
          "Tracking still off just before Effort Status is first set to ON");
    env->AddRow();
    int erow = env->RowCount() - 1;
    env->SetCellValueByName(erow, "Effort", "ON");
    check(plugin.GetTrackRecorder()->IsEnabled(),
          "Setting Effort Status to ON automatically turns tracking on "
          "too -- being on effort without a track being recorded doesn't "
          "make sense");
    env->SetCellValueByName(erow, "Beaufort", "3");
    env->SetCellValueByName(erow, "Weather", "Overcast");
    env->SetCellValueByName(erow, "Port", "Observer 1");
    env->SetCellValueByName(erow, "Recorder", "Observer 2");
    env->SetCellValueByName(erow, "Starboard", "Observer 3");
    env->SetCellValueByName(erow, "Notes", "Choppy conditions");
    check(wxFileExists(env->GetCsvPath()), "effort.csv exists");
    check(env->GetCellValueByName(erow, "Port") == "Observer 1",
          "Port Observer column works");
    check(env->GetCellValueByName(erow, "Beaufort") == "3",
          "Sea State is a plain number, no description text");
    check(env->GetCellValueByName(erow, "Effort") == "ON",
          "Effort Status column works on the merged tab");

    // --- New rows inherit the previous row's values (conditions --
    // and effort status -- usually haven't changed since the last
    // check) -- except Notes, which is always left blank (a one-off
    // observation, not something that should repeat automatically).
    env->AddRow();
    int erow2 = env->RowCount() - 1;
    check(env->GetCellValueByName(erow2, "Beaufort") == "3",
          "New row inherits Sea State from the previous row");
    check(env->GetCellValueByName(erow2, "Weather") == "Overcast",
          "New row inherits Weather from the previous row");
    check(env->GetCellValueByName(erow2, "Port") == "Observer 1",
          "New row inherits Port Observer from the previous row");
    check(env->GetCellValueByName(erow2, "Effort") == "ON",
          "New row inherits Effort Status from the previous row");
    check(env->GetCellValueByName(erow2, "Notes") == "",
          "New row does NOT inherit Notes (skipInherit)");

    // --- SegNo: blank while Effort is OFF, increments only on an
    // OFF->ON transition, stays the same across consecutive ON rows.
    // `env` already has 2 rows at this point (both Effort=ON, from
    // erow/erow2 above), so SegNo should already be "1" on both.
    check(env->GetCellValueByName(erow, "SegNo") == "1",
          "First Effort=ON row gets SegNo 1");
    check(env->GetCellValueByName(erow2, "SegNo") == "1",
          "Consecutive Effort=ON row keeps the same SegNo (1)");
    env->AddRow();
    int erow3 = env->RowCount() - 1;
    env->SetCellValueByName(erow3, "Effort", "OFF");
    check(env->GetCellValueByName(erow3, "SegNo") == "",
          "SegNo is blank while Effort is OFF");
    env->AddRow();
    int erow4 = env->RowCount() - 1;
    env->SetCellValueByName(erow4, "Effort", "ON");
    check(env->GetCellValueByName(erow4, "SegNo") == "2",
          "SegNo increments to 2 on the next OFF->ON transition");
    env->AddRow();
    int erow5 = env->RowCount() - 1;
    check(env->GetCellValueByName(erow5, "Effort") == "ON",
          "Effort ON is inherited onto a new row");
    check(env->GetCellValueByName(erow5, "SegNo") == "2",
          "SegNo correctly recomputes even when Effort arrives via "
          "inheritance rather than a direct edit");

    // --- Effort tab no longer has a Map/Chart column at all.
    check(env->GetCellValueByName(erow5, "Map") == "",
          "Effort tab has no Map column (removed per direct request)");

    // --- Position/height lookup, and Glare columns.
    env->SetCellValueByName(erow5, "Position", "Bow");
    check(env->GetCellValueByName(erow5, "Position") == "Bow",
          "Effort Position column works");
    env->SetCellValueByName(erow5, "Glare", "Severe");
    env->SetCellValueByName(erow5, "GlareBegin", "045");
    env->SetCellValueByName(erow5, "GlareEnd", "090");
    check(env->GetCellValueByName(erow5, "Glare") == "Severe",
          "Effort Glare column works");
    check(env->GetCellValueByName(erow5, "GlareBegin") == "045",
          "Effort GlareBegin column works");
    check(env->GetCellValueByName(erow5, "GlareEnd") == "090",
          "Effort GlareEnd column works");

    {
      PositionHeights freshPositions(plugin.GetDataDir());
      check(wxFileExists(freshPositions.GetPath()),
            "positions.csv created on first run");
      wxArrayString names = freshPositions.GetPositionNames();
      check(names.Index("Wheelhouse") != wxNOT_FOUND &&
                names.Index("Topdeck") != wxNOT_FOUND &&
                names.Index("Bow") != wxNOT_FOUND,
            "positions.csv includes the default Wheelhouse/Topdeck/Bow "
            "positions");
      check(std::abs(freshPositions.GetHeightFt("Wheelhouse", -1.0) - 20.0) <
                0.01,
            "Wheelhouse defaults to 20 ft");
      check(std::abs(freshPositions.GetHeightFt("Topdeck", -1.0) - 30.0) < 0.01,
            "Topdeck defaults to 30 ft");
      check(std::abs(freshPositions.GetHeightFt("Bow", -1.0) - 10.0) < 0.01,
            "Bow defaults to 10 ft");
      check(std::abs(freshPositions.GetHeightFt("NoSuchPosition", 99.0) -
                     99.0) < 0.01,
            "GetHeightFt falls back to the given default for an unknown "
            "position");
    }

    // --- ColumnDefinitions: created on first run, with entries for at
    // least a representative sample of columns across different tabs.
    {
      ColumnDefinitions freshDefs(plugin.GetDataDir());
      check(wxFileExists(freshDefs.GetPath()),
            "column_definitions.csv created on first run");
      check(!freshDefs.GetDefinition("Sightings", "SightNo").IsEmpty(),
            "column_definitions.csv has a definition for Sightings/SightNo");
      check(!freshDefs.GetDefinition("Effort", "SegNo").IsEmpty(),
            "column_definitions.csv has a definition for Effort/SegNo");
      check(!freshDefs.GetDefinition("Surfacings", "SurfLat").IsEmpty(),
            "column_definitions.csv has a definition for "
            "Surfacings/SurfLat");
      check(freshDefs.GetDefinition("NoSuchTab", "NoSuchColumn").IsEmpty(),
            "GetDefinition returns empty for an unknown tab/column");
    }

    // --- Events tab.
    DataTab* events = log->Events();
    events->AddRow();
    int evrow = events->RowCount() - 1;
    events->SetCellValueByName(evrow, "Event", "CTD cast");
    events->SetCellValueByName(evrow, "ID", "Cast #12");
    check(wxFileExists(events->GetCsvPath()), "events.csv exists");

    // --- Surfacings tab: every Sightings AddRow() automatically creates
    // a linked Surfacings row (see LogWindow::BuildTabs' wiring of
    // on_row_added_external), sharing Time/Bearing/BearUnit/Dist/
    // DistUnit/SightNo -- replacing an earlier version's manually-
    // triggered "+ Surf" button. Two Sightings rows were added earlier
    // in this test (`row` and `row2`), so two Surfacings rows should
    // already exist by this point.
    DataTab* surfacing = log->Surfacing();
    check(surfacing == nullptr,
          "Surfacings tab is currently disabled per direct request "
          "(pending more thought about its relationship to Sightings) "
          "-- Surfacing() correctly returns null rather than a tab that "
          "was never created");
    if (surfacing) {
      check(surfacing->RowCount() == 2,
            "Two Sightings AddRow() calls auto-created two Surfacings rows");
      check(surfacing->GetCellValueByName(0, "SightNo") == "1",
            "First auto-created Surfacings row has SightNo 1");
      check(surfacing->GetCellValueByName(1, "SightNo") == "2",
            "Second auto-created Surfacings row has SightNo 2");
      check(surfacing->GetCellValueByName(0, "BearingMag") == "",
            "Auto-created Surfacings row's BearingMag is blank -- the "
            "shared columns are copied at the moment the Sightings row is "
            "created, and Bearing/Dist are usually filled in on Sightings "
            "*after* that (this is a one-time copy, not an ongoing sync)");
      check(surfacing->GetCellValueByName(0, "Map") == "1",
            "Auto-created Surfacings row defaults to Map=on");
      check(surfacing->GetCellValueByName(0, "Event") == "Surfacing",
            "New Surfacings row defaults its Event to \"Surfacing\" (not "
            "\"First surfacing\")");
      check(surfacing->GetCellValueByName(0, "Lat") != "",
            "Surfacings' vessel Lat is visible again and auto-filled from "
            "the current GPS fix");

      int surfRow = 0;
      surfacing->SetCellValueByName(surfRow, "Event", "First surfacing");
      check(
          surfacing->GetCellValueByName(surfRow, "Event") == "First surfacing",
          "Surfacings Event column is editable");
      check(wxFileExists(surfacing->GetCsvPath()), "surfacings.csv exists");
      {
        auto pts = surfacing->GetChartedPoints();
        check(pts.size() == 2,
              "Both charted Surfacings rows appear in "
              "GetChartedPoints");
      }

      // --- Bearing is always magnetic now (no BearUnit column -- relative
      // bearings were removed since COG can reflect drift rather than
      // actual vessel heading, with no real heading source available to
      // use instead). Dist supports "m" (meters, the default), "nm", and
      // "reticles" (horizon-based binocular rangefinding, using the
      // current observer height).
      {
        surfacing->SetCellValueByName(surfRow, "DistUnit", "m");
        surfacing->SetCellValueByName(surfRow, "BearingMag", "090");
        surfacing->SetCellValueByName(surfRow, "Dist", "500");
        double surfLat1 = 0, surfLon1 = 0;
        surfacing->GetCellValueByName(surfRow, "SurfLat").ToDouble(&surfLat1);
        surfacing->GetCellValueByName(surfRow, "SurfLon").ToDouble(&surfLon1);
        check(surfLat1 != 0.0 || surfLon1 != 0.0,
              "Magnetic bearing + meters distance computes a real position");

        // Switching Dist to nm for the *same* numeric distance value
        // should move the computed position much farther (1 nm = 1852 m,
        // roughly 3.7x farther for the same "500").
        surfacing->SetCellValueByName(surfRow, "DistUnit", "nm");
        double surfLat2 = 0, surfLon2 = 0;
        surfacing->GetCellValueByName(surfRow, "SurfLat").ToDouble(&surfLat2);
        surfacing->GetCellValueByName(surfRow, "SurfLon").ToDouble(&surfLon2);
        check(std::abs(surfLat2 - surfLat1) > 0.001 ||
                  std::abs(surfLon2 - surfLon1) > 0.001,
              "Changing DistUnit from m to nm (same numeric value) "
              "recomputes to a meaningfully different position");

        // "reticles": distance depends on observer height (set via the
        // Effort tab's Position, polled into DataTab::SetObserverHeightFt()
        // once a second -- set directly here to make the test
        // deterministic rather than waiting on the timer).
        surfacing->SetObserverHeightFt(20.0);
        surfacing->SetCellValueByName(surfRow, "DistUnit", "reticles");
        surfacing->SetCellValueByName(surfRow, "Dist", "10");
        double surfLat3 = 0, surfLon3 = 0;
        surfacing->GetCellValueByName(surfRow, "SurfLat").ToDouble(&surfLat3);
        surfacing->GetCellValueByName(surfRow, "SurfLon").ToDouble(&surfLon3);
        check(surfLat3 != 0.0 || surfLon3 != 0.0,
              "reticles DistUnit computes a real position using observer "
              "height");
        check(std::abs(surfLat3 - surfLat2) > 0.0001 ||
                  std::abs(surfLon3 - surfLon2) > 0.0001,
              "reticles distance genuinely differs from the nm-based one "
              "(not silently falling back to treating the raw number as "
              "nm)");
      }
    }  // if (surfacing)

    // --- Unlimited undo/redo: a full stack, not just the last change.
    {
      DataTab* events = log->Events();
      int beforeUndo = events->RowCount();
      events->AddRow();
      int firstRow = events->RowCount() - 1;
      events->SetCellValueByName(firstRow, "ID", "Cast A");
      events->AddRow();
      check(events->RowCount() == beforeUndo + 2,
            "Two rows added, plus an edit, before testing undo");

      // Step back through three separate changes (add, edit, add), not
      // just the one most recent -- confirms this is an actual stack,
      // not a single slot that stops working after one Undo().
      check(events->Undo(), "Undo() #1 (undoes the second AddRow)");
      check(events->RowCount() == beforeUndo + 1,
            "First Undo() reverts the second row addition");
      check(events->Undo(), "Undo() #2 (undoes the ID edit)");
      check(events->GetCellValueByName(firstRow, "ID") == "",
            "Second Undo() reverts the ID edit");
      check(events->Undo(), "Undo() #3 (undoes the first AddRow)");
      check(events->RowCount() == beforeUndo,
            "Third Undo() reverts the first row addition, back to the "
            "original state -- three separate undos in a row all "
            "actually doing something confirms this is a real stack, "
            "not just a single slot that would've stopped working "
            "after the first one");

      // Redo should be able to step forward through the same three
      // changes, in order, not just the one most recent.
      check(events->Redo(), "Redo() #1 (re-does the first AddRow)");
      check(events->RowCount() == beforeUndo + 1,
            "First Redo() restores the first row addition");
      check(events->Redo(), "Redo() #2 (re-does the ID edit)");
      check(events->GetCellValueByName(firstRow, "ID") == "Cast A",
            "Second Redo() restores the ID edit");
      check(events->Redo(), "Redo() #3 (re-does the second AddRow)");
      check(events->RowCount() == beforeUndo + 2,
            "Third Redo() restores the second row addition -- fully "
            "back to where undo started, confirming redo is also a "
            "real multi-step stack");

      // A new edit after undoing should clear the redo stack (standard
      // undo/redo convention) -- checked by undoing once (redo now
      // available), making a new edit, and confirming that specific
      // redo opportunity is gone. Not asserting redo is *completely*
      // exhausted (unlike undo, which has unlimited real history behind
      // it from earlier in this test, redo before this point is
      // legitimately empty, so this one comparison is meaningful
      // without needing to fully unwind the stack first).
      events->Undo();
      wxString redoTargetId = events->GetCellValueByName(firstRow, "ID");
      events->AddRow();  // a genuinely new edit
      bool redoAfterNewEdit = events->Redo();
      check(!redoAfterNewEdit ||
                events->GetCellValueByName(firstRow, "ID") != redoTargetId,
            "Making a new edit after undoing clears that specific redo "
            "opportunity");
    }

    // --- Per-tab marker shape/color (DisplaySettings) -- distinct from
    // the earlier, removed per-*row* Mark Type dropdown. A single
    // shape/color per tab, read once per repaint rather than looked up
    // per point.
    {
      DisplaySettings* ds = plugin.GetDisplaySettings();
      check(ds != nullptr, "DisplaySettings exists");
      check(ds->MarkerShape("Sightings", "Diamond") == "Diamond",
            "Sightings marker shape defaults to the fallback (Diamond)");
      ds->SetMarkerShape("Sightings", "Star");
      check(ds->MarkerShape("Sightings", "Diamond") == "Star",
            "Sightings marker shape is changeable");
      ds->SetMarkerColor("Events", wxColour(10, 20, 30));
      wxColour c = ds->MarkerColor("Events", wxColour(0, 0, 0));
      check(c.Red() == 10 && c.Green() == 20 && c.Blue() == 30,
            "Events marker color is changeable and round-trips exactly");

      DisplaySettings reloadedDs(plugin.GetDataDir());
      check(reloadedDs.MarkerShape("Sightings", "Diamond") == "Star",
            "Marker shape change persists across reload");
      wxColour c2 = reloadedDs.MarkerColor("Events", wxColour(0, 0, 0));
      check(c2.Red() == 10 && c2.Green() == 20 && c2.Blue() == 30,
            "Marker color change persists across reload");
      check(reloadedDs.GridFontSize() > 0,
            "GridFontSize has a sensible positive default");
    }

    check(sightings->GetCellValueByName(row, "Map") == "1",
          "New Sightings row defaults to Chart=on");
    check(events->GetCellValueByName(evrow, "Map") == "1",
          "New Events row defaults to Chart=on");

    // --- Multi-select Behaviors column.
    sightings->SetCellValueByName(row, "Behavs", "Feeding, Traveling");
    check(sightings->GetCellValueByName(row, "Behavs") == "Feeding, Traveling",
          "Behaviors column stores multiple comma-separated values");

    // --- Keyboard shortcuts now live in their own shortcuts.csv, separate
    // from dropdowns.csv.
    {
      ShortcutsFile freshShortcuts(plugin.GetDataDir());
      auto shortcuts = freshShortcuts.Get();
      check(shortcuts.at("Cmd+Shift+S") == "AddSighting",
            "Cmd+Shift+S shortcut maps to AddSighting");
      check(shortcuts.at("Cmd+Shift+E") == "AddEnvironmental",
            "Cmd+Shift+E shortcut maps to AddEnvironmental");
      check(shortcuts.at("Cmd+Shift+R") ==
                "AddSighting:Species=North Atlantic right whale;Img=Photos",
            "Cmd+Shift+R shortcut uses the extended Field=Value syntax "
            "and now also sets Img=Photos");
    }

    // --- Extended shortcut syntax: RunShortcutAction is private, but the
    // underlying field-population behavior is exercised the same way it
    // would be triggered (a row is added, then specific columns are
    // pre-filled) -- confirmed to actually work end-to-end via a live
    // synthetic-keystroke test during development (see project history),
    // not just this in-process check.
    {
      int beforeCount = sightings->RowCount();
      sightings->AddRow();
      int newRow = sightings->RowCount() - 1;
      sightings->SetCellValueByName(newRow, "Species",
                                    "North Atlantic right whale");
      check(sightings->RowCount() == beforeCount + 1,
            "Shortcut-style add-then-populate adds exactly one row");
      check(sightings->GetCellValueByName(newRow, "Species") ==
                "North Atlantic right whale",
            "Shortcut-style add-then-populate sets the species field");
    }

    // --- species.csv (replacing dropdowns.csv's "species" category)
    // still provides the survey-specific species list, including its
    // color/species_code columns.
    {
      CategoryConfigFile freshSpecies(plugin.GetDataDir(), "species.csv",
                                      {"color", "species_code"}, {});
      auto species = freshSpecies.Names();
      check(species.Index("Humpback whale") != wxNOT_FOUND,
            "species.csv includes Humpback whale as a species option");
      check(freshSpecies.GetField("Humpback whale", "color") == "Orange",
            "species.csv's default Humpback whale color loads correctly");
      check(freshSpecies.GetField("Humpback whale", "species_code") == "Mn",
            "species.csv's default Humpback whale species_code loads "
            "correctly");
    }

    // --- GPS watchdog: fresh fix means "not stale".
    check(plugin.SecondsSinceLastFix() < 5,
          "SecondsSinceLastFix small right after a fix");

    // --- Position-unchanged detection: a second, distinct signal from
    // "no fix at all" -- fixes arrive on schedule, but the value itself
    // hasn't moved. Right after a fix this should read as "just
    // changed" (a low number).
    check(plugin.SecondsSincePositionChanged() < 5,
          "SecondsSincePositionChanged is small right after the first fix");
    {
      PlugIn_Position_Fix movedPos;
      movedPos.Lat = 42.40;   // ~0.05 degrees away -- well past the
      movedPos.Lon = -70.05;  // ~5m "meaningful change" threshold
      movedPos.Sog = 6.0;
      movedPos.FixTime = time(nullptr);
      plugin.SetPositionFix(movedPos);
      check(plugin.SecondsSincePositionChanged() < 5,
            "Moving to a meaningfully different position resets the "
            "change clock");
    }

    // --- Internal trackline recording. Tracking now starts OFF by
    // default (verified right after Init(), above) -- it only turns on
    // once "Start New Survey" is used for the first time, or Effort
    // Status is set to ON (verified separately, in the Effort tab
    // section above), so a track is always associated with either a
    // specific survey or an active effort period, not accumulating
    // before either has begun.
    TrackRecorder* track = plugin.GetTrackRecorder();
    check(wxFileExists(track->GetCsvPath()), "track.csv exists");
    // Already turned on by the earlier "Effort Status = ON" step in
    // this same test run -- see the dedicated check right after that
    // edit, above, for the actual coupling behavior being verified.
    check(track->IsEnabled(),
          "Tracking is already on from the earlier Effort Status=ON step");

    // --- Tracking tab settings: TrackRecorder's runtime state is
    // independently adjustable (and is what the Tracking tab's controls
    // actually change).
    track->RecordFix(42.35, -70.05, 6.5, 180.0, wxDateTime::Now(), "OFF", "");
    check(track->GetPoints().size() >= 1,
          "RecordFix actually records a point while enabled");
    track->SetIntervalSeconds(45);
    check(track->GetIntervalSeconds() == 45,
          "TrackRecorder's interval is adjustable at runtime");
    track->SetEnabled(false);
    check(!track->IsEnabled(), "Tracking can be disabled again at runtime");
    {
      // Recording should be a no-op while disabled.
      size_t before = track->GetPoints().size();
      track->RecordFix(43.0, -71.0, 5.0, 0.0,
                       wxDateTime::Now() + wxTimeSpan::Seconds(100), "OFF", "");
      check(track->GetPoints().size() == before,
            "RecordFix is a no-op while tracking is disabled");
    }
    track->SetEnabled(true);

    {
      TrackingSettings freshTrackingSettings(plugin.GetDataDir());
      check(wxFileExists(freshTrackingSettings.GetPath()),
            "tracking.csv created on first run");
      freshTrackingSettings.SetIntervalSeconds(20);
      TrackingSettings reloaded(plugin.GetDataDir());
      check(reloaded.IntervalSeconds() == 20,
            "TrackingSettings interval change persists across reload");
      freshTrackingSettings.SetEnabled(false);
      TrackingSettings reloaded2(plugin.GetDataDir());
      check(!reloaded2.Enabled(),
            "TrackingSettings enabled change persists across reload");
      freshTrackingSettings.SetEnabled(true);  // restore for the rest of
                                               // this test run
    }

    // --- Chart overlay rendering: exercise the actual RenderOverlay path
    // (wxDC-based) with a fake viewport, confirming it runs without
    // crashing given real charted points (with labels) and a recorded
    // trackline. The OpenGL path isn't exercised here since it needs a
    // live GL context this harness doesn't set up -- see README.
    {
      wxBitmap bmp(400, 300);
      wxMemoryDC memDc(bmp);
      PlugIn_ViewPort vp;
      vp.clat = 42.35;
      vp.clon = -70.05;
      vp.view_scale_ppm = 5000.0;
      vp.pix_width = 400;
      vp.pix_height = 300;
      vp.bValid = true;
      bool renderOk = plugin.RenderOverlay(memDc, &vp);
      check(renderOk, "RenderOverlay runs without crashing");
    }

    // --- Column resize-to-fit: resizing the actual LogWindow frame
    // should cascade down through the notebook/panel/grid sizers and
    // change column widths proportionally. The window needs to actually
    // be Show()n first -- on at least GTK, an unshown/unrealized
    // top-level window's children don't reliably get real size
    // allocations from Layout() alone. ForceResizeColumnsToFit() is
    // called explicitly afterward for a deterministic result, since this
    // self-test has no real running event loop to reliably drain every
    // cascading layout/size event a single wxYield() might leave pending.
    // --- Column resize-to-fit: full end-to-end resize behavior (does
    // dragging the actual window edge change column widths?) genuinely
    // needs a live, running wx main loop -- LogWindow debounces the
    // recalculation behind a short timer (confirmed necessary via direct
    // testing: even wxYield()/CallAfter() weren't reliably enough to see
    // a settled post-resize layout immediately, but a real timer, fired
    // by a real running event loop, is), and this --selftest mode
    // deliberately never enters MainLoop() (it runs synchronously and
    // exits). So this checks the resize *logic* directly instead --
    // ForceResizeColumnsToFit() genuinely recomputes proportionally
    // against whatever width the grid currently reports, without
    // crashing -- and the full behavior (drag the window, watch columns
    // actually rescale) is exercised via a separate live-GUI test
    // during development, not by this fast/scriptable suite.
    {
      auto widthsBefore = sightings->GetColumnWidths();
      check(!widthsBefore.empty(), "GetColumnWidths returns real data");
      sightings->ForceResizeColumnsToFit();  // should not crash
      auto widthsAfter = sightings->GetColumnWidths();
      check(widthsAfter.size() == widthsBefore.size(),
            "ForceResizeColumnsToFit preserves column count");
    }

    // --- Environmental reminder interval: now controlled entirely from
    // outside (LogWindow's status bar spinctrl), no more in-tab UI.
    {
      int before = env->GetReminderIntervalMinutes();
      check(before > 0,
            "GetReminderIntervalMinutes returns a positive default");
      env->SetReminderIntervalMinutes(45);
      check(env->GetReminderIntervalMinutes() == 45,
            "SetReminderIntervalMinutes updates the interval");
      check(!env->IsReminderOverdue(),
            "Reminder isn't overdue immediately after changing the interval");
    }

    // --- Layout presets: shouldn't crash even when OpenCPN's own window
    // can't be found (as in this harness, where GetOCPNCanvasWindow()
    // returns null) -- should just fall back to resizing this window
    // alone.
    {
      log->ApplyLayoutPreset(LayoutPreset::Overlay);
      log->ApplyLayoutPreset(LayoutPreset::SplitVertical);
      log->ApplyLayoutPreset(LayoutPreset::SplitHorizontal);
      check(true,
            "ApplyLayoutPreset runs for all three presets without crashing");
    }

    // --- Clear Survey Data (DataTab::ClearAllData): genuinely wipes a
    // tab's rows *and* rewrites its CSV file with just the header --
    // distinct from StartNewFile(), which switches to a new file and
    // leaves the old one alone. Tested on Events specifically since
    // (with Surfacings currently disabled) it's the tab least depended
    // on by anything after this point -- ExportCopyTo() afterward just
    // copies whatever's there, empty or not.
    {
      check(events->RowCount() > 0, "Events has rows before Clear Survey Data");
      events->ClearAllData();
      check(events->RowCount() == 0, "ClearAllData empties the tab's rows");
      auto rows = CsvUtils::ReadAll(events->GetCsvPath());
      check(rows.size() == 1,
            "ClearAllData rewrites the CSV with just the header row");
    }

    wxString exportDir = wxFileName::GetTempDir() +
                         wxFileName::GetPathSeparator() +
                         "whale_export_selftest";
    sightings->ExportCopyTo(exportDir);
    env->ExportCopyTo(exportDir);
    events->ExportCopyTo(exportDir);
    if (plugin.GetTrackRecorder())
      plugin.GetTrackRecorder()->ExportCopyTo(exportDir);
    check(wxFileExists(wxFileName(exportDir, "sightings.csv").GetFullPath()),
          "Export wrote sightings.csv");

    log->ExportMergedCsv(exportDir);
    {
      wxString mergedPath = wxFileName(exportDir, "merged.csv").GetFullPath();
      check(wxFileExists(mergedPath),
            "ExportMergedCsv wrote merged.csv "
            "(no survey prefix set yet in this "
            "test, so no prefix in the name)");
      auto rows = CsvUtils::ReadAll(mergedPath);
      check(rows.size() > 1, "merged.csv has data rows, not just a header");
      check(
          !rows.empty() && rows[0][0] == "Timestamp" && rows[0][1] == "Source",
          "merged.csv starts with Timestamp,Source columns");
      // Rows should be chronologically sorted.
      bool sorted = true;
      for (size_t r = 2; r < rows.size(); r++) {
        if (rows[r][0] < rows[r - 1][0]) {
          sorted = false;
          break;
        }
      }
      check(sorted, "merged.csv rows are in chronological order");
      // At least one row from each source should be present.
      bool haveSightings = false, haveEffort = false, haveTrack = false;
      for (size_t r = 1; r < rows.size(); r++) {
        if (rows[r].size() < 2) continue;
        if (rows[r][1] == "Sightings") haveSightings = true;
        if (rows[r][1] == "Effort") haveEffort = true;
        if (rows[r][1] == "Track") haveTrack = true;
      }
      check(haveSightings, "merged.csv includes at least one Sightings row");
      check(haveEffort, "merged.csv includes at least one Effort row");
      check(haveTrack, "merged.csv includes at least one Track row");
    }

    // --- Summary tab: add controlled rows to verify (a) the Sightings
    // Breakdown includes every sighting regardless of SpecConf/NumConf
    // (an earlier version filtered to Probable/Definite only; removed
    // per direct request), and (b) a blank Num/NumCalf is treated as
    // genuinely unknown, not silently added as zero.
    {
      int row1 = sightings->RowCount();
      sightings->AddRow();
      sightings->SetCellValueByName(row1, "Species", "Fin whale");
      sightings->SetCellValueByName(row1, "SpecConf", "Definite");
      sightings->SetCellValueByName(row1, "Num", "4");
      sightings->SetCellValueByName(row1, "NumConf", "Probable");
      sightings->SetCellValueByName(row1, "NumCalf", "1");

      int row2 = sightings->RowCount();
      sightings->AddRow();
      sightings->SetCellValueByName(row2, "Species", "Fin whale");
      sightings->SetCellValueByName(row2, "SpecConf",
                                    "Possible");  // low
                                                  // confidence
                                                  // -- still
                                                  // counted now
      sightings->SetCellValueByName(row2, "Num", "100");
      sightings->SetCellValueByName(row2, "NumConf", "Possible");
      sightings->SetCellValueByName(row2, "NumCalf", "0");

      // Deliberately leaves Num/NumCalf blank -- should count as a
      // sighting, but contribute "unknown," not zero, to the totals.
      int row3 = sightings->RowCount();
      sightings->AddRow();
      sightings->SetCellValueByName(row3, "Species", "Fin whale");
      sightings->SetCellValueByName(row3, "SpecConf", "Definite");
      sightings->SetCellValueByName(row3, "NumConf", "Definite");

      SurveySummary summary = log->ComputeSummary();
      const SurveySummary::SpeciesRow* finWhale = nullptr;
      for (const auto& sp : summary.speciesBreakdown) {
        if (sp.species == "Fin whale") finWhale = &sp;
      }
      check(finWhale != nullptr,
            "Summary species breakdown includes Fin whale");
      if (finWhale) {
        check(finWhale->sightings == 3,
              "All 3 Fin whale rows counted regardless of SpecConf/"
              "NumConf (the filter was removed per direct request)");
        check(finWhale->individuals == 104,
              "Individuals summed from the 2 rows that had a value "
              "(4+100), not from the 3rd (blank)");
        check(finWhale->calves == 1,
              "Calves summed from the 2 rows that had a value (1+0), "
              "not from the 3rd (blank)");
        check(finWhale->hasUnknownIndividuals,
              "hasUnknownIndividuals is set, since one row's Num was "
              "blank -- flags the sum as a floor, not silently treating "
              "the blank as zero");
        check(finWhale->hasUnknownCalves,
              "hasUnknownCalves is set for the same reason (one row's "
              "NumCalf was blank)");
      }
      check(summary.numSightings >= 3,
            "Total sightings count includes all rows regardless of "
            "confidence");

      log->ExportSummaryCsv(exportDir);
      wxString summaryPath = wxFileName(exportDir, "summary.csv").GetFullPath();
      check(wxFileExists(summaryPath), "ExportSummaryCsv wrote summary.csv");
      auto summaryRows = CsvUtils::ReadAll(summaryPath);
      bool foundFinWhaleRow = false;
      bool foundNAMarker = false;
      for (const auto& row : summaryRows) {
        if (!row.empty() && row[0] == "Fin whale") {
          foundFinWhaleRow = true;
          for (const auto& cell : row) {
            if (cell.Contains("NA")) foundNAMarker = true;
          }
        }
      }
      check(foundFinWhaleRow,
            "summary.csv includes the Fin whale species-breakdown row");
      check(foundNAMarker,
            "summary.csv's Fin whale row shows an NA marker, reflecting "
            "the blank Num/NumCalf row");

      // --- Pure-NA case: a species where NOT ONE sighting has a known
      // NumCalf -- should show "NA", not "0", since 0 would falsely
      // claim a confirmed count of zero calves rather than "unknown."
      int rightWhaleRow = sightings->RowCount();
      sightings->AddRow();
      sightings->SetCellValueByName(rightWhaleRow, "Species", "Right whale");
      sightings->SetCellValueByName(rightWhaleRow, "Num", "2");
      // NumCalf deliberately left blank.

      SurveySummary summaryNA = log->ComputeSummary();
      const SurveySummary::SpeciesRow* rightWhale = nullptr;
      for (const auto& sp : summaryNA.speciesBreakdown) {
        if (sp.species == "Right whale") rightWhale = &sp;
      }
      check(rightWhale != nullptr,
            "Summary species breakdown includes Right whale");
      if (rightWhale) {
        check(rightWhale->knownCalvesCount == 0,
              "Right whale has zero sightings with a known NumCalf "
              "value -- not one, not even a confirmed zero");
        check(rightWhale->calves == 0,
              "The raw sum is 0 (nothing to add), but display/export "
              "should show NA, not 0, for this case specifically");
        check(rightWhale->hasUnknownCalves, "hasUnknownCalves is set");
        check(rightWhale->knownIndividualsCount == 1 &&
                  rightWhale->individuals == 2,
              "Individuals, unlike calves, DO have a known value here "
              "(Num=2), so that one should show as a plain 2, not NA");
      }

      log->ExportSummaryCsv(exportDir);
      auto summaryRowsNA = CsvUtils::ReadAll(summaryPath);
      bool foundRightWhaleNA = false;
      for (const auto& row : summaryRowsNA) {
        if (row.size() >= 2 && row[0] == "Right whale") {
          // Column 1 is "Sightings,Individuals,Calves" joined with
          // commas as a single CSV field's value (see ExportSummaryCsv) --
          // check that it contains "NA" (for calves) but also "2" (for
          // individuals), confirming the two are handled independently.
          if (row[1].Contains("NA") && row[1].Contains("2"))
            foundRightWhaleNA = true;
        }
      }
      check(foundRightWhaleNA,
            "summary.csv's Right whale row shows NA for calves "
            "specifically, while still showing the known individuals "
            "count (2)");

      // --- Events Breakdown: add controlled rows to verify the tally.
      int evRow2 = events->RowCount();
      events->AddRow();
      events->SetCellValueByName(evRow2, "Event", "CTD cast");
      int evRow3 = events->RowCount();
      events->AddRow();
      events->SetCellValueByName(evRow3, "Event", "Drone flight");

      SurveySummary summary2 = log->ComputeSummary();
      int ctdCount = -1, droneCount = -1;
      for (const auto& ec : summary2.eventBreakdown) {
        if (ec.eventType == "CTD cast") ctdCount = ec.count;
        if (ec.eventType == "Drone flight") droneCount = ec.count;
      }
      check(ctdCount == 1,
            "Events breakdown correctly tallies the 1 CTD cast row "
            "(an earlier one from before this test's ClearAllData() "
            "call is gone, as expected)");
      check(droneCount == 1,
            "Events breakdown correctly tallies 1 Drone flight row");

      log->ExportSummaryCsv(exportDir);
      auto summaryRows2 = CsvUtils::ReadAll(summaryPath);
      bool foundEventsHeader = false, foundCtdRow = false;
      for (const auto& row : summaryRows2) {
        if (!row.empty() && row[0] == "Events Breakdown")
          foundEventsHeader = true;
        if (!row.empty() && row[0] == "CTD cast") foundCtdRow = true;
      }
      check(foundEventsHeader,
            "summary.csv includes an Events Breakdown section header");
      check(foundCtdRow, "summary.csv includes the CTD cast tally row");
    }

    // --- Reload from disk to make sure persistence round-trips.
    plugin.DeInit();
    SpotterPlugin plugin2(nullptr);
    plugin2.Init();
    LogWindow* log2 = plugin2.GetLogWindow();
    check(log2->Sightings()->RowCount() >= 1,
          "Sightings row persisted across restart");
    check(log2->Environmental()->RowCount() == 5,
          "Effort/Environmental rows persisted across restart");

    // --- Start New Survey: clears the active tables and switches to
    // newly-prefixed files, *without* deleting the previous files. Also
    // resets the trackline so it doesn't draw a connecting line back to
    // wherever the vessel was at the end of the previous survey.
    wxString oldSightingsPath = log2->Sightings()->GetCsvPath();
    check(wxFileExists(oldSightingsPath),
          "Pre-new-survey sightings file exists");
    check(log2->Sightings()->RowCount() > 0,
          "Sightings has rows before Start New Survey");

    TrackRecorder* track2 = plugin2.GetTrackRecorder();
    wxString oldTrackPath = track2->GetCsvPath();
    size_t oldTrackPointCount = track2->GetPoints().size();
    check(oldTrackPointCount > 0,
          "Trackline has points before Start New Survey");

    wxString prefix = log2->StartNewSurvey("Selftest Survey");

    check(log2->CurrentSurveyName() == "Selftest Survey",
          "StartNewSurvey updates the current survey name");

    check(log2->Sightings()->RowCount() == 0,
          "Sightings empty immediately after Start New Survey");
    check(wxFileExists(oldSightingsPath),
          "Previous survey's sightings file is NOT deleted");
    wxString newSightingsPath = log2->Sightings()->GetCsvPath();
    check(newSightingsPath != oldSightingsPath,
          "Sightings now points at a new, differently-named file");
    check(newSightingsPath.Contains(prefix),
          "New sightings file name includes the survey prefix");
    check(wxFileExists(newSightingsPath),
          "New (empty) sightings file was created");

    check(track2->GetCsvPath().Contains(prefix),
          "Track file also gets renamed to include the survey prefix "
          "on Start New Survey");
    check(track2->GetCsvPath() != oldTrackPath,
          "Track file path actually changed (not still pointing at the "
          "pre-survey track.csv)");
    {
      // Directly checks what "Export Data..." actually produces once a
      // survey has been started -- the exported track file's name
      // should carry the same prefix as every other exported file.
      wxString postSurveyExportDir = wxFileName::GetTempDir() +
                                     wxFileName::GetPathSeparator() +
                                     "whale_export_selftest_post_survey";
      track2->ExportCopyTo(postSurveyExportDir);
      wxFileName expectedTrackExport(postSurveyExportDir,
                                     prefix + "_track.csv");
      check(wxFileExists(expectedTrackExport.GetFullPath()),
            "Exported track file is named <prefix>_track.csv, matching "
            "every other exported file's naming convention");
      auto trackRows = CsvUtils::ReadAll(expectedTrackExport.GetFullPath());
      check(!trackRows.empty() && trackRows[0].back() == "nm_since_prev",
            "Exported track file has an nm_since_prev column as the "
            "last header entry");
      if (trackRows.size() > 2) {
        check(trackRows[1].back().IsEmpty(),
              "First track row has a blank nm_since_prev (no previous "
              "point to measure from)");
        double nm = 0.0;
        check(trackRows[2].back().ToDouble(&nm) && nm > 0.0,
              "Second track row has a positive nm_since_prev distance");
      }
    }
    {
      auto rows = CsvUtils::ReadAll(newSightingsPath);
      check(rows.size() == 1, "New sightings file has just the header row");
    }

    check(track2->GetPoints().empty(),
          "Trackline is cleared (no connecting line to the old survey)");
    check(track2->GetCsvPath() != oldTrackPath,
          "Trackline now points at a new, differently-named file");
    check(track2->GetCsvPath().Contains(prefix),
          "New track file name includes the survey prefix");
    check(wxFileExists(oldTrackPath),
          "Previous survey's track file is NOT deleted");

    log2->Sightings()->AddRow();
    check(log2->Sightings()->RowCount() == 1,
          "Can add a fresh row after Start New Survey");

    plugin2.DeInit();

    // --- One more restart to confirm the new survey's file (and its
    // fresh row) persisted correctly.
    SpotterPlugin plugin3(nullptr);
    plugin3.Init();
    check(plugin3.GetLogWindow()->Sightings()->GetCsvPath() == newSightingsPath,
          "New survey's file path persisted across restart");
    check(plugin3.GetLogWindow()->Sightings()->RowCount() == 1,
          "New survey's row persisted across restart");
    check(plugin3.GetTrackRecorder()->GetCsvPath().Contains(prefix),
          "Track recorder's file path reflects the current survey's "
          "prefix immediately after Init() on a fresh (simulated "
          "restart) plugin instance -- confirmed as a real, reported "
          "bug without this: the track recorder used to always start "
          "out pointed at the plain, unprefixed track.csv, and nothing "
          "else automatically restored its prefix on startup");
    check(plugin3.GetLogWindow()->CurrentSurveyName() == "Selftest Survey",
          "Survey name persisted across restart");

    // --- Load Survey: prefix discovery, and correctly switching a tab's
    // data to a different, already-existing survey's file.
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      wxString dataDir = wxFileName(log3->Sightings()->GetCsvPath()).GetPath();

      wxArrayString prefixes = log3->FindSurveyPrefixesInDir(dataDir);
      check(!prefixes.IsEmpty(),
            "FindSurveyPrefixesInDir finds at least the current survey");
      wxString currentPrefix = log3->CurrentFilePrefix();
      check(prefixes.Index(currentPrefix) != wxNOT_FOUND,
            "FindSurveyPrefixesInDir's results include the current "
            "survey's own prefix");

      // Start a second, distinct survey, so there are two to
      // distinguish between.
      wxString secondPrefix = log3->StartNewSurvey("Second Survey");
      log3->Sightings()->AddRow();
      log3->Sightings()->SetCellValueByName(0, "Species",
                                            "Second survey species");

      wxArrayString prefixesAfter = log3->FindSurveyPrefixesInDir(dataDir);
      check(prefixesAfter.Index(currentPrefix) != wxNOT_FOUND &&
                prefixesAfter.Index(secondPrefix) != wxNOT_FOUND,
            "FindSurveyPrefixesInDir finds both surveys now that a "
            "second one exists");

      // Switch back to the *first* survey via LoadSurvey() directly
      // (the same method OnLoadSurveyClicked uses internally) and
      // confirm its original data comes back, not the second survey's.
      log3->Sightings()->LoadSurvey(currentPrefix);
      check(log3->Sightings()->RowCount() == 1,
            "LoadSurvey() restores the first survey's row count");
      check(log3->Sightings()->GetCellValueByName(0, "Species") !=
                "Second survey species",
            "LoadSurvey() restores the first survey's actual data, not "
            "the second survey's");

      // And switching to the second survey's prefix brings *that* data
      // back.
      log3->Sightings()->LoadSurvey(secondPrefix);
      check(log3->Sightings()->GetCellValueByName(0, "Species") ==
                "Second survey species",
            "LoadSurvey() correctly switches to the second survey's data "
            "when given its prefix");

      // --- Survey name after loading: the prefix *is* the survey name,
      // unchanged -- no date-stripping. An earlier version of this
      // plugin auto-prepended today's date to every new survey's
      // prefix, and a ParseSurveyNameFromPrefix() helper existed to
      // recover the real name by stripping that back off when loading;
      // that auto-prepending is long gone, so a survey whose name
      // genuinely happens to look date-shaped (typed by the user, not
      // auto-added) should come through exactly as typed, not silently
      // stripped.
      log3->ApplyLoadedSurveyPrefix("2026-07-23_test3");
      check(log3->CurrentSurveyName() == "2026-07-23_test3",
            "Loading a survey whose prefix genuinely looks date-shaped "
            "keeps the survey name exactly as-is, not stripped down to "
            "just the part after the date");
      // Restore state for whatever runs after this block.
      log3->ApplyLoadedSurveyPrefix(secondPrefix);
      check(log3->CurrentSurveyName() == secondPrefix,
            "Loading a survey's prefix sets the survey name to match "
            "it exactly, in the ordinary (non-date-shaped) case too");
    }

    // --- Order-sensitive dropdowns (Glare, SpecConf, NumConf) should
    // keep their defined severity/confidence order, not get
    // alphabetized -- confirmed as a real, reported bug for Glare
    // specifically (was coming out as Mild/Moderate/None/Severe).
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      auto checkComboOrder = [&](DataTab* tab, const wxString& colName,
                                 const std::vector<wxString>& expected,
                                 const wxString& label) {
        wxGrid* grid = nullptr;
        std::function<void(wxWindow*)> findGrid = [&](wxWindow* w) {
          for (auto child : w->GetChildren()) {
            wxGrid* g = dynamic_cast<wxGrid*>(child);
            if (g) {
              grid = g;
              return;
            }
            findGrid(child);
            if (grid) return;
          }
        };
        findGrid(tab->GetPanel());
        if (!grid) {
          check(false, label + ": grid not found");
          return;
        }
        int col = -1;
        for (int c = 0; c < grid->GetNumberCols(); c++) {
          if (grid->GetColLabelValue(c) == colName) {
            col = c;
            break;
          }
        }
        if (col < 0) {
          check(false, label + ": column not found");
          return;
        }
        if (tab->RowCount() == 0) tab->AddRow();
        grid->SetGridCursor(0, col);
        grid->EnableCellEditControl(true);
        wxGridCellEditor* editor = grid->GetCellEditor(0, col);
        wxComboBox* combo = dynamic_cast<wxComboBox*>(editor->GetControl());
        wxArrayString items = combo ? combo->GetStrings() : wxArrayString();
        bool matches = combo && items.size() == expected.size();
        if (matches) {
          for (size_t i = 0; i < expected.size(); i++) {
            if (items[i] != expected[i]) matches = false;
          }
        }
        check(matches, label);
        editor->DecRef();
        grid->HideCellEditControl();
      };

      checkComboOrder(log3->Environmental(), "Glare",
                      {"None", "Mild", "Moderate", "Severe"},
                      "Glare dropdown keeps None/Mild/Moderate/Severe "
                      "order, not alphabetized");
      checkComboOrder(log3->Sightings(), "SpecConf",
                      {"Definite", "Probable", "Possible"},
                      "SpecConf dropdown keeps Definite/Probable/"
                      "Possible order, not alphabetized");
      checkComboOrder(log3->Sightings(), "NumConf",
                      {"Definite", "Probable", "Possible", "At Least"},
                      "NumConf dropdown keeps its defined order, not "
                      "alphabetized");
    }

    // --- Column widths should widen (not stay at the same, smaller-
    // font-tuned base width) when the grid font size increases --
    // confirmed as a real, reported bug: Time/Lat/Lon text was getting
    // clipped at a 14pt grid font size, since SetGridFontSize() used to
    // just reset every column to its static base width regardless of
    // the new font size's actual space needs.
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      DataTab* sightings = log3->Sightings();
      if (sightings->RowCount() == 0) sightings->AddRow();
      sightings->SetCellValueByName(0, "Time", "12:34:56");
      sightings->SetCellValueByName(0, "Lat", "42.123456");
      sightings->SetCellValueByName(0, "Lon", "-70.123456");

      sightings->SetGridFontSize(10);
      std::vector<int> widthsSmallFont = sightings->GetColumnWidths();
      sightings->SetGridFontSize(20);
      std::vector<int> widthsLargeFont = sightings->GetColumnWidths();

      bool anyWidened = false;
      bool matchingSizes = widthsSmallFont.size() == widthsLargeFont.size();
      if (matchingSizes) {
        for (size_t i = 0; i < widthsSmallFont.size(); i++) {
          if (widthsLargeFont[i] > widthsSmallFont[i]) anyWidened = true;
        }
      }
      check(matchingSizes && anyWidened,
            "At least one column widens when the grid font size "
            "increases from 10pt to 20pt (Time/Lat/Lon content needs "
            "more room at a larger font, and the column width should "
            "reflect that)");
    }

    // --- Chart marker color is always resolved from a Sightings row's
    // Species (species.csv) or an Events row's Event (event_types.csv)
    // -- reverted away from a brief per-row Color-column override
    // experiment back to a plain Map on/off column, per direct request.
    // species.csv/event_types.csv now store human-readable color names
    // (e.g. "Blue"), not hex codes.
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      DataTab* sightings = log3->Sightings();
      int r = sightings->RowCount();
      sightings->AddRow();
      sightings->SetCellValueByName(r, "Species", "Fin whale");
      sightings->SetCellValueByName(r, "Lat", "42.35");
      sightings->SetCellValueByName(r, "Lon", "-70.05");
      check(sightings->GetCellValueByName(r, "Map") == "1",
            "A new Sightings row's Map column defaults to checked (\"1\")");

      auto findPointNear = [](const std::vector<ChartPoint>& pts, double lat,
                              double lon) -> const ChartPoint* {
        for (const auto& pt : pts) {
          if (std::abs(pt.lat - lat) < 0.0001 &&
              std::abs(pt.lon - lon) < 0.0001) {
            return &pt;
          }
        }
        return nullptr;
      };

      auto pts1 = sightings->GetChartedPoints();
      const ChartPoint* p1 = findPointNear(pts1, 42.35, -70.05);
      check(p1 != nullptr,
            "Fin whale row appears in GetChartedPoints with Map checked");
      if (p1) {
        // species.csv's default Fin whale color is "Blue" -> (52, 120,
        // 219) per NamedColorToColour() in DataTab.cpp.
        check(p1->color.IsOk() && p1->color.Red() == 52 &&
                  p1->color.Green() == 120 && p1->color.Blue() == 219,
              "Fin whale's marker color resolves to species.csv's "
              "configured \"Blue\" (52, 120, 219)");
      }

      sightings->SetCellValueByName(r, "Map", "0");
      auto pts2 = sightings->GetChartedPoints();
      check(findPointNear(pts2, 42.35, -70.05) == nullptr,
            "Unchecking Map removes the point from GetChartedPoints");
    }

    // --- Weather conditions in the Summary tab join with ", " (comma
    // + space), not a bare comma.
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      DataTab* env = log3->Environmental();
      int r1 = env->RowCount();
      env->AddRow();
      env->SetCellValueByName(r1, "Weather", "Clear");
      int r2 = env->RowCount();
      env->AddRow();
      env->SetCellValueByName(r2, "Weather", "Overcast");

      SurveySummary s = log3->ComputeSummary();
      check(s.uniqueWeather.Index("Clear") != wxNOT_FOUND &&
                s.uniqueWeather.Index("Overcast") != wxNOT_FOUND,
            "ComputeSummary picks up both distinct Weather values logged "
            "above");
    }

    // --- Timezone setting: explicit override for recorded timestamps,
    // including correct US/Canada DST computation (2nd Sunday of March
    // to 1st Sunday of November) -- checked against independently
    // computed reference dates/offsets, on both sides of both
    // transitions, for two different zones.
    {
      // AllZones() order: System Default(0), UTC(1), Eastern(2),
      // Atlantic(3), Central(4), Mountain(5), Pacific(6).
      const int kEastern = 2, kAtlantic = 3, kUtc = 1;
      auto checkTz = [&](const wxString& label, const wxString& isoUtc,
                         int zoneIdx, const wxString& expectedAbbrev,
                         const wxString& expectedTime) {
        TimeZoneSetting::Set(zoneIdx);
        wxDateTime dt;
        dt.ParseISOCombined(isoUtc);
        wxString result = TimeZoneSetting::FormatInSelectedZone(dt);
        check(result.Contains(expectedAbbrev) && result.Contains(expectedTime),
              label);
      };

      checkTz("Jan 15 2026 (winter) in Eastern resolves to EST, 07:00",
              "2026-01-15T12:00:00", kEastern, "EST", "07:00:00");
      checkTz("Jul 15 2026 (summer) in Eastern resolves to EDT, 08:00",
              "2026-07-15T12:00:00", kEastern, "EDT", "08:00:00");
      checkTz(
          "Mar 7 2026 (day before spring-forward) in Eastern is "
          "still EST",
          "2026-03-07T12:00:00", kEastern, "EST", "07:00:00");
      checkTz(
          "Mar 8 2026 (spring-forward day, well after 2am local) in "
          "Eastern is already EDT",
          "2026-03-08T18:00:00", kEastern, "EDT", "14:00:00");
      checkTz(
          "Oct 31 2026 (day before fall-back) in Eastern is still "
          "EDT",
          "2026-10-31T12:00:00", kEastern, "EDT", "08:00:00");
      checkTz(
          "Nov 1 2026 (fall-back day, well after 2am local) in "
          "Eastern is back to EST",
          "2026-11-01T18:00:00", kEastern, "EST", "13:00:00");
      checkTz("Jan 15 2026 in Atlantic resolves to AST, 08:00",
              "2026-01-15T12:00:00", kAtlantic, "AST", "08:00:00");
      checkTz("Jul 15 2026 in Atlantic resolves to ADT, 09:00",
              "2026-07-15T12:00:00", kAtlantic, "ADT", "09:00:00");
      checkTz("UTC zone has no DST and applies no offset",
              "2026-07-15T12:00:00", kUtc, "UTC", "12:00:00");

      // A row added while a specific zone is selected should actually
      // use it -- confirms the wiring into DataTab::OnRowAdded, not
      // just the standalone formatting function.
      TimeZoneSetting::Set(kUtc);
      LogWindow* log3 = plugin3.GetLogWindow();
      DataTab* sightings = log3->Sightings();
      int r = sightings->RowCount();
      sightings->AddRow();
      wxString recordedTime = sightings->GetCellValueByName(r, "Time");
      check(recordedTime.Contains("UTC"),
            "A new row's Time column reflects the currently-selected "
            "Timezone setting (UTC), not just whatever the system's "
            "own timezone happens to be");

      TimeZoneSetting::Set(0);  // restore System Default
    }

    // --- DistUnit should default to "nm" on a new row (the distance
    // calculations already assume nm; it just wasn't showing/selected
    // in the cell by default) -- per direct request.
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      DataTab* sightings = log3->Sightings();
      int r = sightings->RowCount();
      sightings->AddRow();
      check(sightings->GetCellValueByName(r, "DistUnit") == "nm",
            "A new Sightings row's DistUnit defaults to \"nm\"");
    }

    // --- Survey data files (sightings.csv, track.csv, etc) live in a
    // "data" subfolder within this plugin's own config directory,
    // separate from settings/config files (species.csv, timezone.txt,
    // etc) which stay directly in the config directory -- per direct
    // request.
    {
      LogWindow* log3 = plugin3.GetLogWindow();
      check(plugin3.GetSurveyDataDir() != plugin3.GetDataDir(),
            "The survey data directory is distinct from the base "
            "config directory");
      check(plugin3.GetSurveyDataDir().StartsWith(plugin3.GetDataDir()),
            "The survey data directory is a subfolder of the base "
            "config directory");
      check(plugin3.GetSurveyDataDir().EndsWith("data") ||
                plugin3.GetSurveyDataDir().Contains(
                    wxString("data") + wxFileName::GetPathSeparator()),
            "The survey data subfolder is named \"data\"");
      check(log3->Sightings()->GetCsvPath().StartsWith(
                plugin3.GetSurveyDataDir()),
            "Sightings' CSV file lives inside the survey data "
            "subfolder, not the base config directory");
      check(plugin3.GetTrackRecorder()->GetCsvPath().StartsWith(
                plugin3.GetSurveyDataDir()),
            "track.csv lives inside the survey data subfolder, not "
            "the base config directory");
      check(wxFileExists(
                wxFileName(plugin3.GetDataDir(), "species.csv").GetFullPath()),
            "species.csv (a settings/config file, not survey data) "
            "still lives directly in the base config directory");
    }

    // --- One-time migration of survey data files left behind at the
    // old, pre-"data subfolder" location -- confirmed as a real gap:
    // anyone who used this plugin before that split would have their
    // existing survey files stranded there, invisible to "Load
    // Survey..." (which only ever looks in the new data/ subfolder).
    {
      wxFile leftover;
      wxString leftoverPath =
          wxFileName(plugin3.GetDataDir(), "MigrationTest_sightings.csv")
              .GetFullPath();
      leftover.Create(leftoverPath, true);
      leftover.Write("SightNo,Species\n1,Fin whale\n");
      leftover.Close();

      // A fresh plugin instance, simulating a restart, is what actually
      // runs the migration (in Init()) -- not plugin3, which has
      // already run past that point.
      SpotterPlugin plugin4(nullptr);
      plugin4.Init();

      check(!wxFileExists(leftoverPath),
            "A leftover pre-migration survey file is moved out of the "
            "base config directory");
      wxString migratedPath =
          wxFileName(plugin4.GetSurveyDataDir(), "MigrationTest_sightings.csv")
              .GetFullPath();
      check(wxFileExists(migratedPath),
            "...and shows up in the new data subfolder instead");

      wxFile readBack(migratedPath);
      wxString content;
      readBack.ReadAll(&content);
      check(content.Contains("Fin whale"),
            "...with its original content intact after the move");

      wxArrayString foundAfterMigration =
          plugin4.GetLogWindow()->FindSurveyPrefixesInDir(
              plugin4.GetSurveyDataDir());
      check(foundAfterMigration.Index("MigrationTest") != wxNOT_FOUND,
            "...and \"Load Survey...\" can now actually find it");

      plugin4.DeInit();
    }

    // --- Settings files (current_survey.txt, latlon_format.txt,
    // timezone.txt, tracking.csv) live in their own "settings"
    // subfolder, separate from both survey data and user-editable
    // config files (species.csv, etc) -- per direct request, along
    // with a one-time migration for anyone with these files still at
    // the old, top-level location.
    {
      check(plugin3.GetSettingsDir() != plugin3.GetDataDir() &&
                plugin3.GetSettingsDir() != plugin3.GetSurveyDataDir(),
            "The settings directory is distinct from both the base "
            "config directory and the survey data directory");
      check(plugin3.GetSettingsDir().StartsWith(plugin3.GetDataDir()),
            "The settings directory is a subfolder of the base config "
            "directory");
      check(wxFileExists(wxFileName(plugin3.GetSettingsDir(), "tracking.csv")
                             .GetFullPath()),
            "tracking.csv lives in the settings subfolder");

      wxFile leftoverTz;
      wxString leftoverTzPath =
          wxFileName(plugin3.GetDataDir(), "timezone.txt").GetFullPath();
      // Simulate a leftover from before this split -- direct write
      // rather than going through TimeZoneSetting, so this doesn't
      // depend on that class's own file-location logic (already
      // correctly pointed at the new location).
      leftoverTz.Create(leftoverTzPath, true);
      leftoverTz.Write("Eastern Time");
      leftoverTz.Close();

      SpotterPlugin plugin5(nullptr);
      plugin5.Init();
      check(!wxFileExists(leftoverTzPath),
            "A leftover pre-migration timezone.txt is moved out of the "
            "base config directory");
      check(wxFileExists(wxFileName(plugin5.GetSettingsDir(), "timezone.txt")
                             .GetFullPath()),
            "...and shows up in the new settings subfolder instead");
      plugin5.DeInit();
    }

    // --- Settings tab dropdowns (Lat/Lon format, Timezone, Sightings/
    // Events marker shape) should show their actual current selection
    // the very first time the Settings tab is shown -- confirmed as a
    // real, reported bug without the OnPageChanged fix: all four showed
    // no selection at all on first opening Settings, since
    // wxChoice::SetSelection() doesn't reliably stick on a control
    // that's part of a notebook page not yet actually shown. A fresh
    // plugin instance, never having shown Settings before, is
    // important here -- this is exactly the condition the bug depended
    // on.
    {
      SpotterPlugin plugin6(nullptr);
      plugin6.Init();
      LogWindow* log6 = plugin6.GetLogWindow();
      log6->Show();

      wxNotebook* nb = nullptr;
      std::function<void(wxWindow*)> findNb = [&](wxWindow* w) {
        for (auto child : w->GetChildren()) {
          wxNotebook* found = dynamic_cast<wxNotebook*>(child);
          if (found) {
            nb = found;
            return;
          }
          findNb(child);
          if (nb) return;
        }
      };
      findNb(log6);
      check(nb != nullptr, "Found the log window's notebook");
      if (nb) {
        nb->SetSelection(4);  // Settings, for the first time
        wxYield();

        std::function<void(wxWindow*, std::vector<wxChoice*>&)> findChoices =
            [&](wxWindow* w, std::vector<wxChoice*>& out) {
              for (auto child : w->GetChildren()) {
                wxChoice* ch = dynamic_cast<wxChoice*>(child);
                if (ch) out.push_back(ch);
                findChoices(child, out);
              }
            };
        std::vector<wxChoice*> choices;
        findChoices(nb->GetPage(4), choices);
        check(choices.size() == 4,
              "Settings tab has exactly 4 dropdowns (Lat/Lon format, "
              "Timezone, Sightings marker shape, Events marker shape)");
        bool allSelected = !choices.empty();
        for (auto* ch : choices) {
          if (ch->GetSelection() < 0) allSelected = false;
        }
        check(allSelected,
              "Every dropdown on the Settings tab shows an actual "
              "selection (not blank) the first time that tab is ever "
              "shown");
      }

      plugin6.DeInit();
    }

    // --- New surveys start with Tracking OFF and a blank Effort tab,
    // per direct request -- Tracking used to be turned on automatically
    // by StartNewSurvey(); an intermediate version of this also had
    // StartNewSurvey() add an initial Effort row explicitly set to
    // "OFF" (since a zero-row Effort tab reads as "not set" rather than
    // an explicit OFF/ON), reverted per a direct follow-up correction --
    // a new survey's Effort tab should be genuinely blank, with "not
    // set" as its correct, expected initial status.
    {
      SpotterPlugin plugin7(nullptr);
      plugin7.Init();
      LogWindow* log7 = plugin7.GetLogWindow();

      // Forced ON first, to confirm StartNewSurvey() actually turns
      // these off rather than just happening to already be off.
      plugin7.GetTrackRecorder()->SetEnabled(true);
      plugin7.GetTrackingSettings()->SetEnabled(true);

      log7->StartNewSurvey("NewSurveyDefaultsTest");
      check(!plugin7.GetTrackRecorder()->IsEnabled(),
            "A new survey starts with the track recorder's own "
            "enabled state OFF");
      check(!plugin7.GetTrackingSettings()->Enabled(),
            "...and TrackingSettings (the persisted preference) OFF "
            "too");
      check(log7->Environmental()->RowCount() == 0,
            "A new survey's Effort tab starts with zero rows (blank), "
            "not an automatically-added one");
      check(log7->CurrentEffortStatus() == "",
            "...so CurrentEffortStatus() is blank, which the status "
            "bar displays as \"not set\" rather than an explicit ON "
            "or OFF");

      plugin7.DeInit();
    }

    // --- Summary tab shows the survey's start/end time (the track's
    // own minimum-to-maximum timestamps) as the very first item, per
    // direct request.
    {
      SpotterPlugin plugin8(nullptr);
      plugin8.Init();
      LogWindow* log8 = plugin8.GetLogWindow();
      log8->StartNewSurvey("SummaryPeriodTest");

      wxDateTime t1 = wxDateTime::Now();
      wxDateTime t2 = t1 + wxTimeSpan::Minutes(5);
      // Explicit reset, not an assumption -- TimeZoneSetting's state is
      // process-global and persisted to a shared file, so it can't be
      // assumed to still be "System Default" here just because nothing
      // in this test block itself changed it.
      TimeZoneSetting::Set(0);
      plugin8.GetTrackRecorder()->SetEnabled(true);
      plugin8.GetTrackRecorder()->RecordFix(42.0, -70.0, 5.0, 90.0, t1, "OFF",
                                            "");
      plugin8.GetTrackRecorder()->RecordFix(42.1, -70.1, 5.0, 90.0, t2, "OFF",
                                            "");

      SurveySummary s = log8->ComputeSummary();
      check(s.trackStartTime.IsValid() && s.trackEndTime.IsValid(),
            "ComputeSummary() reports a valid survey start and end "
            "time once track points exist");
      check(s.trackStartTime.Format("%Y-%m-%d %H:%M:%S") ==
                    t1.Format("%Y-%m-%d %H:%M:%S") &&
                s.trackEndTime.Format("%Y-%m-%d %H:%M:%S") ==
                    t2.Format("%Y-%m-%d %H:%M:%S"),
            "...matching the track's actual first and last timestamps "
            "exactly");

      log8->Show();
      wxNotebook* nb8 = nullptr;
      std::function<void(wxWindow*)> findNb8 = [&](wxWindow* w) {
        for (auto child : w->GetChildren()) {
          wxNotebook* found = dynamic_cast<wxNotebook*>(child);
          if (found) {
            nb8 = found;
            return;
          }
          findNb8(child);
          if (nb8) return;
        }
      };
      findNb8(log8);
      if (nb8) {
        nb8->SetSelection(3);  // Summary
        wxYield();
        wxStaticText* summaryLabel = nullptr;
        std::function<void(wxWindow*)> findLabel8 = [&](wxWindow* w) {
          for (auto child : w->GetChildren()) {
            wxStaticText* st = dynamic_cast<wxStaticText*>(child);
            if (st && st->GetLabel().Contains("Survey period")) {
              summaryLabel = st;
              return;
            }
            findLabel8(child);
            if (summaryLabel) return;
          }
        };
        findLabel8(nb8->GetPage(3));
        check(summaryLabel != nullptr,
              "The Summary tab's displayed text includes \"Survey "
              "period\"");
        if (summaryLabel) {
          check(summaryLabel->GetLabel().BeforeFirst('\n').StartsWith(
                    "Survey period"),
                "...specifically as the very first line, not just "
                "somewhere in the text");
        }
      }

      plugin8.DeInit();
    }

    // --- Obs (and every other searchable CHOICE column) autocomplete:
    // typing initials that are themselves a prefix of a longer, valid
    // choice (e.g. "MD", also a prefix of "MDM") used to end up with
    // the wrong casing once a third character was typed, if that third
    // character wasn't the same case as the earlier, already-suggested
    // portion -- confirmed as a real, reported bug: typing all-lowercase
    // initials worked for the first two characters (still selected/
    // highlighted from an inline suggestion, so their casing came from
    // the suggestion, not what was actually typed) but broke on the
    // third, since nothing normalized the casing of an exact
    // case-insensitive match, only detected it and stopped there.
    {
      SpotterPlugin* setupPlugin9 = new SpotterPlugin(nullptr);
      setupPlugin9->Init();
      wxString dataDir9 = setupPlugin9->GetDataDir();
      setupPlugin9->DeInit();
      delete setupPlugin9;

      wxFile obsFile;
      obsFile.Create(wxFileName(dataDir9, "observers.csv").GetFullPath(), true);
      obsFile.Write("name,full_name\nMD,\nMDM,\n");
      obsFile.Close();

      SpotterPlugin plugin9(nullptr);
      plugin9.Init();
      LogWindow* log9 = plugin9.GetLogWindow();
      log9->Show();
      DataTab* sightings9 = log9->Sightings();
      sightings9->AddRow();

      wxGrid* grid9 = nullptr;
      std::function<void(wxWindow*)> findGrid9 = [&](wxWindow* w) {
        for (auto child : w->GetChildren()) {
          wxGrid* g = dynamic_cast<wxGrid*>(child);
          if (g) {
            grid9 = g;
            return;
          }
          findGrid9(child);
          if (grid9) return;
        }
      };
      findGrid9(sightings9->GetPanel());
      int obsCol = -1;
      if (grid9) {
        for (int c = 0; c < grid9->GetNumberCols(); c++) {
          if (grid9->GetColLabelValue(c) == "Obs") {
            obsCol = c;
            break;
          }
        }
      }
      check(grid9 != nullptr && obsCol >= 0,
            "Found the Sightings grid and its Obs column, for the "
            "autocomplete case test");

      if (grid9 && obsCol >= 0) {
        grid9->SetGridCursor(0, obsCol);
        grid9->EnableCellEditControl(true);
        wxGridCellEditor* editor9 = grid9->GetCellEditor(0, obsCol);
        wxComboBox* combo9 = dynamic_cast<wxComboBox*>(editor9->GetControl());
        check(combo9 != nullptr, "Obs cell's editor is a wxComboBox");

        auto typeChar = [&](wxChar ch) {
          long from = 0, to = 0;
          combo9->GetSelection(&from, &to);
          wxString current = combo9->GetValue();
          wxString newText =
              current.Left(from) + wxString(ch) + current.Mid(to);
          combo9->ChangeValue(newText);
          combo9->SetInsertionPoint(from + 1);
          wxCommandEvent textEvt(wxEVT_TEXT, combo9->GetId());
          textEvt.SetEventObject(combo9);
          combo9->GetEventHandler()->ProcessEvent(textEvt);
          wxYield();
        };

        if (combo9) {
          // All lowercase, exactly the reported scenario -- "md"
          // suggests "MD" first (shorter, alphabetically first), then
          // the second "m" no longer matches "MD" at all, so it needs
          // to land on "MDM" instead, correctly cased.
          typeChar('m');
          typeChar('d');
          typeChar('m');
          check(combo9->GetValue() == "MDM",
                "Typing lowercase \"mdm\" (completing initials that "
                "are themselves a prefix of another valid choice) "
                "lands on the exactly, correctly-cased \"MDM\", not "
                "some mixed-case variant that would fail the "
                "case-sensitive exact-match check on commit");
        }
        editor9->DecRef();
      }

      plugin9.DeInit();
    }

    // --- On-effort time in the Summary tab should keep increasing
    // live while the most recent Effort row is still ON, not freeze at
    // that row's own timestamp -- confirmed as a real, reported bug:
    // the previous computation only summed gaps *between* consecutive
    // Effort rows, so the time since the last logged "ON" row (with no
    // "next" row yet to close that gap against) was never counted.
    {
      SpotterPlugin plugin10(nullptr);
      plugin10.Init();
      LogWindow* log10 = plugin10.GetLogWindow();
      log10->StartNewSurvey("OnEffortLiveTest");
      DataTab* effort10 = log10->Environmental();
      effort10->AddRow();
      wxDateTime tenMinAgo = wxDateTime::Now() - wxTimeSpan::Minutes(10);
      effort10->SetCellValueByName(
          0, "Time", tenMinAgo.Format("%Y-%m-%d %H:%M:%S") + " UTC");
      effort10->SetCellValueByName(0, "Effort", "ON");

      SurveySummary s10 = log10->ComputeSummary();
      check(s10.haveEffort,
            "ComputeSummary() reports on-effort time once an ON row "
            "exists");
      check(
          s10.effortTime.GetMinutes() >= 9 && s10.effortTime.GetMinutes() <= 11,
          "On-effort time reflects the time since the still-ON row "
          "up to right now (~10 minutes), not zero/frozen at that "
          "row's own timestamp");

      plugin10.DeInit();
    }

    plugin3.DeInit();
  }
};

wxIMPLEMENT_APP(TestApp);
