#ifndef WHALE_LOG_WINDOW_H
#define WHALE_LOG_WINDOW_H

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/datetime.h>
#include <wx/spinctrl.h>
#include <memory>
#include <vector>

#include "DataTab.h"
#include "CategoryConfigFile.h"
#include "ShortcutsFile.h"
#include "PositionHeights.h"
#include "ColumnDefinitions.h"

class SpotterPlugin;

// Window arrangement presets -- see LogWindow::ApplyLayoutPreset().
enum class LayoutPreset { Overlay, SplitVertical, SplitHorizontal };

// Computed fresh (from the CSVs on disk, same as ExportMergedCsv/
// ExportGpxLayer) whenever the Summary tab is shown or "Export Data..."
// is used -- see LogWindow::ComputeSummary().
struct SurveySummary {
  bool haveTrack = false;
  double trackNm = 0.0;
  wxTimeSpan trackTime;
  // Survey start/end time -- the track's own first and last timestamps
  // (minimum to maximum), not the current wall-clock time or anything
  // else -- per direct request, shown as the first item in the Summary
  // tab.
  wxDateTime trackStartTime;
  wxDateTime trackEndTime;

  bool haveEffort = false;
  double effortNm = 0.0;
  wxTimeSpan effortTime;

  int numSightings = 0;
  int numSpeciesSighted = 0;

  bool haveBoundingBox = false;
  double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;

  bool haveVis = false;
  double minVis = 0, maxVis = 0;
  bool haveBeaufort = false;
  double minBeaufort = 0, maxBeaufort = 0;
  wxArrayString uniqueWeather;

  struct SpeciesRow {
    wxString species;
    int sightings = 0;
    int individuals = 0;
    int calves = 0;
    // True if at least one qualifying sighting for this species had a
    // blank Num/NumCalf -- individuals/calves above are summed only
    // from sightings that actually had a value, so if this is true,
    // that sum is a floor (at least this many), not a confirmed total.
    // A missing count is treated as genuinely unknown, not as zero.
    bool hasUnknownIndividuals = false;
    bool hasUnknownCalves = false;
    // How many sightings actually had a known (non-blank, valid)
    // value -- distinguishes "some sightings had a value, others
    // didn't" (show the known sum plus a "some missing" marker) from
    // "not a single sighting had a value" (show "NA" outright, since
    // there's no sum to speak of at all, not even a confirmed zero).
    int knownIndividualsCount = 0;
    int knownCalvesCount = 0;
  };
  std::vector<SpeciesRow> speciesBreakdown;  // every sighting with a
                                             // non-empty Species

  struct EventCount {
    wxString eventType;
    int count = 0;
  };
  std::vector<EventCount> eventBreakdown;  // tally of Events tab rows by
                                           // Event type, all rows (no
                                           // confidence concept for
                                           // Events the way Sightings
                                           // has)
};

// The main plugin window: a notebook with one spreadsheet tab each for
// Sightings, Effort (environmental conditions + effort ON/OFF, merged
// into one tab), Events, plus a Tracking settings tab; a status bar
// (time, vessel GPS position/speed, environmental reminder countdown +
// interval, current vessel/survey name, track/effort logging status)
// below the tabs; keyboard shortcuts (defined in shortcuts.csv); and
// buttons to export a snapshot of all the CSVs, start a fresh survey, or
// change the window arrangement. Genuinely closes (and is recreated on
// demand) when dismissed -- see on_closed.
class LogWindow : public wxFrame {
public:
  LogWindow(wxWindow* parent, SpotterPlugin* plugin, const wxString& dataDir);

  void NotifyVesselFix(double lat, double lon, const wxDateTime& utc);

  DataTab* Sightings() const { return m_sightings; }
  // Environmental conditions + Effort ON/OFF status, merged into one
  // tab titled "Effort" in the UI -- kept as a separate C++ accessor
  // name since the data it holds is still primarily environmental.
  DataTab* Environmental() const { return m_environmental; }
  DataTab* Events() const { return m_events; }
  DataTab* Surfacing() const { return m_surfacing; }

  // Repositions/resizes this window (and, for the Split presets, also
  // OpenCPN's own main window) to the requested arrangement. Overlay is
  // the original behavior (two independent floating windows); the Split
  // presets divide the current screen between OpenCPN and this window
  // (Split Vertical gives OpenCPN 60% of the width, this window 40%;
  // Split Horizontal is an even 50/50 split).
  void ApplyLayoutPreset(LayoutPreset preset);

  // The actual "start new survey" logic (compute the new file prefix,
  // switch every tab -- and the trackline -- to it, persist the choice
  // so it survives a restart), separated from OnStartNewSurveyClicked's
  // UI (name-entry and confirmation dialogs) so it's directly testable
  // and reusable. Returns the new prefix.
  wxString StartNewSurvey(const wxString& surveyName);

  wxString CurrentSurveyName() const { return m_surveyName; }
  wxString CurrentFilePrefix() const { return m_filePrefix; }

  // Called once, right before the underlying wxFrame is actually
  // destroyed (the window now really closes on the user clicking its
  // close button -- see OnClose()), so the owning plugin can clear its
  // now-dangling pointer and recreate a fresh LogWindow next time it's
  // needed.
  std::function<void()> on_closed;

  // Merges Sightings/Effort/Track rows into one CSV, sorted
  // chronologically by timestamp -- called from "Export Data..." (see
  // OnExportClicked), exposed here too since it's a self-contained,
  // useful operation on its own.
  void ExportMergedCsv(const wxString& destDir);

  // Writes a GPX file (the trackline as a <trk>, Sightings as <wpt>
  // waypoints) -- meant to be loaded into OpenCPN as a persistent
  // layer, e.g. to overlay a previous survey day's data while looking
  // at today's.
  void ExportGpxLayer(const wxString& destDir);

  // Reads Sightings/Effort/Track from disk (same pattern as
  // ExportMergedCsv/ExportGpxLayer) and computes the Summary tab's
  // figures. Exposed publicly since both the Summary tab's display and
  // ExportSummaryCsv() need it, and it's cheap enough to just recompute
  // fresh each time rather than cache.
  SurveySummary ComputeSummary() const;

  // Writes <prefix>_summary.csv -- called from "Export Data...", exposed
  // here too like the other Export* methods.
  void ExportSummaryCsv(const wxString& destDir) const;

  // Scans a directory for survey data files (matching
  // "<prefix>_sightings.csv") and returns the unique prefixes found,
  // sorted. Used by "Load Survey..." for both this plugin's own data
  // directory and an externally-chosen folder; exposed publicly here
  // too since it's cheap, self-contained, and directly testable.
  wxArrayString FindSurveyPrefixesInDir(const wxString& dir) const;

  // The non-UI portion of "Load Survey...": given a prefix already
  // chosen (by whatever means -- OnLoadSurveyClicked's own dialogs, or
  // a test calling this directly), points every tab and the track
  // recorder at that survey's files and updates this window's own
  // current-survey state (m_filePrefix, m_surveyName) to match.
  // Extracted out of OnLoadSurveyClicked (which is otherwise entirely
  // UI-driven -- modal dialogs -- and so not directly callable from a
  // test) specifically so this part is directly, publicly testable.
  void ApplyLoadedSurveyPrefix(const wxString& prefixToLoad);

private:
  void BuildStatusBar(wxSizer* rootSizer, wxWindow* root);
  void BuildBottomBar(wxSizer* rootSizer, wxWindow* root);
  void BuildTabs();
  void BuildSettingsTab();
  void BuildSummaryTab();
  void RefreshSummaryTab();
  void ApplyUiFontSize(wxWindow* win, int pointSize);
  void SetupShortcuts();
  void OnExportClicked(wxCommandEvent& evt);
  void OnStartNewSurveyClicked(wxCommandEvent& evt);
  void OnClearSurveyDataClicked(wxCommandEvent& evt);
  void OnLoadSurveyClicked(wxCommandEvent& evt);
  void OnClose(wxCloseEvent& evt);
  void OnStatusTick();
  void OnFrameResize(wxSizeEvent& evt);
  void OnPageChanged(wxBookCtrlEvent& evt);
  void OnCharHook(wxKeyEvent& evt);
  void RunShortcutAction(const wxString& action);
  wxString GetEffortStatusText() const;
  wxString GetTrackStatusText() const;

public:
  // Used by SpotterPlugin::SetPositionFix() to pass the Effort
  // tab's current state into TrackRecorder::RecordFix(), so the track
  // file records which parts of the transit were on-effort. Returns
  // blank (not the status bar's "not set" placeholder) when there's no
  // Effort data yet, since track.csv's Effort column should only ever
  // contain "ON", "OFF", or blank.
  wxString CurrentEffortStatus() const;
  wxString CurrentEffortSegNo() const;

private:
  struct SurveyInfo {
    wxString survey;
    wxString prefix;
  };
  SurveyInfo LoadCurrentSurveyInfo() const;
  void SaveCurrentSurveyInfo(const wxString& survey,
                             const wxString& prefix) const;
  static wxString SanitizeForFilename(const wxString& s);

  SpotterPlugin* m_plugin;
  // Set once in BuildStatusBar() -- lets OnStatusTick() trigger a
  // re-layout of just the status bar's own row after a label's
  // natural size changes (SetLabel() alone doesn't reliably make the
  // containing wxWrapSizer recompute on its own), without touching
  // the rest of the window's layout at all. Previously did this via
  // m_root->Layout() (a whole-window reference), confirmed via real
  // diagnostic logging as the cause of a real, reported bug --
  // re-laying-out the *entire* window every second, unconditionally,
  // was disrupting whatever grid cell might currently be mid-edit: on
  // Windows specifically, this was disrupting an actively-editing
  // dropdown cell roughly once a second, visible in real diagnostic
  // logging as a repeating BeginEdit/EndEdit cycle with no
  // corresponding key event, making it effectively impossible to
  // finish typing or selecting anything in a dropdown cell before the
  // next tick tore the edit down again.
  wxSizer* m_statusBarSizer = nullptr;
  wxString m_dataDir;
  wxString m_filePrefix;
  wxString m_surveyName;
  // Replaces a single, all-categories dropdowns.csv from an earlier
  // version of this plugin (per direct request) -- each list is its
  // own file with its own extra columns (a default map color for
  // species/events, a species code, an observer's full name, a
  // behavior code).
  CategoryConfigFile m_speciesConfig;
  CategoryConfigFile m_eventsConfig;
  CategoryConfigFile m_observersConfig;
  CategoryConfigFile m_behaviorsConfig;
  ShortcutsFile m_shortcuts;
  PositionHeights m_positionHeights;
  ColumnDefinitions m_columnDefinitions;

  wxStaticText* m_timeLabel;
  wxStaticText* m_positionLabel;
  wxStaticText* m_gpsWarningLabel;  // separate slot from m_positionLabel
                                    // specifically so its dynamically
                                    // changing width doesn't fight
                                    // with -- or visually overlap --
                                    // neighboring status fields; see
                                    // BuildStatusBar
  wxStaticText* m_speedLabel;
  wxStaticText* m_envTimerLabel;
  wxSpinCtrl* m_envIntervalCtrl;
  wxStaticText* m_surveyLabel;
  wxStaticText* m_trackStatusLabel;
  wxStaticText* m_effortStatusLabel;
  wxStaticText* m_columnDefLabel;  // between View and Start New Survey,
  // The Settings tab's "Enable tracking" checkbox -- kept as a member
  // (rather than a purely local variable in BuildSettingsTab()) so its
  // displayed state can be synced when tracking is turned off from
  // somewhere other than the checkbox itself (Load Survey). Confirmed
  // as a real, reported bug without this: Load Survey correctly turns
  // tracking off, but the checkbox stayed visually checked until the
  // Settings tab was rebuilt some other way.
  wxCheckBox* m_trackingEnabledCheck = nullptr;
  // The bottom bar's "View:" dropdown -- kept as a member so its
  // selection can be set reliably from the same deferred CallAfter()
  // block (see the constructor) that applies the actual startup
  // layout, rather than immediately at construction time, which -- like
  // ApplyLayoutPreset() itself -- doesn't reliably stick before this
  // window has actually been shown/realized.
  wxChoice* m_viewChoice = nullptr;
  // The Settings tab's Lat/Lon format and Timezone dropdowns -- kept as
  // members for the same reason as m_viewChoice just above, but with
  // an extra wrinkle: unlike m_viewChoice (part of the always-visible
  // bottom bar), these live inside the Settings notebook page itself,
  // which isn't shown at all until the user actually switches to it
  // (Sightings is the initially-selected tab). A one-time CallAfter()
  // at window construction isn't enough here -- confirmed as a real,
  // reported bug: both dropdowns showed no selection at all on first
  // opening Settings. See OnPageChanged(), which re-applies both
  // dropdowns' selection every time the Settings tab actually becomes
  // the visible page, not just once at startup.
  wxChoice* m_latLonFormatChoice = nullptr;
  wxChoice* m_timezoneChoice = nullptr;
  // The Settings tab's Sightings/Events marker shape dropdowns -- same
  // fix, same reason as m_latLonFormatChoice/m_timezoneChoice just
  // above.
  wxChoice* m_sightingsMarkerShapeChoice = nullptr;
  wxChoice* m_eventsMarkerShapeChoice = nullptr;
  // Raw (unwrapped) text currently shown in m_columnDefLabel -- kept
  // separately since wxStaticText::Wrap() rewrites the label's own
  // text in place with embedded newlines, and re-wrapping already-
  // wrapped text (as would happen on a second resize if this weren't
  // tracked separately) would wrap it into increasingly fragmented,
  // wrong-looking lines each time.
  wxString m_columnDefRawText;

  // Summary tab -- see BuildSummaryTab()/RefreshSummaryTab().
  wxStaticText* m_summaryMetricsLabel = nullptr;
  // in the bottom bar
  wxTimer m_statusTimer;
  bool m_gpsWasStale;
  wxTimer m_resizeDebounceTimer;

  wxNotebook* m_notebook;
  std::vector<std::unique_ptr<DataTab>> m_tabs;

  DataTab* m_sightings;
  DataTab* m_environmental;  // titled "Effort" in the UI -- see above
  DataTab* m_events;
  DataTab* m_surfacing;

  wxDECLARE_EVENT_TABLE();
};

#endif  // WHALE_LOG_WINDOW_H
