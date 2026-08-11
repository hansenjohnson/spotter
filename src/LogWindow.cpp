#include "LogWindow.h"
#include "spotter_pi.h"
#include "CsvUtils.h"
#include "LatLonFormat.h"
#include "TimeZoneSetting.h"

#include <wx/dirdlg.h>
#include <wx/dir.h>
#include <wx/choicdlg.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/hyperlink.h>
#include <wx/wrapsizer.h>
#include <wx/display.h>
#include <wx/scrolwin.h>
#include <wx/colordlg.h>
#include <wx/file.h>

#include "ocpn_plugin.h"

#include <cmath>
#include <algorithm>

wxBEGIN_EVENT_TABLE(LogWindow, wxFrame) wxEND_EVENT_TABLE()

    namespace {
  // Fixed, standardized vocabularies -- not survey-specific, so these stay
  // hardcoded rather than living in an editable CSV. Species/event/
  // observer/behavior choices, by contrast, come from CategoryConfigFile
  // (see BuildTabs()) since those genuinely vary survey to survey.
  const wxString kConfidenceChoices[] = {"Definite", "Probable", "Possible"};
  // "At Least" -- for a count known to be a minimum/undercount (animals
  // present but not all necessarily seen/countable), distinct from
  // genuine uncertainty about the exact number otherwise.
  const wxString kNumConfChoices[] = {"Definite", "Probable", "Possible",
                                      "At Least"};

  // Plain numeric Beaufort scale, 1-9 -- an earlier version included
  // descriptive text ("3 - Slight") but that was more than wanted; the
  // bare number is enough for anyone trained on the scale.
  const wxString kSeaStateChoices[] = {"0", "1", "2", "3",  "4",  "5", "6",
                                       "7", "8", "9", "10", "11", "12"};

  const wxString kWeatherChoices[] = {
      "Clear", "Partly cloudy", "Overcast", "Fog/haze",
      "Rain",  "Squall",        "Drizzle",
  };

  // "OFF" listed first so a brand-new, never-logged tab's very first row
  // (before there's any previous row to inherit from) defaults to OFF --
  // effort hasn't started until explicitly logged as ON.
  const wxString kEffortStatusChoices[] = {"OFF", "ON"};
  const wxString kImagesCollectedChoices[] = {"None", "Photos", "Video",
                                              "Photos/Video"};
  // "Surfacing" listed first (the default) per direct request -- it's the
  // far more common entry than "First surfacing".
  const wxString kSurfacingEventChoices[] = {"Surfacing", "First surfacing",
                                             "Fluking"};

  // "m" (meters) listed first (the default). "reticles" is the number of
  // binocular reticle marks (mils) a target appears below the visible
  // horizon -- see RecomputeBearingDistancePosition's comment for the
  // distance calculation and an important caveat about the constant it
  // depends on.
  const wxString kDistUnitChoices[] = {"m", "nm", "reticles"};

  const wxString kGlareChoices[] = {"None", "Mild", "Moderate", "Severe"};

  const int kGpsStaleThresholdSeconds = 30;
  const int kPositionUnchangedThresholdSeconds = 60;

  wxArrayString ToArrayString(const wxString* arr, size_t n) {
    wxArrayString out;
    for (size_t i = 0; i < n; i++) out.Add(arr[i]);
    return out;
  }

  // Wraps a ColumnDef to mark its choices as order-sensitive (see
  // ColumnDef::preserveChoiceOrder) -- used inline at construction, for
  // CHOICE columns whose defined order (severity, confidence) is the
  // meaningful one and shouldn't be alphabetized.
  ColumnDef OrderPreserved(ColumnDef cd) {
    cd.preserveChoiceOrder = true;
    return cd;
  }

  // Wraps a ColumnDef to give it an explicit default value, applied to
  // every new row regardless of the tab-wide
  // DataTabConfig::defaultChoicesToFirstOption flag -- see
  // ColumnDef::defaultValue's own comment for why this needs to be
  // separate from that flag.
  ColumnDef WithDefault(ColumnDef cd, const wxString& defaultValue) {
    cd.defaultValue = defaultValue;
    return cd;
  }

  // Builds a "Marker: [shape] [color]" row for one plottable tab (used by
  // the Settings tab's Map section) -- moved here from DataTab's own
  // per-tab toolbar (see DataTab::SetupMarkerControls(), removed) per
  // direct request, so both controls live in one place, next to that
  // tab's map-label picker, rather than being split across two different
  // tabs' toolbars and the Settings tab.
  wxChoice* BuildMarkerControlsRow(
      wxWindow * panel, wxSizer * targetSizer, DisplaySettings * settings,
      const wxString& tabKey, const wxString& defaultShape,
      std::function<void()> onChanged) {
    wxString currentShape =
        settings ? settings->MarkerShape(tabKey, defaultShape) : defaultShape;

    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, tabKey + " marker shape:"), 0,
             wxALIGN_CENTER_VERTICAL);

    wxArrayString shapeOptions;
    shapeOptions.Add("Diamond");
    shapeOptions.Add("Square");
    shapeOptions.Add("Triangle");
    shapeOptions.Add("Circle");
    shapeOptions.Add("Star");
    wxChoice* shapeChoice = new wxChoice(panel, wxID_ANY, wxDefaultPosition,
                                         wxDefaultSize, shapeOptions);
    // A best-effort initial value -- harmless even though it doesn't
    // reliably stick on a control that's part of a notebook page not yet
    // actually shown (wxChoice::SetSelection() has the same issue
    // ApplyLayoutPreset() and the View dropdown did). The real fix for
    // that is OnPageChanged() re-applying this same selection every time
    // the Settings tab actually becomes visible -- see there.
    int sel = shapeOptions.Index(currentShape);
    shapeChoice->SetSelection(sel == wxNOT_FOUND ? 0 : sel);
    shapeChoice->Bind(wxEVT_CHOICE, [settings, tabKey, shapeChoice,
                                     onChanged](wxCommandEvent&) {
      if (settings) {
        settings->SetMarkerShape(tabKey, shapeChoice->GetStringSelection());
      }
      if (onChanged) onChanged();
    });
    row->Add(shapeChoice, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

    targetSizer->Add(row, 0, wxALL, 8);
    return shapeChoice;
  }

  // Shared by Sightings and Surfacings (identical logic; only the output
  // column names differ). Derives a lat/lon from the vessel's own Lat/Lon
  // plus a magnetic bearing (BearingMag) and a distance (Dist, in
  // DistUnit).
  //
  // Bearing is always taken as a magnetic compass bearing, used directly
  // (not corrected for magnetic variation -- no declination source
  // available). An earlier version also supported bearings relative to
  // vessel heading (DegRel/ClockRel, using course-over-ground as a
  // heading proxy), but that was removed: COG can reflect current/leeway
  // drift rather than the vessel's actual heading, which made relative
  // bearings unreliable without a real heading source -- magnetic-only
  // avoids depending on that proxy at all.
  //
  // DistUnit: "nm" is used directly; "m" (meters) is converted via the
  // standard 1 nm = 1852 m. "reticles" estimates range from the number of
  // binocular reticle marks (mils) the target appears below the visible
  // horizon, combined with the observer's eye height (see
  // DataTab::GetObserverHeightFt(), sourced from the Effort tab's
  // Position column) -- see kRadiansPerReticle's comment for an important
  // caveat about this constant.
  void RecomputeBearingDistancePosition(
      DataTab * tab, GenericGridTable * t, int row, int editedCol,
      const wxString& outLatCol, const wxString& outLonCol) {
    int bearingCol = t->FindColByName("BearingMag");
    int distCol = t->FindColByName("Dist");
    int distUnitCol = t->FindColByName("DistUnit");
    int vlatCol = t->FindColByName("Lat");
    int vlonCol = t->FindColByName("Lon");
    int slatCol = t->FindColByName(outLatCol);
    int slonCol = t->FindColByName(outLonCol);
    if (editedCol != bearingCol && editedCol != distCol &&
        editedCol != distUnitCol && editedCol != vlatCol &&
        editedCol != vlonCol) {
      return;
    }
    double vlat, vlon;
    if (!t->RawGet(row, vlatCol).ToDouble(&vlat)) return;
    if (!t->RawGet(row, vlonCol).ToDouble(&vlon)) return;

    double trueBearing = 0.0, distRaw = 0.0;
    t->RawGet(row, bearingCol).ToDouble(&trueBearing);
    t->RawGet(row, distCol).ToDouble(&distRaw);
    wxString distUnit = t->RawGet(row, distUnitCol);
    if (trueBearing < 0) trueBearing = fmod(trueBearing, 360.0) + 360.0;

    double distNm = distRaw;
    if (distUnit == "m") {
      distNm = distRaw / 1852.0;
    } else if (distUnit == "reticles") {
      // Standard "dip of horizon" formula (small-angle approximation,
      // the same one used for nautical-almanac dip tables): the angle
      // from the observer's eye down to the visible horizon, in radians,
      // for eye height h (meters) and Earth radius R (meters).
      const double kEarthRadiusM = 6371000.0;
      // ObsHeightFt is captured once, at the moment this row was first
      // added (see DataTab::OnRowAdded), from whatever the observer
      // height was *then* -- not read live here. Reading it live would
      // mean editing Dist on an old row later (e.g. after the observer
      // has since moved to a different Position) would silently use
      // *today's* height instead of the height that was actually in
      // effect at the time of the sighting -- confirmed as a real,
      // reported bug in an earlier version that read
      // tab->GetObserverHeightFt() directly here.
      int obsHeightCol = t->FindColByName("ObsHeightFt");
      double heightFt = tab->GetObserverHeightFt();  // fallback for rows
                                                     // logged before this
                                                     // column existed
      if (obsHeightCol >= 0) {
        double stored = 0.0;
        if (t->RawGet(row, obsHeightCol).ToDouble(&stored) && stored > 0.0) {
          heightFt = stored;
        }
      }
      double heightM = heightFt * 0.3048;
      double dipRadians = std::sqrt(2.0 * heightM / kEarthRadiusM);

      // Radians subtended by one reticle mark -- THIS CONSTANT VARIES BY
      // INSTRUMENT and hasn't been verified against a specific reticle
      // binocular's actual specification (no way to look that up from
      // here); 5 mrad/reticle (0.005 rad) is a commonly cited convention
      // for reticle-scale marine binoculars, used here as a reasonable
      // default, but this should be checked against whatever binoculars
      // are actually in use and corrected if it doesn't match -- a wrong
      // constant here will systematically bias every reticle-based
      // distance by the same factor. No adjustable setting for this yet;
      // flagging it here for whoever revisits this.
      const double kRadiansPerReticle = 0.005;

      double totalAngle = dipRadians + distRaw * kRadiansPerReticle;
      double distanceM =
          (totalAngle > 0.0) ? heightM / std::tan(totalAngle) : 0.0;
      distNm = distanceM / 1852.0;
    }

    if (std::isnan(distNm)) {
      // No known observer height (reticles DistUnit, before any Effort
      // Position has ever been logged) -- propagate NAN rather than
      // silently falling back to the vessel's own position, which would
      // look like a real (if lazy) answer instead of "this can't
      // currently be computed."
      t->RawSet(row, slatCol, "nan");
      t->RawSet(row, slonCol, "nan");
      return;
    }

    double dlat = vlat, dlon = vlon;
    if (distNm > 0.0) {
      PositionBearingDistanceMercator_Plugin(vlat, vlon, trueBearing, distNm,
                                             &dlat, &dlon);
    }
    t->RawSet(row, slatCol, wxString::FromDouble(dlat, 6));
    t->RawSet(row, slonCol, wxString::FromDouble(dlon, 6));
  }

  // Effort tab's SegNo: blank while Effort is OFF; increments only when
  // effort transitions OFF -> ON (staying at the same number for however
  // many consecutive rows effort remains ON). Recomputed unconditionally
  // whenever this tab's recompute() runs (cheap for the row counts a
  // survey produces), rather than gated on a specific edited column,
  // since Effort can arrive at "ON" via inheritance onto a new row rather
  // than a direct edit to the Effort column itself.
  void RecomputeEffortSegNo(DataTab * tab, GenericGridTable * t, int row,
                            int editedCol) {
    wxUnusedVar(tab);
    wxUnusedVar(editedCol);
    int effortCol = t->FindColByName("Effort");
    int segNoCol = t->FindColByName("SegNo");
    if (effortCol < 0 || segNoCol < 0) return;

    if (t->RawGet(row, effortCol) != "ON") {
      t->RawSet(row, segNoCol, "");
      return;
    }
    if (row == 0) {
      t->RawSet(row, segNoCol, "1");
      return;
    }
    if (t->RawGet(row - 1, effortCol) == "ON") {
      t->RawSet(row, segNoCol, t->RawGet(row - 1, segNoCol));
      return;
    }
    long maxSeg = 0;
    for (int r = 0; r < row; r++) {
      long v = 0;
      if (t->RawGet(r, segNoCol).ToLong(&v)) maxSeg = std::max(maxSeg, v);
    }
    t->RawSet(row, segNoCol, wxString::Format("%ld", maxSeg + 1));
  }

}  // namespace

wxString LogWindow::SanitizeForFilename(const wxString& s) {
  wxString out;
  for (size_t i = 0; i < s.length(); i++) {
    wxChar c = s[i];
    if (wxIsalnum(c) || c == '-' || c == '_') {
      out += c;
    } else if (c == ' ') {
      out += '_';
    }
    // everything else (slashes, colons, quotes, etc.) is dropped
  }
  while (out.Contains("__")) out.Replace("__", "_");
  out.Trim(true).Trim(false);
  if (out.IsEmpty()) out = "Unnamed";
  return out;
}

LogWindow::SurveyInfo LogWindow::LoadCurrentSurveyInfo() const {
  SurveyInfo info;
  wxFileName fn(m_plugin->GetSettingsDir(), "current_survey.txt");
  if (!wxFileExists(fn.GetFullPath())) return info;
  wxFile f(fn.GetFullPath());
  if (!f.IsOpened()) return info;
  wxString contents;
  f.ReadAll(&contents);
  wxArrayString lines = wxSplit(contents, '\n');
  // SaveCurrentSurveyInfo() writes a trailing newline, which wxSplit()
  // turns into an extra empty element at the end -- stripped here
  // first, so it doesn't get miscounted as part of the old-vs-new
  // format detection below.
  while (!lines.IsEmpty() && lines.Last().IsEmpty())
    lines.RemoveAt(lines.size() - 1);
  // Older versions of this file had three lines: vessel, survey,
  // prefix. Now there's just survey and prefix (two lines) -- an
  // existing file from before this change still has the old layout,
  // so it's read differently based on line count specifically (not
  // just "at least 2 lines," which both layouts satisfy) to correctly
  // pick out survey from the right position either way.
  if (lines.size() >= 3) {
    info.survey = lines[1];
    info.prefix = lines[2];
  } else if (lines.size() == 2) {
    info.survey = lines[0];
    info.prefix = lines[1];
  } else if (lines.size() == 1) {
    info.survey = lines[0];
  }
  info.survey.Trim(true).Trim(false);
  info.prefix.Trim(true).Trim(false);
  return info;
}

void LogWindow::SaveCurrentSurveyInfo(const wxString& survey,
                                      const wxString& prefix) const {
  wxFileName fn(m_plugin->GetSettingsDir(), "current_survey.txt");
  wxFile f;
  if (f.Create(fn.GetFullPath(), true)) {
    f.Write(survey + "\n" + prefix + "\n");
    f.Close();
  }
}

LogWindow::LogWindow(wxWindow* parent, SpotterPlugin* plugin,
                     const wxString& dataDir)
    : wxFrame(parent, wxID_ANY, "Spotter", wxDefaultPosition,
              // Deliberately plain wxDEFAULT_FRAME_STYLE, no
              // wxFRAME_FLOAT_ON_PARENT: `parent` is OpenCPN's canvas
              // window, which isn't guaranteed non-null at the point
              // Init() constructs this, and floating-on-a-possibly-null
              // parent is a plausible source of the window becoming
              // hard to dismiss on some platforms.
              wxSize(850, 620), wxDEFAULT_FRAME_STYLE),
      m_plugin(plugin),
      m_dataDir(dataDir),
      m_speciesConfig(dataDir, "species.csv", {"color", "species_code"},
                      {
                          {"Humpback whale", "Orange", "Mn"},
                          {"Fin whale", "Blue", "Bp"},
                          {"Blue whale", "Navy", "Bm"},
                          {"Minke whale", "Teal", "Ba"},
                          {"Sperm whale", "Purple", "Pm"},
                          {"North Atlantic right whale", "Red", "Eg"},
                          {"Killer whale (Orca)", "Black", "Oo"},
                          {"Pilot whale", "Green", "Gm"},
                          {"Beaked whale (unident.)", "Gray", "Ziphiidae"},
                          {"Dolphin (unident.)", "Yellow", "Delphinidae"},
                          {"Unidentified whale", "Gray", "Unid"},
                          {"Other", "White", "Other"},
                      }),
      m_eventsConfig(dataDir, "event_types.csv", {"color"},
                     {
                         {"CTD cast", "Blue"},
                         {"Drifter deployment", "Teal"},
                         {"Drone flight", "Purple"},
                         {"Tagging", "Red"},
                         {"Biopsy sample", "Orange"},
                         {"Acoustic recorder deployment", "Green"},
                         {"Acoustic recorder recovery", "Green"},
                         {"Other", "Gray"},
                     }),
      m_observersConfig(dataDir, "observers.csv", {"full_name"},
                        {
                            {"Observer 1", ""},
                            {"Observer 2", ""},
                            {"Observer 3", ""},
                            {"Observer 4", ""},
                        }),
      m_behaviorsConfig(dataDir, "behaviors.csv", {"behavior_code"},
                        {
                            {"Traveling", "TRV"},
                            {"Feeding", "FEED"},
                            {"Resting", "REST"},
                            {"Socializing", "SOC"},
                            {"Breaching", "BRCH"},
                            {"Spy-hopping", "SPY"},
                            {"Tail slapping / lobtailing", "TAIL"},
                            {"Bow riding", "BOW"},
                            {"Milling", "MILL"},
                            {"Unknown", "UNK"},
                        }),
      m_shortcuts(dataDir),
      m_positionHeights(dataDir),
      m_columnDefinitions(dataDir),
      m_timeLabel(nullptr),
      m_positionLabel(nullptr),
      m_gpsWarningLabel(nullptr),
      m_speedLabel(nullptr),
      m_envTimerLabel(nullptr),
      m_envIntervalCtrl(nullptr),
      m_surveyLabel(nullptr),
      m_trackStatusLabel(nullptr),
      m_effortStatusLabel(nullptr),
      m_columnDefLabel(nullptr),
      m_gpsWasStale(false),
      m_notebook(nullptr),
      m_sightings(nullptr),
      m_environmental(nullptr),
      m_events(nullptr),
      m_surfacing(nullptr) {
  SurveyInfo info = LoadCurrentSurveyInfo();
  m_filePrefix = info.prefix;
  m_surveyName = info.survey;

  wxPanel* root = new wxPanel(this);
  wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

  // Status bar at the top (always visible regardless of which tab is
  // active), tables below it.
  BuildStatusBar(rootSizer, root);

  m_notebook = new wxNotebook(root, wxID_ANY);
  // Same reasoning as DataTab's grid SetMinSize() -- without this, the
  // notebook's natural size (driven by its widest page's natural size)
  // becomes a hard floor on the whole window, defeating the point of
  // resizing it smaller.
  m_notebook->SetMinSize(wxSize(300, 150));
  BuildTabs();
  for (auto& tab : m_tabs) {
    m_notebook->AddPage(tab->GetPanel(), tab->Title());
  }
  // Between the DataTab-backed pages (Sightings/Effort/Events) and
  // Settings -- neither is itself a DataTab, so both have to come after
  // all of m_tabs for OnPageChanged's bounds check (see its comment) to
  // keep working, but Summary specifically goes *before* Settings per
  // direct request.
  BuildSummaryTab();
  BuildSettingsTab();  // added last -- see OnPageChanged's bounds check,
                       // which relies on non-DataTab pages coming after
                       // all the m_tabs-backed ones. Consolidates the
                       // Tracking controls, the Effort reminder
                       // interval, and the file path links (data
                       // folder / dropdowns / shortcuts / display) all
                       // in one place.
  rootSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 4);
  m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &LogWindow::OnPageChanged,
                   this);

  BuildBottomBar(rootSizer, root);

  root->SetSizer(rootSizer);

  wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
  frameSizer->Add(root, 1, wxEXPAND);
  SetSizer(frameSizer);

  Bind(wxEVT_CLOSE_WINDOW, &LogWindow::OnClose, this);

  SetupShortcuts();
  Bind(wxEVT_SIZE, &LogWindow::OnFrameResize, this);
  m_resizeDebounceTimer.SetOwner(this);
  Bind(
      wxEVT_TIMER,
      [this, root](wxTimerEvent&) {
        for (auto& tab : m_tabs) {
          tab->ForceResizeColumnsToFit();
          // Column widths changing can change how much text wraps
          // within a cell, which can change how tall a row needs to be
          // -- re-checked here too, not just once at startup, so a
          // window resize can't leave rows too short for their content
          // (or unnecessarily tall) after columns have settled into
          // their new widths.
          tab->ReapplyRowHeights();
        }
        // The status bar's wxWrapSizer re-wrapping its fields needs the
        // same debounce-then-Layout() treatment as the grid columns
        // above -- confirmed via a real, still-reproducing reported bug
        // that the plain wxEVT_SIZE-time Layout() nudge further down
        // (which *does* reliably work for the notebook/grid) wasn't
        // enough on its own for this specific sizer. Doing it again here,
        // after the debounce delay has let the platform's own resize
        // negotiation actually settle, is the same fix that made the
        // grid columns reliable in the first place.
        root->Layout();
        // The column definition label (above the View/Start New
        // Survey/etc. row) needs the same re-wrap treatment --
        // wxStaticText doesn't auto-wrap to its container's width on
        // its own, so this is re-applied here using the current,
        // settled width every time the window resizes (including
        // switching between Split Vertical/Horizontal, which changes
        // available width substantially).
        if (m_columnDefLabel) {
          int width = m_columnDefLabel->GetParent()->GetClientSize().GetWidth();
          if (width > 40) {
            m_columnDefLabel->SetLabel(m_columnDefRawText);
            m_columnDefLabel->Wrap(width - 24);  // roughly matches the
                                                 // wxLEFT|wxRIGHT, 12
                                                 // border used when
                                                 // adding this label
          }
        }
      },
      m_resizeDebounceTimer.GetId());

  // Explicitly force a re-layout on every frame resize. This looks
  // redundant with the sizers already in place (root -> rootSizer ->
  // notebook -> page -> grid, all wxEXPAND), and shrinking the frame
  // does cascade correctly without this -- but growing it back (and,
  // separately, the status bar's wxWrapSizer re-wrapping its fields)
  // didn't reliably happen without an explicit nudge here, at least in
  // testing -- confirmed via a real reported bug where the status bar
  // simply didn't re-wrap when switching into Split Vertical. Layout()
  // on the root panel itself covers both the notebook and the status
  // bar (siblings under the same rootSizer), rather than narrowly
  // targeting just the notebook. Cheap and safe to always do.
  Bind(wxEVT_SIZE, [this, root](wxSizeEvent& evt) {
    root->Layout();
    if (m_notebook) {
      wxWindow* page = m_notebook->GetCurrentPage();
      if (page) page->Layout();
    }
    evt.Skip();
  });

  m_statusTimer.SetOwner(this);
  Bind(
      wxEVT_TIMER, [this](wxTimerEvent&) { OnStatusTick(); },
      m_statusTimer.GetId());
  m_statusTimer.Start(1000);
  OnStatusTick();

  // Applied last, after every control in the window has been created --
  // recurses through the whole window, skipping wxGrids (which have
  // their own separate GridFontSize control).
  if (m_plugin->GetDisplaySettings()) {
    ApplyUiFontSize(this, m_plugin->GetDisplaySettings()->UiFontSize());
    Layout();
  }

  // Split Horizontal is the default view (see the "View:" dropdown
  // above, which starts on this same option) -- actually apply it here
  // too, since setting a wxChoice's displayed selection doesn't itself
  // fire the change event that would otherwise apply it. Deferred via
  // CallAfter(), the same reasoning as the row-height fix just below:
  // applying window geometry (this window's own size/position, and
  // OpenCPN's main frame's) before this window has ever actually been
  // shown/realized may not reliably stick -- reported as not applying
  // automatically on launch despite being set here directly before.
  CallAfter([this]() {
    ApplyLayoutPreset(LayoutPreset::SplitHorizontal);
    // Set here, not immediately at construction, for the same reason
    // as ApplyLayoutPreset() itself just above -- confirmed as a real,
    // reported bug: the dropdown was visually showing no selection at
    // all on startup, despite Split Horizontal actually being applied.
    if (m_viewChoice) m_viewChoice->SetSelection(2);
  });

  // A real, reported bug: rows loaded from an existing CSV (i.e.
  // whatever was already in a survey when the plugin was reopened)
  // could end up with a too-short, text-clipping row height, while
  // brand new rows added during the same session were always sized
  // correctly. The likely cause: SetGridFontSize()'s AutoSizeRows()
  // call (in the per-tab setup loop, further up this constructor) runs
  // before this window has ever actually been shown/laid out, so the
  // grid may still have a zero or otherwise not-yet-real size to
  // measure text against at that point -- and nothing ever repeated
  // that measurement later once the window's real size was known. Newly
  // *added* rows didn't have this problem because they're only ever
  // added well after the window is already showing, when the grid's
  // size is real. Fixed by re-running AutoSizeRows() here, deferred via
  // CallAfter() so it runs on a later idle cycle after this window has
  // actually been shown and laid out at least once, not immediately.
  CallAfter([this]() {
    for (auto& tab : m_tabs) tab->ReapplyRowHeights();
    RefreshSummaryTab();
  });

  CentreOnParent();
}

void LogWindow::ApplyUiFontSize(wxWindow* win, int pointSize) {
  if (!win || pointSize <= 0) return;
  // Grids have their own separate size control (GridFontSize /
  // display.csv's grid_font_size, set directly on each DataTab) --
  // skipped here so the two settings don't fight over the same text.
  if (dynamic_cast<wxGrid*>(win)) return;

  wxFont f = win->GetFont();
  f.SetPointSize(pointSize);
  win->SetFont(f);
  // SetFont() alone doesn't resize a widget -- confirmed as a real,
  // reported bug: at a large UI font size (20pt+), button text visibly
  // overflowed the button's own boundaries, because the button kept
  // whatever bounding box it was originally created with (sized for
  // whatever font was active then), even though it was now rendering
  // much larger text inside that same, unchanged box.
  // InvalidateBestSize() marks the cached best-size stale so it's
  // recomputed against the new font; SetSize() to that freshly-
  // recomputed size actually applies it.
  win->InvalidateBestSize();
  win->SetSize(win->GetBestSize());
  for (wxWindow* child : win->GetChildren()) {
    ApplyUiFontSize(child, pointSize);
  }
  // Deliberately after the children loop, not before: a child needs
  // its own correct, font-adjusted size established first, so that
  // when this window's own sizer (if it has one) lays out its
  // children, it's measuring against each child's real, up-to-date
  // size rather than a still-stale one.
  if (win->GetSizer()) win->Layout();
}

void LogWindow::BuildBottomBar(wxSizer* rootSizer, wxWindow* root) {
  // Shows the currently-selected cell's column definition (see
  // ColumnDefinitions / column_definitions.csv), in italics, updated by
  // each DataTab's on_cell_selected callback (wired below, once the
  // tabs exist). In its own row, above the View/Start New Survey/etc.
  // buttons row, rather than sharing a row with them -- a shared row
  // meant a long definition either got clipped or squeezed the buttons
  // for space, especially in Split Vertical (much narrower). Wrapping
  // is applied explicitly (wxStaticText doesn't auto-wrap to its
  // container's width on its own) and re-applied on every window
  // resize (see the resize debounce timer below), the same treatment
  // already needed for the status bar's own wxWrapSizer.
  wxBoxSizer* columnDefRow = new wxBoxSizer(wxHORIZONTAL);
  m_columnDefLabel = new wxStaticText(root, wxID_ANY, "", wxDefaultPosition,
                                      wxDefaultSize, wxST_NO_AUTORESIZE);
  wxFont defFont = m_columnDefLabel->GetFont();
  defFont.MakeItalic();
  m_columnDefLabel->SetFont(defFont);
  columnDefRow->Add(m_columnDefLabel, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);
  rootSizer->Add(columnDefRow, 0, wxEXPAND);

  // wxWrapSizer, not a plain wxBoxSizer, so these controls wrap onto
  // additional rows instead of overflowing/getting clipped when the
  // window is narrow (e.g. Split Vertical, or docked side-by-side with
  // the chart) -- confirmed as a real, reported issue: with a plain
  // wxBoxSizer, the survey-management buttons could become unreachable
  // rather than just rearranging. The AddStretchSpacer previously used
  // to push those buttons to the right of the View dropdown doesn't
  // have a sensible equivalent in a wrapping layout (there's no fixed
  // line for something to be "pushed to the end of"), so it's removed
  // here -- everything just flows left-to-right and wraps as needed,
  // the same treatment already used for the status bar above.
  wxWrapSizer* bottomBar =
      new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS);

  bottomBar->Add(new wxStaticText(root, wxID_ANY, "View:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
  wxArrayString viewOptions;
  viewOptions.Add("Overlay");
  viewOptions.Add("Split Vertical");
  viewOptions.Add("Split Horizontal");
  wxChoice* viewChoice = new wxChoice(root, wxID_ANY, wxDefaultPosition,
                                      wxDefaultSize, viewOptions);
  m_viewChoice = viewChoice;
  viewChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& evt) {
    switch (evt.GetSelection()) {
      case 1:
        ApplyLayoutPreset(LayoutPreset::SplitVertical);
        break;
      case 2:
        ApplyLayoutPreset(LayoutPreset::SplitHorizontal);
        break;
      default:
        ApplyLayoutPreset(LayoutPreset::Overlay);
        break;
    }
  });
  bottomBar->Add(viewChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);

  wxButton* newSurveyBtn = new wxButton(root, wxID_ANY, "Start New Survey...");
  wxButton* loadSurveyBtn = new wxButton(root, wxID_ANY, "Load Survey...");
  wxButton* clearSurveyBtn =
      new wxButton(root, wxID_ANY, "Clear Survey Data...");
  wxButton* exportBtn = new wxButton(root, wxID_ANY, "Export Data...");
  bottomBar->Add(newSurveyBtn, 0, wxALL, 6);
  bottomBar->Add(loadSurveyBtn, 0, wxALL, 6);
  bottomBar->Add(clearSurveyBtn, 0, wxALL, 6);
  bottomBar->Add(exportBtn, 0, wxALL, 6);

  exportBtn->Bind(wxEVT_BUTTON, &LogWindow::OnExportClicked, this);
  newSurveyBtn->Bind(wxEVT_BUTTON, &LogWindow::OnStartNewSurveyClicked, this);
  loadSurveyBtn->Bind(wxEVT_BUTTON, &LogWindow::OnLoadSurveyClicked, this);
  clearSurveyBtn->Bind(wxEVT_BUTTON, &LogWindow::OnClearSurveyDataClicked,
                       this);

  rootSizer->Add(bottomBar, 0, wxEXPAND);
}

void LogWindow::ApplyLayoutPreset(LayoutPreset preset) {
  wxWindow* canvasWin = GetOCPNCanvasWindow();
  wxWindow* ocpnFrame = canvasWin ? wxGetTopLevelParent(canvasWin) : nullptr;

  if (!ocpnFrame || preset == LayoutPreset::Overlay) {
    // Either running somewhere OpenCPN's own window can't be found (e.g.
    // the standalone test harness), or the user just wants the original
    // independent-floating-window behavior -- either way, only this
    // window's own geometry is touched, OpenCPN's is left alone.
    SetSize(wxSize(850, 620));
    CentreOnParent();
    return;
  }

  int displayIdx = wxDisplay::GetFromWindow(ocpnFrame);
  if (displayIdx == wxNOT_FOUND) displayIdx = 0;
  wxDisplay display(static_cast<unsigned int>(displayIdx));
  wxRect area = display.GetClientArea();

  if (preset == LayoutPreset::SplitVertical) {
    // OpenCPN (the chart) gets the larger share -- it's the primary
    // thing being navigated by; this window is more of a reference/
    // data-entry panel alongside it.
    int mapWidth = static_cast<int>(area.width * 0.6);
    ocpnFrame->SetSize(area.x, area.y, mapWidth, area.height);
    SetSize(area.x + mapWidth, area.y, area.width - mapWidth, area.height);
  } else {  // SplitHorizontal -- same 60/40 reasoning as SplitVertical,
            // just split top/bottom instead of left/right. (An earlier
            // version used a plain 50/50 split here.)
    int mapHeight = static_cast<int>(area.height * 0.6);
    ocpnFrame->SetSize(area.x, area.y, area.width, mapHeight);
    SetSize(area.x, area.y + mapHeight, area.width, area.height - mapHeight);
  }
}

void LogWindow::SetupShortcuts() {
  // wxEVT_CHAR_HOOK fires at the top of keyboard event propagation --
  // before it reaches whichever child control has focus (a wxGrid cell,
  // typically) -- which is what makes shortcuts here actually work
  // regardless of what's focused. A wxAcceleratorTable was tried first
  // and turned out not to reliably fire while a grid had focus; this is
  // the more robust standard approach for exactly that situation.
  Bind(wxEVT_CHAR_HOOK, &LogWindow::OnCharHook, this);
}

void LogWindow::OnFrameResize(wxSizeEvent& evt) {
  evt.Skip();
  // A short debounce timer, rather than reacting synchronously (or via
  // CallAfter()): confirmed via direct testing that reading a child's
  // client size immediately after a resize -- even via CallAfter(),
  // even after an explicit Layout() -- can still see a stale, pre-resize
  // size, since the underlying platform's own layout negotiation isn't
  // necessarily complete within the same event-loop iteration. A short
  // delay reliably waits it out on every platform, and as a bonus
  // debounces rapid resize events while the user is actively dragging
  // the window edge, rather than recalculating on every intermediate
  // pixel.
  m_resizeDebounceTimer.StartOnce(150);
}

void LogWindow::OnPageChanged(wxBookCtrlEvent& evt) {
  evt.Skip();
  if (m_columnDefLabel) {
    m_columnDefRawText = wxEmptyString;
    m_columnDefLabel->SetLabel(wxEmptyString);
  }
  // A non-visible notebook page's grid may not have been given a
  // correct/current size while hidden (confirmed via testing -- hidden
  // tabs can report a stale, tiny client size), so re-check the newly
  // selected tab's column widths now that it's actually being shown.
  // The Settings tab (added after all m_tabs-backed pages) has no grid
  // and isn't in m_tabs at all -- this bounds check safely skips it.
  int sel = evt.GetSelection();
  if (sel >= 0 && sel < static_cast<int>(m_tabs.size())) {
    m_tabs[sel]->ForceResizeColumnsToFit();
    // Same underlying issue as the column-width fix just above, for row
    // heights instead: a tab that's never been the visible page yet
    // (e.g. Effort/Events on first opening the plugin, if Sightings is
    // the initially-selected tab) can have rows sized against a stale,
    // tiny grid size from before it was ever actually shown -- a
    // one-time CallAfter() in this window's constructor only catches
    // whichever tab happened to be selected at that moment, not every
    // tab. Re-checking here, every time a *different* tab becomes the
    // visible one, covers all of them as each is actually seen for the
    // first time.
    m_tabs[sel]->ReapplyRowHeights();
  } else if (sel == static_cast<int>(m_tabs.size())) {
    // Summary is added right after all the m_tabs-backed pages (see the
    // constructor) -- recomputed fresh every time it's shown, rather
    // than kept live-updated in the background, since it's cheap enough
    // to just recompute and this is the only place it needs to happen.
    RefreshSummaryTab();
  } else if (sel == static_cast<int>(m_tabs.size()) + 1) {
    // Settings is added right after Summary (see the constructor) --
    // re-applies the Lat/Lon format and Timezone dropdowns' selection
    // every time this tab actually becomes visible. Confirmed as a
    // real, reported bug without this: both dropdowns showed no
    // selection at all the first time Settings was opened, since
    // wxChoice::SetSelection() -- like ApplyLayoutPreset() and the
    // View dropdown before it -- doesn't reliably stick on a control
    // that's part of a notebook page not yet actually shown, and a
    // one-time CallAfter() at window construction doesn't help here
    // since Settings isn't the initially-selected tab.
    if (m_latLonFormatChoice) {
      m_latLonFormatChoice->SetSelection(static_cast<int>(LatLonFormat::Get()));
    }
    if (m_timezoneChoice) {
      m_timezoneChoice->SetSelection(TimeZoneSetting::Get());
    }
    DisplaySettings* ds = m_plugin->GetDisplaySettings();
    if (m_sightingsMarkerShapeChoice) {
      wxString shape = ds ? ds->MarkerShape("Sightings", "Diamond") : "Diamond";
      int idx = m_sightingsMarkerShapeChoice->FindString(shape);
      m_sightingsMarkerShapeChoice->SetSelection(idx == wxNOT_FOUND ? 0 : idx);
    }
    if (m_eventsMarkerShapeChoice) {
      wxString shape = ds ? ds->MarkerShape("Events", "Square") : "Square";
      int idx = m_eventsMarkerShapeChoice->FindString(shape);
      m_eventsMarkerShapeChoice->SetSelection(idx == wxNOT_FOUND ? 0 : idx);
    }
  }

  // Explicitly focus whichever page just became visible -- reported as
  // a real bug specifically for Settings (reachable via Cmd+5): once
  // switched to, *every* keyboard shortcut stopped working, until
  // clicking some other tab. Root cause: unlike the grid-based tabs
  // (which naturally have their wxGrid receive focus), Settings is
  // just a wxScrolledWindow full of assorted controls (checkboxes,
  // spin controls, links) with no single "main" control that
  // automatically grabs focus when the tab is switched to -- so
  // wxWindow::FindFocus() could return null once on Settings, which
  // OnCharHook's very first guard (added for an earlier, unrelated
  // fix) treats as "this keystroke isn't for this window at all" and
  // rejects unconditionally. Explicitly focusing the page's panel here
  // ensures FindFocus() always finds something within this window,
  // regardless of which tab is showing.
  wxWindow* page = m_notebook->GetPage(sel);
  if (page) page->SetFocus();
}

void LogWindow::OnCharHook(wxKeyEvent& evt) {
  // Only even consider this event if the currently focused control
  // actually belongs to this window. wxEVT_CHAR_HOOK fires very early,
  // before normal accelerator-table processing -- if it were ever
  // invoked for a keystroke intended for OpenCPN's own window (not this
  // one), even correctly Skip()-ing on a non-match could plausibly still
  // interfere with how that key then gets processed downstream. This
  // guard makes it structurally impossible for that to happen: if focus
  // isn't inside this window at all, bail out immediately without
  // touching the event any further. Added specifically because of a
  // reported issue where OpenCPN's own "+" chart zoom-in shortcut
  // stopped working after this plugin introduced CHAR_HOOK-based
  // shortcuts -- "+" was never one of this plugin's own shortcuts, so
  // if this fixes it, the mechanism was almost certainly this handler
  // firing (or being consulted) even when it shouldn't have been,
  // rather than an actual matching bug.
  wxWindow* focused = wxWindow::FindFocus();
  if (!focused || wxGetTopLevelParent(focused) != this) {
    evt.Skip();
    return;
  }
  // A second guard (wxGetActiveWindow() != this) was added here for a
  // while, on top of the FindFocus() check above, as a second attempt
  // at the "+' stopped zooming the chart" problem -- removed again
  // after it was strongly implicated in a much worse regression
  // (*every* keyboard shortcut in this plugin silently stopped
  // working). wxGetActiveWindow() asks the platform which top-level
  // window is active/frontmost -- a query that, in a plugin hosted
  // inside another application's process (OpenCPN's), evidently doesn't
  // reliably agree that a focused floating/utility window like this one
  // counts as "active," even when it genuinely has keyboard focus. The
  // extra guard was speculative from the start (never confirmed to fix
  // anything, only suspected); trading it away in exchange for
  // everything else working again is the right call. FindFocus() alone
  // stays: it's a more fundamental property (which window actually has
  // keyboard focus, not which one some platform API considers
  // "active"), and removing it entirely would reopen the original "+"
  // issue this was all trying to fix.

  // Tab navigation (cycling with PageUp/PageDown, jumping directly to a
  // specific tab with 1-5) used to be hardcoded here; moved into
  // shortcuts.csv as ordinary, user-editable actions (NextTab/PrevTab/
  // GoToSightings/GoToEffort/GoToEvents/GoToSummary/GoToSettings -- see
  // RunShortcutAction()) per direct request, so they can be remapped or
  // removed the same way any other shortcut can if a default
  // combination doesn't work well on a given machine.
  // Undo/Redo used to be hardcoded here (Cmd+Shift+Backspace/Delete);
  // moved into shortcuts.csv as ordinary, user-remappable actions
  // ("Undo"/"Redo" -- see RunShortcutAction()) per direct request, so
  // they can be changed the same way any other shortcut can if a
  // default combination doesn't work well on a given machine. The
  // "Undo"/"Redo" toolbar buttons on every tab remain as a keyboard-
  // independent fallback either way.

  // Enter/F2/Space, as a backup for opening a MULTI_CHOICE column's
  // checklist dialog (the primary path is DataTab::OnGridKeyDown, bound
  // directly on the grid) -- added after a reported case where these
  // weren't reliably working for this, especially on a read-only cell
  // (which is what every MULTI_CHOICE cell is -- the checklist dialog
  // is the only way to edit one). Handled here, at the CHAR_HOOK level,
  // in case something between this handler and the grid's own
  // wxEVT_KEY_DOWN was intercepting the key first, possibly specific to
  // read-only cells; harmless to have both, since whichever one runs
  // first consumes the event.
  if (!evt.CmdDown() && !evt.ControlDown() && !evt.ShiftDown() &&
      !evt.AltDown() &&
      (evt.GetKeyCode() == WXK_SPACE || evt.GetKeyCode() == WXK_RETURN ||
       evt.GetKeyCode() == WXK_NUMPAD_ENTER || evt.GetKeyCode() == WXK_F2)) {
    int sel = m_notebook->GetSelection();
    if (sel >= 0 && sel < static_cast<int>(m_tabs.size()) &&
        m_tabs[sel]->TryOpenMultiSelectForCurrentCell()) {
      return;  // consumed
    }
  }

  // General case: explicitly start editing on Enter or a plain typed
  // character, for whatever cell currently has the grid cursor, when
  // the grid isn't already editing. Confirmed via direct testing (real
  // synthetic key events, not just calling EnableCellEditControl()
  // directly) that wxGrid's supposed native "typing/Enter starts
  // editing" behavior does not reliably trigger in this environment --
  // only F2 reliably worked on its own -- and that consuming this at
  // the grid's own wxEVT_KEY_DOWN level (DataTab::OnGridKeyDown) isn't
  // sufficient either: something afterward, almost certainly wxGrid's
  // own internal handling of the follow-up wxEVT_CHAR event (a
  // genuinely separate event from KEY_DOWN, not something consuming
  // KEY_DOWN alone prevents), still cancelled the edit state that had
  // just been enabled. Handled here instead, at the CHAR_HOOK level --
  // early and authoritative enough to actually stick.
  //
  // DataTab::TryStartEditingCurrentCell() opens the cell showing its
  // *existing* content rather than replacing it with the typed
  // character the way native "typing starts editing" convention
  // normally would -- a deliberate deviation, since a separately
  // reported complaint was specifically about content getting erased
  // when starting an edit via the keyboard (unlike via double-click).
  if (!evt.CmdDown() && !evt.ControlDown() && !evt.AltDown()) {
    int keyCode = evt.GetKeyCode();
    bool isEnter = keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER;
    bool isPrintable = keyCode >= 32 && keyCode <= 126;
    if (isEnter || isPrintable) {
      int sel = m_notebook->GetSelection();
      if (sel >= 0 && sel < static_cast<int>(m_tabs.size()) &&
          m_tabs[sel]->TryStartEditingCurrentCell()) {
        return;  // consumed
      }
    }
  }

  for (const auto& kv : m_shortcuts.Get()) {
    const wxString& combo = kv.first;
    const wxString& action = kv.second;

    bool wantCtrl = false, wantCmd = false, wantShift = false, wantAlt = false;
    int keyCode = 0;
    bool validCombo = true;

    wxStringTokenizer tok(combo, "+");
    wxString last;
    while (tok.HasMoreTokens()) {
      wxString part = tok.GetNextToken();
      part.Trim(true).Trim(false);
      wxString upper = part.Upper();
      if (upper == "CTRL") {
        wantCtrl = true;
      } else if (upper == "CMD") {
        wantCmd = true;
      } else if (upper == "SHIFT") {
        wantShift = true;
      } else if (upper == "ALT") {
        wantAlt = true;
      } else {
        last = part;
      }
    }
    if (last.IsEmpty()) {
      validCombo = false;
    } else if (last.length() == 1) {
      keyCode = static_cast<int>(last.Upper()[0]);
    } else {
      wxString upperLast = last.Upper();
      if (upperLast == "SPACE") {
        keyCode = WXK_SPACE;
      } else if (upperLast == "TAB") {
        keyCode = WXK_TAB;
      } else if (upperLast == "PAGEUP") {
        keyCode = WXK_PAGEUP;
      } else if (upperLast == "PAGEDOWN") {
        keyCode = WXK_PAGEDOWN;
      } else if (upperLast.StartsWith("F") && upperLast.length() <= 3) {
        long n = 0;
        if (upperLast.Mid(1).ToLong(&n) && n >= 1 && n <= 12) {
          keyCode = WXK_F1 + static_cast<int>(n - 1);
        } else {
          validCombo = false;
        }
      } else {
        validCombo = false;
      }
    }
    if (!validCombo) continue;

    // Ctrl and Cmd are treated as interchangeable here rather than
    // matched independently: per wx's own docs, CmdDown() is literally
    // defined as "same as ControlDown() on platforms other than macOS"
    // -- they're not two independent modifier states there, they're the
    // same physical key reported through two different accessors. Only
    // on macOS does CmdDown() reflect a genuinely separate key (Command)
    // from ControlDown(). Requiring both to match independently broke
    // shortcuts entirely on non-Mac platforms (confirmed via testing);
    // this interchangeable check works correctly on both.
    bool ctrlOrCmdWanted = wantCtrl || wantCmd;
    bool ctrlOrCmdHeld = evt.ControlDown() || evt.CmdDown();

    if (evt.GetKeyCode() == keyCode && ctrlOrCmdHeld == ctrlOrCmdWanted &&
        evt.ShiftDown() == wantShift && evt.AltDown() == wantAlt) {
      RunShortcutAction(action);
      return;  // consumed -- don't call evt.Skip()
    }
  }
  evt.Skip();  // not one of ours -- let it propagate normally
}

void LogWindow::RunShortcutAction(const wxString& action) {
  // Extended syntax: "AddSighting" adds a plain row, but
  // "AddSighting:Species=North Atlantic right whale" (or with several
  // ";"-separated Field=Value pairs) also pre-fills specific columns on
  // that new row -- e.g. a dedicated shortcut per commonly-seen species.
  wxString baseAction = action;
  wxString fieldsStr;
  int colon = action.Find(':');
  if (colon != wxNOT_FOUND) {
    baseAction = action.Left(colon);
    fieldsStr = action.Mid(colon + 1);
  }

  // Cycles between the data tabs (Sightings, Effort, Events,
  // Surfacing) only -- skips Summary/Settings, neither of which is a
  // data table. Wraps around in either direction.
  if (baseAction == "NextTab" || baseAction == "PrevTab") {
    int count = static_cast<int>(m_tabs.size());
    if (count > 0) {
      int sel = m_notebook->GetSelection();
      if (sel < 0 || sel >= count) sel = 0;
      int delta = baseAction == "NextTab" ? 1 : -1;
      int next = (sel + delta + count) % count;
      m_notebook->SetSelection(next);
    }
    return;
  }

  // Jumps directly to a specific tab by name -- unlike NextTab/PrevTab
  // above, Summary and Settings are valid targets here too, since
  // jumping straight to either is exactly the kind of thing a direct
  // shortcut is for. Uses the notebook's page index directly rather
  // than m_tabs, since neither Summary nor Settings is itself a
  // DataTab.
  if (baseAction == "GoToSightings" || baseAction == "GoToEffort" ||
      baseAction == "GoToEvents" || baseAction == "GoToSummary" ||
      baseAction == "GoToSettings") {
    int pageIndex = -1;
    if (baseAction == "GoToSightings")
      pageIndex = 0;
    else if (baseAction == "GoToEffort")
      pageIndex = 1;
    else if (baseAction == "GoToEvents")
      pageIndex = 2;
    else if (baseAction == "GoToSummary")
      pageIndex = 3;
    else if (baseAction == "GoToSettings")
      pageIndex = 4;
    if (pageIndex >= 0 &&
        pageIndex < static_cast<int>(m_notebook->GetPageCount())) {
      m_notebook->SetSelection(pageIndex);
      // Explicitly focused here too (not just relying on
      // OnPageChanged, which normally does this -- see its comment for
      // the full explanation), since SetSelection() isn't guaranteed to
      // reliably fire wxEVT_NOTEBOOK_PAGE_CHANGED on every platform.
      wxWindow* page = m_notebook->GetPage(pageIndex);
      if (page) page->SetFocus();
    }
    return;
  }

  // Undo/Redo apply to whichever data tab is currently active, rather
  // than a fixed target the way AddSighting/AddEnvironmental/AddEvent
  // do -- handled here, separately, before any of that fixed-target
  // logic below.
  if (baseAction == "Undo" || baseAction == "Redo") {
    int sel = m_notebook->GetSelection();
    if (sel >= 0 && sel < static_cast<int>(m_tabs.size())) {
      if (baseAction == "Undo") {
        m_tabs[sel]->Undo();
      } else {
        m_tabs[sel]->Redo();
      }
    }
    return;
  }

  DataTab* target = nullptr;
  if (baseAction == "AddSighting") target = m_sightings;
  // AddEffort is kept as an alias for AddEnvironmental -- Environmental
  // and Effort used to be two separate tabs; anyone with an existing
  // shortcuts.csv referencing the old AddEffort action should still get
  // a working shortcut rather than a silently-ignored one.
  else if (baseAction == "AddEnvironmental" || baseAction == "AddEffort")
    target = m_environmental;
  else if (baseAction == "AddEvent")
    target = m_events;
  if (!target) return;

  target->AddRow();
  int row = target->RowCount() - 1;

  if (!fieldsStr.IsEmpty() && row >= 0) {
    wxStringTokenizer fieldTok(fieldsStr, ";");
    while (fieldTok.HasMoreTokens()) {
      wxString pair = fieldTok.GetNextToken();
      int eq = pair.Find('=');
      if (eq == wxNOT_FOUND) continue;
      wxString field = pair.Left(eq);
      wxString value = pair.Mid(eq + 1);
      field.Trim(true).Trim(false);
      value.Trim(true).Trim(false);
      if (field.IsEmpty()) continue;
      target->SetCellValueByName(row, field, value);
    }
  }

  for (size_t i = 0; i < m_tabs.size(); i++) {
    if (m_tabs[i].get() == target) {
      m_notebook->SetSelection(static_cast<int>(i));
      break;
    }
  }
  Show();
  Raise();
}

void LogWindow::BuildStatusBar(wxSizer* rootSizer, wxWindow* root) {
  // Plain wxBoxSizer, not a wxStaticBoxSizer -- the "Status" label/
  // border was removed per direct request, to reclaim the vertical
  // space its title text and border padding took up.
  wxBoxSizer* box = new wxBoxSizer(wxVERTICAL);

  // wxWrapSizer lays fields out left-to-right on as few lines as fit,
  // wrapping overflow onto additional lines -- so on a wide window
  // everything sits on one line, and as the window is narrowed (e.g. in
  // Split Vertical layout, or just resized down toward half a monitor)
  // fields automatically reflow into a multi-line grid instead of being
  // clipped or forcing the window wider than the user wants.
  wxWrapSizer* wrap = new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS);

  auto addField = [&](const wxString& labelText, wxStaticText** out) {
    wxBoxSizer* pair = new wxBoxSizer(wxHORIZONTAL);
    // Bottom-aligned, not center-aligned, per direct request -- the
    // monospace font used for the value below has different metrics
    // (ascent/descent) than the label's own proportional-width font,
    // which made the value visibly sit slightly higher than the label
    // next to it under center alignment. Bottom alignment isn't
    // sensitive to that difference the same way, since it's anchored
    // to a fixed edge instead of each widget's own full line height.
    pair->Add(new wxStaticText(root, wxID_ANY, labelText), 0, wxALIGN_BOTTOM);
    *out = new wxStaticText(root, wxID_ANY, "--");
    wxFont f = (*out)->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    // A fixed-width (monospace) font specifically for the computed
    // value -- not the static label text before it -- so its rendered
    // width doesn't jitter as the value changes. Confirmed as a real,
    // reported issue: Time updates every second, and with a
    // proportional-width font each new digit can be a slightly
    // different width, making the whole field (and everything the wrap
    // sizer placed after it) visibly shift left/right continuously.
    // wxFONTFAMILY_TELETYPE resolves to whatever generic monospace font
    // is available on the current platform (Consolas/Monaco/Courier-
    // style), rather than hardcoding one specific font name that might
    // not exist everywhere.
    f.SetFamily(wxFONTFAMILY_TELETYPE);
    (*out)->SetFont(f);
    pair->Add(*out, 0, wxALIGN_BOTTOM | wxLEFT, 4);
    wrap->Add(pair, 0, wxALIGN_BOTTOM | wxALL, 6);
  };

  addField("Time:", &m_timeLabel);
  addField("Vessel Position:", &m_positionLabel);
  // A separate field from Vessel Position specifically so its own,
  // frequently-changing text length (blank most of the time, a longer
  // warning message when something's actually wrong) doesn't change
  // Vessel Position's effective width at runtime and visually collide
  // with whatever field the wrap sizer had placed next to it -- each
  // field gets its own slot in the wrap layout, so a length change here
  // only affects wrapping, never overlaps a neighbor.
  addField("", &m_gpsWarningLabel);
  addField("Speed:", &m_speedLabel);

  // Effort tab reminder countdown -- the interval *control* now lives on
  // the Settings tab, alongside Tracking and the file links; this is
  // just the live countdown readout, which stays here since it's
  // something you want visible while working, not a setting.
  addField("Effort check:", &m_envTimerLabel);

  addField("Survey:", &m_surveyLabel);
  addField("Tracking:", &m_trackStatusLabel);
  addField("Effort:", &m_effortStatusLabel);

  box->Add(wrap, 0, wxEXPAND);
  m_statusBarSizer = box;
  rootSizer->Add(box, 0, wxEXPAND | wxALL, 4);
}

wxString LogWindow::GetEffortStatusText() const {
  if (!m_environmental || m_environmental->RowCount() == 0) return "not set";
  return m_environmental->GetCellValueByName(m_environmental->RowCount() - 1,
                                             "Effort");
}

wxString LogWindow::CurrentEffortStatus() const {
  if (!m_environmental || m_environmental->RowCount() == 0) return "";
  return m_environmental->GetCellValueByName(m_environmental->RowCount() - 1,
                                             "Effort");
}

wxString LogWindow::CurrentEffortSegNo() const {
  if (!m_environmental || m_environmental->RowCount() == 0) return "";
  return m_environmental->GetCellValueByName(m_environmental->RowCount() - 1,
                                             "SegNo");
}

wxString LogWindow::GetTrackStatusText() const {
  TrackRecorder* track = m_plugin->GetTrackRecorder();
  if (!track) return "--";
  return track->IsEnabled() ? "ON" : "OFF";
}

void LogWindow::OnStatusTick() {
  if (!m_timeLabel) return;

  m_timeLabel->SetLabel(wxDateTime::Now().Format("%H:%M:%S"));

  // Observer height for the "reticles" DistUnit calculation, looked up
  // from the Effort tab's most recent Position entry and pushed to
  // Sightings/Surfacings -- polled here (once a second, alongside
  // everything else this timer already does) rather than via a
  // dedicated change-watching mechanism, since a 1-second lag between
  // changing Position and it taking effect isn't practically
  // significant for this.
  if (m_environmental && m_environmental->RowCount() > 0) {
    wxString position = m_environmental->GetCellValueByName(
        m_environmental->RowCount() - 1, "Position");
    double heightFt = m_positionHeights.GetHeightFt(position, 20.0);
    if (m_sightings) m_sightings->SetObserverHeightFt(heightFt);
    if (m_surfacing) m_surfacing->SetObserverHeightFt(heightFt);
  }

  bool haveFix = m_plugin->HasEverHadFix();
  int elapsed = m_plugin->SecondsSinceLastFix();
  bool stale = haveFix && elapsed > kGpsStaleThresholdSeconds;
  int unchangedFor = m_plugin->SecondsSincePositionChanged();
  bool positionStuck =
      haveFix && !stale && unchangedFor > kPositionUnchangedThresholdSeconds;

  // Built via wxUniChar rather than a "\u26A0" escape embedded directly
  // in a format string with substitutions -- confirmed via direct
  // testing that this specific wx 3.2 build reproducibly segfaults when
  // a raw multi-byte Unicode escape and a numeric format specifier
  // appear in the same wxString::Format() call (see LatLonFormat.cpp
  // for the fuller explanation; same underlying issue, same fix).
  static const wxString kWarningSign(wxUniChar(0x26A0));

  if (!haveFix) {
    m_positionLabel->SetLabel("waiting for fix...");
    m_positionLabel->SetForegroundColour(m_timeLabel->GetForegroundColour());
    m_gpsWarningLabel->SetLabel("");
    m_speedLabel->SetLabel("--");
  } else if (stale) {
    m_positionLabel->SetForegroundColour(m_timeLabel->GetForegroundColour());
    m_gpsWarningLabel->SetLabel(
        kWarningSign + wxString::Format(" GPS SIGNAL LOST (%ds)", elapsed));
    m_gpsWarningLabel->SetForegroundColour(*wxRED);
    m_speedLabel->SetLabel("--");
    if (!m_gpsWasStale) {
      wxMessageBox(
          "No GPS position fix has been received in the last " +
              wxString::Format("%d", kGpsStaleThresholdSeconds) +
              " seconds.\n\nNew rows will not auto-fill vessel position "
              "until the fix is restored. Existing data is unaffected.",
          "Spotter -- GPS Signal Lost", wxOK | wxICON_WARNING, this);
    }
  } else {
    double lat, lon;
    m_plugin->GetLastFix(lat, lon);
    wxString posText = LatLonFormat::FormatValue(lat, true) + "  " +
                       LatLonFormat::FormatValue(lon, false);
    m_positionLabel->SetLabel(posText);
    if (positionStuck) {
      // Fixes are arriving on schedule, but the position value itself
      // hasn't meaningfully changed in a while -- distinct from "no fix
      // at all" (still shown separately above), but still worth
      // flagging: could be a stuck/repeating feed, though a genuinely
      // stationary vessel would look the same, so this is a caution,
      // not a hard error -- amber rather than red.
      m_positionLabel->SetForegroundColour(wxColour(200, 130, 0));
      m_gpsWarningLabel->SetLabel(
          kWarningSign + wxString::Format(" unchanged for %ds", unchangedFor));
      m_gpsWarningLabel->SetForegroundColour(wxColour(200, 130, 0));
    } else {
      m_positionLabel->SetForegroundColour(wxColour(0, 130, 0));
      m_gpsWarningLabel->SetLabel("");
    }
    m_speedLabel->SetLabel(
        wxString::Format("%.1f kts", m_plugin->GetLastSog()));
  }
  m_gpsWasStale = stale;

  if (m_environmental && m_environmental->HasReminderTimer()) {
    m_envTimerLabel->SetLabel(
        m_environmental->IsReminderOverdue()
            ? "OVERDUE"
            : m_environmental->GetReminderCountdownText());
    m_envTimerLabel->SetForegroundColour(
        m_environmental->IsReminderOverdue() ? *wxRED : wxColour(0, 130, 0));
  }

  m_surveyLabel->SetLabel(m_surveyName.IsEmpty() ? "(not set)" : m_surveyName);
  wxString trackStatus = GetTrackStatusText();
  m_trackStatusLabel->SetLabel(trackStatus);
  m_trackStatusLabel->SetForegroundColour(
      trackStatus == "ON"    ? wxColour(0, 130, 0)
      : trackStatus == "OFF" ? *wxRED
                             : m_timeLabel->GetForegroundColour());
  wxString effortStatus = GetEffortStatusText();
  m_effortStatusLabel->SetLabel(effortStatus);
  m_effortStatusLabel->SetForegroundColour(
      effortStatus == "ON"    ? wxColour(0, 130, 0)
      : effortStatus == "OFF" ? *wxRED
                              : m_timeLabel->GetForegroundColour());

  m_positionLabel->Refresh();
  m_gpsWarningLabel->Refresh();
  m_speedLabel->Refresh();
  m_envTimerLabel->Refresh();
  m_surveyLabel->Refresh();
  m_trackStatusLabel->Refresh();
  m_effortStatusLabel->Refresh();

  // Label text lengths just changed (e.g. the GPS warning field going
  // from empty to a real message, or back), which can change what the
  // status bar's wxWrapSizer needs to do -- SetLabel() alone doesn't
  // reliably trigger that recalculation on its own -- so ask for it
  // explicitly. Scoped to just this row's own sizer (m_statusBarSizer,
  // set once in BuildStatusBar()) rather than the previous
  // m_root->Layout() -- confirmed via real diagnostic logging as the
  // cause of a real, reported bug: re-laying-out the *entire* window
  // every single second, unconditionally, was disrupting whatever grid
  // cell happened to be actively being edited at that moment (visible
  // in the log as a repeating BeginEdit/EndEdit cycle with no
  // corresponding key event, roughly once a second) -- on Windows
  // specifically, severely enough to make it effectively impossible to
  // finish typing or selecting anything in a dropdown cell before the
  // next tick tore the edit down again. wxSizer::Layout() recomputes
  // just that sizer's own children, achieving the same intended effect
  // (the status bar's wrap sizer actually re-wrapping) without
  // touching anything else in the window at all.
  if (m_statusBarSizer) m_statusBarSizer->Layout();
}

void LogWindow::BuildSummaryTab() {
  wxScrolledWindow* panel = new wxScrolledWindow(m_notebook);
  panel->SetScrollRate(0, 10);
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  // Plain text throughout (not a wxListCtrl-based table) -- an earlier
  // version used tables for the Sightings/Events breakdowns, but
  // wxListCtrl natively captures mouse wheel events for its own
  // possible internal scrolling and doesn't let them propagate to this
  // panel, making it impossible to scroll the tab at all while the
  // mouse was over either table. A couple of workarounds were tried
  // (forwarding the wheel event, then explicitly computing and applying
  // the scroll) without a confirmed fix; switching to plain text
  // removes the problem entirely, since a wxStaticText has no
  // scrolling behavior of its own to compete with the panel's.
  m_summaryMetricsLabel = new wxStaticText(panel, wxID_ANY, "");
  sizer->Add(m_summaryMetricsLabel, 0, wxALL, 10);

  wxButton* refreshBtn = new wxButton(panel, wxID_ANY, "Refresh");
  refreshBtn->Bind(wxEVT_BUTTON,
                   [this](wxCommandEvent&) { RefreshSummaryTab(); });
  sizer->Add(refreshBtn, 0, wxALL, 10);

  panel->SetSizer(sizer);
  m_notebook->AddPage(panel, "Summary");
}

namespace {
// "1 sighting" vs "3 sightings", "0 calves" vs "1 calf" vs "2 calves",
// etc.
wxString Plural(int n, const wxString& singular, const wxString& plural) {
  return wxString::Format("%d %s", n, n == 1 ? singular : plural);
}

// Formats a summed count (individuals or calves) for display, handling
// three distinct cases:
// - Not a single contributing sighting had a value at all: "NA calves"
//   -- there's no sum to speak of, not even a confirmed zero, so
//   showing a number here (even "0") would misrepresent a total
//   absence of data as a confirmed observation of none.
// - Some sightings had a value and at least one didn't: "3 individuals
//   (+ NA)" -- the sum is real but a floor, not a confirmed total.
// - Every contributing sighting had a value: plain "3 individuals".
wxString FormatKnownCount(int sum, int knownCount, bool hasUnknown,
                          const wxString& singular, const wxString& plural) {
  if (knownCount == 0) return "NA " + plural;
  wxString text = Plural(sum, singular, plural);
  if (hasUnknown) text += " (+ NA)";
  return text;
}

// Same idea as FormatKnownCount(), but for a plain numeric CSV column
// (no trailing "individuals"/"calves" word) -- "NA", "3", or "3 (+ NA)".
wxString FormatKnownCountForCsv(int sum, int knownCount, bool hasUnknown) {
  if (knownCount == 0) return "NA";
  wxString text = wxString::Format("%d", sum);
  if (hasUnknown) text += " (+ NA)";
  return text;
}
}  // namespace

void LogWindow::RefreshSummaryTab() {
  if (!m_summaryMetricsLabel) return;
  SurveySummary s = ComputeSummary();

  wxString text;
  text << "Survey period: "
       << (s.trackStartTime.IsValid() && s.trackEndTime.IsValid()
               ? s.trackStartTime.Format("%Y-%m-%d %H:%M:%S") + " to " +
                     s.trackEndTime.Format("%Y-%m-%d %H:%M:%S")
               : wxString("--"))
       << "\n";
  text << "Trackline: "
       << (s.haveTrack ? wxString::Format("%.2f nm", s.trackNm)
                       : wxString("--"))
       << " over "
       << (s.haveTrack ? s.trackTime.Format("%H:%M:%S") : wxString("--"))
       << "\n";
  text << "On-effort: "
       << (s.haveEffort ? wxString::Format("%.2f nm", s.effortNm)
                        : wxString("--"))
       << " over "
       << (s.haveEffort ? s.effortTime.Format("%H:%M:%S") : wxString("--"))
       << "\n";
  text << "Sightings: " << s.numSightings << " total, " << s.numSpeciesSighted
       << " species\n";
  text << "Bounding box: "
       << (s.haveBoundingBox
               ? wxString::Format("%.4f to %.4f lat, %.4f to %.4f lon",
                                  s.minLat, s.maxLat, s.minLon, s.maxLon)
               : wxString("--"))
       << "\n";
  text << "Visibility: "
       << (s.haveVis ? wxString::Format("%.1f to %.1f nm", s.minVis, s.maxVis)
                     : wxString("--"))
       << "\n";
  text << "Beaufort: "
       << (s.haveBeaufort
               ? wxString::Format("%.0f to %.0f", s.minBeaufort, s.maxBeaufort)
               : wxString("--"))
       << "\n";
  wxString weatherDisplay;
  for (size_t i = 0; i < s.uniqueWeather.size(); i++) {
    if (i > 0) weatherDisplay << ", ";
    weatherDisplay << s.uniqueWeather[i];
  }
  text << "Weather conditions encountered: "
       << (s.uniqueWeather.IsEmpty() ? wxString("--") : weatherDisplay)
       << "\n\n";

  text << "Sightings Breakdown:\n";
  if (s.speciesBreakdown.empty()) {
    text << "    (none yet)\n";
  } else {
    for (const auto& sp : s.speciesBreakdown) {
      wxString individualsText = FormatKnownCount(
          sp.individuals, sp.knownIndividualsCount, sp.hasUnknownIndividuals,
          "individual", "individuals");
      wxString calvesText =
          FormatKnownCount(sp.calves, sp.knownCalvesCount, sp.hasUnknownCalves,
                           "calf", "calves");
      text << "    " << sp.species << ": "
           << Plural(sp.sightings, "sighting", "sightings") << ", "
           << individualsText << ", " << calvesText << "\n";
    }
  }
  text << "\n";

  text << "Events Breakdown:\n";
  if (s.eventBreakdown.empty()) {
    text << "    (none yet)\n";
  } else {
    for (const auto& ec : s.eventBreakdown) {
      text << "    " << ec.eventType << ": " << ec.count << "\n";
    }
  }

  m_summaryMetricsLabel->SetLabel(text);

  if (m_summaryMetricsLabel->GetParent()) {
    m_summaryMetricsLabel->GetParent()->Layout();
    m_summaryMetricsLabel->GetParent()->FitInside();
  }
}

void LogWindow::BuildSettingsTab() {
  // wxScrolledWindow (not a plain wxPanel) -- reported as needed since
  // not every control fit on screen in Split Horizontal (the plugin
  // only gets 40% of the screen height there), and there was no way to
  // reach the ones below the fold.
  wxScrolledWindow* panel = new wxScrolledWindow(m_notebook);
  panel->SetScrollRate(0, 10);  // vertical only
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  // Descriptive paragraphs are wrapped to the panel's *current* width
  // whenever it resizes (see the wxEVT_SIZE binding at the end of this
  // function) rather than a fixed pixel width -- reported as getting cut
  // off in Split Vertical, where this tab only gets 40% of the screen's
  // width, considerably narrower than this window's normal default size.
  std::vector<wxStaticText*> wrappingLabels;
  auto addWrappingText = [&](wxSizer* targetSizer, const wxString& text) {
    wxStaticText* label = new wxStaticText(panel, wxID_ANY, text);
    targetSizer->Add(label, 0, wxALL, 8);
    wrappingLabels.push_back(label);
  };

  // --- General ---------------------------------------------------------
  wxStaticBoxSizer* generalBox =
      new wxStaticBoxSizer(wxVERTICAL, panel, "General");

  // Lat/Lon format -- moved here from every tab's own toolbar per an
  // earlier direct request (this is a single global format, see
  // LatLonFormat, not a per-tab setting, so having it repeated on each
  // tab's toolbar was more clutter than it was worth), and grouped into
  // this new General section alongside Timezone below per this round's
  // direct request. A dropdown listing all three formats by name
  // (rather than a button that cycles through them one click at a
  // time) -- lets you jump straight to the one you want and see which
  // one is currently active without guessing. Wherever it's changed
  // from, it both changes the setting and refreshes every tab's display
  // (each one showing the same lat/lon values in the new format) plus
  // the status bar's own vessel-position readout.
  wxBoxSizer* latLonRow = new wxBoxSizer(wxHORIZONTAL);
  latLonRow->Add(new wxStaticText(panel, wxID_ANY, "Lat/Lon format:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxALL, 8);
  wxArrayString formatOptions;
  formatOptions.Add("Decimal Degrees");
  formatOptions.Add("Degrees Decimal Minutes");
  formatOptions.Add("Degrees Minutes Seconds");
  wxChoice* formatChoice = new wxChoice(panel, wxID_ANY, wxDefaultPosition,
                                        wxDefaultSize, formatOptions);
  m_latLonFormatChoice = formatChoice;
  formatChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& evt) {
    LatLonFormat::Set(static_cast<LatLonFormat::Format>(evt.GetSelection()));
    LatLonFormat::SaveToFile(
        wxFileName(m_plugin->GetSettingsDir(), "latlon_format.txt")
            .GetFullPath());
    for (auto& tab : m_tabs) tab->RefreshDisplay();
    OnStatusTick();
  });
  latLonRow->Add(formatChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
  generalBox->Add(latLonRow, 0);

  // Timezone -- an explicit override for recorded timestamps (Time
  // column on every tab, and track.csv), per direct request. Defaults
  // to "System Default" (this plugin's original behavior: whatever the
  // computer's own configured timezone is) so nothing changes unless a
  // specific zone is chosen -- added specifically because relying on
  // the computer's own timezone can get messy if a survey spans
  // multiple timezones and the computer's own zone changes mid-survey
  // (some systems auto-adjust it based on location). Unlike Lat/Lon
  // format, changing this does *not* retroactively change already-
  // recorded timestamps (they're written once, at row-creation time,
  // not reformatted on every display the way Lat/Lon values are) --
  // only rows added after the change use the new zone.
  wxBoxSizer* timezoneRow = new wxBoxSizer(wxHORIZONTAL);
  timezoneRow->Add(new wxStaticText(panel, wxID_ANY, "Timezone:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxALL, 8);
  wxArrayString timezoneOptions;
  for (const auto& zone : TimeZoneSetting::AllZones()) {
    timezoneOptions.Add(zone.name);
  }
  wxChoice* timezoneChoiceCtrl = new wxChoice(
      panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, timezoneOptions);
  m_timezoneChoice = timezoneChoiceCtrl;
  timezoneChoiceCtrl->Bind(wxEVT_CHOICE, [this](wxCommandEvent& evt) {
    TimeZoneSetting::Set(evt.GetSelection());
    TimeZoneSetting::SaveToFile(
        wxFileName(m_plugin->GetSettingsDir(), "timezone.txt").GetFullPath());
  });
  timezoneRow->Add(timezoneChoiceCtrl, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
  generalBox->Add(timezoneRow, 0);
  addWrappingText(generalBox,
                  "Applies to new rows going forward only -- doesn't "
                  "retroactively change timestamps already recorded.");

  sizer->Add(generalBox, 0, wxEXPAND | wxALL, 8);

  // --- Tracking -----------------------------------------------------
  wxStaticBoxSizer* trackingBox =
      new wxStaticBoxSizer(wxVERTICAL, panel, "Tracking");
  TrackRecorder* track = m_plugin->GetTrackRecorder();
  TrackingSettings* settings = m_plugin->GetTrackingSettings();
  bool enabled = settings ? settings->Enabled() : true;
  int intervalSeconds = settings ? settings->IntervalSeconds() : 10;

  wxCheckBox* enabledCheck = new wxCheckBox(panel, wxID_ANY, "Enable tracking");
  enabledCheck->SetValue(enabled);
  enabledCheck->Bind(wxEVT_CHECKBOX, [track, settings](wxCommandEvent& evt) {
    bool on = evt.IsChecked();
    if (track) track->SetEnabled(on);
    if (settings) settings->SetEnabled(on);
  });
  m_trackingEnabledCheck = enabledCheck;
  trackingBox->Add(enabledCheck, 0, wxALL, 8);

  wxBoxSizer* intervalRow = new wxBoxSizer(wxHORIZONTAL);
  intervalRow->Add(
      new wxStaticText(panel, wxID_ANY, "Recording interval (seconds, 1-300):"),
      0, wxALIGN_CENTER_VERTICAL);
  wxSpinCtrl* intervalCtrl =
      new wxSpinCtrl(panel, wxID_ANY, wxString::Format("%d", intervalSeconds),
                     wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 300,
                     intervalSeconds);
  auto applyTrackInterval = [track, settings](int secs) {
    if (track) track->SetIntervalSeconds(secs);
    if (settings) settings->SetIntervalSeconds(secs);
  };
  intervalCtrl->Bind(wxEVT_SPINCTRL, [applyTrackInterval](wxSpinEvent& evt) {
    applyTrackInterval(evt.GetPosition());
  });
  // Also applied (and any lingering text selection cleared) on losing
  // focus -- reported as confusing whether a typed-in (rather than
  // arrow-clicked) value had actually taken effect, and separately,
  // that the control couldn't be "deselected." wxEVT_SPINCTRL isn't
  // guaranteed to fire for every way a value can change on every
  // platform; explicitly re-applying GetValue() here on focus-loss
  // means clicking or tabbing away always commits whatever's currently
  // shown, regardless of how it got there. Explicitly clearing the
  // text selection at the same time addresses the "can't deselect it"
  // half directly -- clicking elsewhere is the natural way to indicate
  // "I'm done with this field," and it should look that way too.
  intervalCtrl->Bind(wxEVT_KILL_FOCUS,
                     [intervalCtrl, applyTrackInterval](wxFocusEvent& evt) {
                       applyTrackInterval(intervalCtrl->GetValue());
                       intervalCtrl->SetSelection(0, 0);
                       evt.Skip();
                     });
  intervalRow->Add(intervalCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
  trackingBox->Add(intervalRow, 0, wxALL, 8);

  DisplaySettings* displaySettings = m_plugin->GetDisplaySettings();
  wxBoxSizer* trackDisplayRow = new wxBoxSizer(wxHORIZONTAL);
  wxCheckBox* trackVisibleCheck =
      new wxCheckBox(panel, wxID_ANY, "Show track on map");
  trackVisibleCheck->SetValue(displaySettings ? displaySettings->TrackVisible()
                                              : true);
  trackVisibleCheck->Bind(wxEVT_CHECKBOX,
                          [this, displaySettings](wxCommandEvent& evt) {
                            if (displaySettings)
                              displaySettings->SetTrackVisible(evt.IsChecked());
                            m_plugin->RequestOverlayRedraw();
                          });
  trackDisplayRow->Add(trackVisibleCheck, 0, wxALIGN_CENTER_VERTICAL);

  trackDisplayRow->Add(new wxStaticText(panel, wxID_ANY, "Color:"), 0,
                       wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
  wxColour defaultTrackColor(160, 30, 200);
  wxButton* trackColorBtn =
      new wxButton(panel, wxID_ANY, "", wxDefaultPosition, wxSize(50, -1));
  trackColorBtn->SetBackgroundColour(
      displaySettings ? displaySettings->TrackColor(defaultTrackColor)
                      : defaultTrackColor);
  trackColorBtn->Bind(wxEVT_BUTTON, [this, panel, displaySettings,
                                     trackColorBtn](wxCommandEvent&) {
    wxColourData data;
    data.SetColour(trackColorBtn->GetBackgroundColour());
    wxColourDialog dlg(panel, &data);
    if (dlg.ShowModal() != wxID_OK) return;
    wxColour chosen = dlg.GetColourData().GetColour();
    trackColorBtn->SetBackgroundColour(chosen);
    trackColorBtn->Refresh();
    if (displaySettings) displaySettings->SetTrackColor(chosen);
    m_plugin->RequestOverlayRedraw();
  });
  trackDisplayRow->Add(trackColorBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
  trackingBox->Add(trackDisplayRow, 0, wxALL, 8);

  addWrappingText(
      trackingBox,
      "Record the vessel position at a given interval and display on "
      "the map. Also included in track.csv. This is this plugin's own "
      "internal tracking, separate from (and not connected to) "
      "OpenCPN's native tracking feature, which the plugin API "
      "doesn't expose a way to control. Turns on automatically the "
      "first time \"Start New Survey\" is used, or Effort is set to "
      "ON, and then stays on (including across restarts) until turned "
      "off here.");
  sizer->Add(trackingBox, 0, wxEXPAND | wxALL, 8);

  // --- Effort (reminder interval + effort-segment overlay display) ----
  wxStaticBoxSizer* reminderBox =
      new wxStaticBoxSizer(wxVERTICAL, panel, "Effort");
  wxBoxSizer* reminderRow = new wxBoxSizer(wxHORIZONTAL);
  reminderRow->Add(new wxStaticText(panel, wxID_ANY, "Remind every"), 0,
                   wxALIGN_CENTER_VERTICAL);
  int currentReminderMin =
      m_environmental ? m_environmental->GetReminderIntervalMinutes() : 30;
  m_envIntervalCtrl = new wxSpinCtrl(
      panel, wxID_ANY, wxString::Format("%d", currentReminderMin),
      wxDefaultPosition, wxSize(55, -1), wxSP_ARROW_KEYS, 1, 180,
      currentReminderMin);
  if (m_environmental) {
    m_envIntervalCtrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& evt) {
      m_environmental->SetReminderIntervalMinutes(evt.GetPosition());
    });
    // Same reasoning as the tracking interval control just above:
    // applied again (and text selection cleared) on losing focus, so a
    // typed-in value is guaranteed to take effect even if
    // wxEVT_SPINCTRL doesn't fire for it on a given platform, and so
    // the field visually looks "done with" once you click away.
    m_envIntervalCtrl->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& evt) {
      if (m_environmental) {
        m_environmental->SetReminderIntervalMinutes(
            m_envIntervalCtrl->GetValue());
      }
      m_envIntervalCtrl->SetSelection(0, 0);
      evt.Skip();
    });
  }
  reminderRow->Add(m_envIntervalCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
  reminderRow->Add(new wxStaticText(panel, wxID_ANY, "minutes"), 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  reminderBox->Add(reminderRow, 0, wxALL, 8);
  addWrappingText(reminderBox,
                  "The countdown itself is always shown in the status "
                  "bar. Adding a row to the Effort tab resets it.");

  wxBoxSizer* segDisplayRow = new wxBoxSizer(wxHORIZONTAL);
  wxCheckBox* segVisibleCheck =
      new wxCheckBox(panel, wxID_ANY, "Show effort segments on map");
  segVisibleCheck->SetValue(
      displaySettings ? displaySettings->EffortSegmentsVisible() : true);
  segVisibleCheck->Bind(
      wxEVT_CHECKBOX, [this, displaySettings](wxCommandEvent& evt) {
        if (displaySettings) {
          displaySettings->SetEffortSegmentsVisible(evt.IsChecked());
        }
        m_plugin->RequestOverlayRedraw();
      });
  segDisplayRow->Add(segVisibleCheck, 0, wxALIGN_CENTER_VERTICAL);

  segDisplayRow->Add(new wxStaticText(panel, wxID_ANY, "Color:"), 0,
                     wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
  wxColour defaultSegColor(0, 160, 60);
  wxButton* segColorBtn =
      new wxButton(panel, wxID_ANY, "", wxDefaultPosition, wxSize(50, -1));
  segColorBtn->SetBackgroundColour(
      displaySettings ? displaySettings->EffortSegmentColor(defaultSegColor)
                      : defaultSegColor);
  segColorBtn->Bind(wxEVT_BUTTON, [this, panel, displaySettings,
                                   segColorBtn](wxCommandEvent&) {
    wxColourData data;
    data.SetColour(segColorBtn->GetBackgroundColour());
    wxColourDialog dlg(panel, &data);
    if (dlg.ShowModal() != wxID_OK) return;
    wxColour chosen = dlg.GetColourData().GetColour();
    segColorBtn->SetBackgroundColour(chosen);
    segColorBtn->Refresh();
    if (displaySettings) displaySettings->SetEffortSegmentColor(chosen);
    m_plugin->RequestOverlayRedraw();
  });
  segDisplayRow->Add(segColorBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
  reminderBox->Add(segDisplayRow, 0, wxALL, 8);
  addWrappingText(
      reminderBox,
      "Effort segments are drawn along the trackline wherever the "
      "Effort tab's Effort column was ON, using the SegNo column to "
      "tell separate on-effort periods apart.");

  sizer->Add(reminderBox, 0, wxEXPAND | wxALL, 8);

  // --- Map: Sightings ---------------------------------------------------
  wxStaticBoxSizer* sightingsMapBox =
      new wxStaticBoxSizer(wxVERTICAL, panel, "Sightings");
  sightingsMapBox->Add(
      new wxStaticText(panel, wxID_ANY,
                       "Sightings label (in order, space-separated):"),
      0, wxALL, 8);
  wxBoxSizer* labelColsRow = new wxBoxSizer(wxHORIZONTAL);
  // A fixed, curated set of Sightings columns that make sense as a
  // short map label -- not every column (Notes or Behavs, for example,
  // would make an unreadable label).
  const wxString kLabelColumnChoices[] = {"SightNo", "Species", "SpecConf",
                                          "FieldID", "Num",     "NumCalf"};
  wxArrayString currentLabelCols =
      m_plugin->GetDisplaySettings()
          ? m_plugin->GetDisplaySettings()->SightingsLabelColumns()
          : wxArrayString();
  std::vector<wxCheckBox*> labelColChecks;
  for (const auto& colName : kLabelColumnChoices) {
    wxCheckBox* cb = new wxCheckBox(panel, wxID_ANY, colName);
    cb->SetValue(currentLabelCols.Index(colName) != wxNOT_FOUND);
    labelColsRow->Add(cb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    labelColChecks.push_back(cb);
  }
  DisplaySettings* displaySettingsForMap = displaySettings;
  auto updateLabelCols = [this, labelColChecks, displaySettingsForMap]() {
    wxArrayString cols;
    for (wxCheckBox* cb : labelColChecks) {
      if (cb->GetValue()) cols.Add(cb->GetLabel());
    }
    if (displaySettingsForMap) {
      displaySettingsForMap->SetSightingsLabelColumns(cols);
    }
    m_plugin->RequestOverlayRedraw();
  };
  for (wxCheckBox* cb : labelColChecks) {
    cb->Bind(wxEVT_CHECKBOX,
             [updateLabelCols](wxCommandEvent&) { updateLabelCols(); });
  }
  sightingsMapBox->Add(labelColsRow, 0, wxALL, 8);
  addWrappingText(sightingsMapBox,
                  "Controls the text label drawn next to each "
                  "Sightings marker on the chart. Defaults to Species "
                  "and FieldID (e.g. \"Right whale A\").");
  m_sightingsMarkerShapeChoice = BuildMarkerControlsRow(
      panel, sightingsMapBox, m_plugin->GetDisplaySettings(), "Sightings",
      "Diamond", [this]() { m_plugin->RequestOverlayRedraw(); });
  sizer->Add(sightingsMapBox, 0, wxEXPAND | wxALL, 8);

  // --- Map: Events --------------------------------------------------
  wxStaticBoxSizer* eventsMapBox =
      new wxStaticBoxSizer(wxVERTICAL, panel, "Events");
  eventsMapBox->Add(
      new wxStaticText(panel, wxID_ANY,
                       "Event label (in order, space-separated):"),
      0, wxALL, 8);
  wxBoxSizer* eventLabelColsRow = new wxBoxSizer(wxHORIZONTAL);
  const wxString kEventLabelColumnChoices[] = {"EventNo", "Event", "ID"};
  wxArrayString currentEventLabelCols =
      m_plugin->GetDisplaySettings()
          ? m_plugin->GetDisplaySettings()->EventsLabelColumns()
          : wxArrayString();
  std::vector<wxCheckBox*> eventLabelColChecks;
  for (const auto& colName : kEventLabelColumnChoices) {
    wxCheckBox* cb = new wxCheckBox(panel, wxID_ANY, colName);
    cb->SetValue(currentEventLabelCols.Index(colName) != wxNOT_FOUND);
    eventLabelColsRow->Add(cb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    eventLabelColChecks.push_back(cb);
  }
  auto updateEventLabelCols = [this, eventLabelColChecks,
                               displaySettingsForMap]() {
    wxArrayString cols;
    for (wxCheckBox* cb : eventLabelColChecks) {
      if (cb->GetValue()) cols.Add(cb->GetLabel());
    }
    if (displaySettingsForMap) {
      displaySettingsForMap->SetEventsLabelColumns(cols);
    }
    m_plugin->RequestOverlayRedraw();
  };
  for (wxCheckBox* cb : eventLabelColChecks) {
    cb->Bind(wxEVT_CHECKBOX, [updateEventLabelCols](wxCommandEvent&) {
      updateEventLabelCols();
    });
  }
  eventsMapBox->Add(eventLabelColsRow, 0, wxALL, 8);
  addWrappingText(eventsMapBox,
                  "Controls the text label drawn next to each Events "
                  "marker on the chart. Defaults to Event and ID.");
  m_eventsMarkerShapeChoice = BuildMarkerControlsRow(
      panel, eventsMapBox, m_plugin->GetDisplaySettings(), "Events", "Square",
      [this]() { m_plugin->RequestOverlayRedraw(); });
  sizer->Add(eventsMapBox, 0, wxEXPAND | wxALL, 8);

  // --- Files ----------------------------------------------------------
  wxStaticBoxSizer* filesBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Files");
  auto addLink = [&](const wxString& text, const wxString& path,
                     const wxString& description) {
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    wxHyperlinkCtrl* link =
        new wxHyperlinkCtrl(panel, wxID_ANY, text, wxEmptyString);
    // A visible, consistent color regardless of dark/light OpenCPN
    // themes, and deliberately the *same* color before and after being
    // clicked -- the default visited-link color tends to be a dim
    // purple that's very hard to read against a dark background, and
    // there's no real reason these particular links (which just open a
    // file or folder, not a one-time action) should look different
    // once used.
    wxColour linkColour(90, 150, 240);
    link->SetNormalColour(linkColour);
    link->SetVisitedColour(linkColour);
    link->SetHoverColour(linkColour.ChangeLightness(130));
    link->Bind(wxEVT_HYPERLINK, [this, path](wxHyperlinkEvent&) {
      if (!wxLaunchDefaultApplication(path)) {
        wxMessageBox("Couldn't open it automatically. It's here:\n" + path,
                     "Spotter", wxOK | wxICON_INFORMATION, this);
      }
    });
    row->Add(link, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    if (!description.IsEmpty()) {
      // A separate, muted-color static text rather than folding this
      // into the link's own label -- keeps the clickable link text
      // itself short and unambiguous, with the explanation clearly
      // set apart as non-interactive.
      wxStaticText* desc = new wxStaticText(panel, wxID_ANY, description);
      desc->SetForegroundColour(wxColour(140, 140, 140));
      row->Add(desc, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    }
    filesBox->Add(row, 0, wxALL, 2);
  };
  addLink("Open data folder", m_plugin->GetSurveyDataDir(),
          "-- where all of this survey's CSV files live");
  addLink("Edit species list", m_speciesConfig.GetPath(),
          "-- names, default map colors, and species codes");
  addLink("Edit events list", m_eventsConfig.GetPath(),
          "-- names and default map colors");
  addLink("Edit observers list", m_observersConfig.GetPath(),
          "-- names and full names");
  addLink("Edit behaviors list", m_behaviorsConfig.GetPath(),
          "-- names and behavior codes");
  addLink("Edit keyboard shortcuts", m_shortcuts.GetPath(),
          "-- customize keyboard shortcuts for adding rows quickly");
  addLink("Edit observer positions/heights", m_positionHeights.GetPath(),
          "-- eye height above water, for reticle-based distance "
          "estimates");
  addLink("Edit column definitions", m_columnDefinitions.GetPath(),
          "-- the help text shown above the buttons when a cell is "
          "selected");
  addLink("Edit marker/label/line sizes",
          m_plugin->GetDisplaySettings()
              ? m_plugin->GetDisplaySettings()->GetPath()
              : wxString(wxEmptyString),
          "-- marker, label, and trackline sizes/thickness on the chart");
  sizer->Add(filesBox, 0, wxEXPAND | wxALL, 8);

  panel->SetSizer(sizer);
  panel->Bind(wxEVT_SIZE, [panel, wrappingLabels](wxSizeEvent& evt) {
    int width = panel->GetClientSize().GetWidth() - 40;  // margin for
                                                         // the group
                                                         // box border
                                                         // and padding
    if (width > 100) {
      for (wxStaticText* label : wrappingLabels) {
        label->Wrap(width);
      }
      panel->GetSizer()->Layout();
      panel->FitInside();
    }
    evt.Skip();
  });
  m_notebook->AddPage(panel, "Settings");
}

void LogWindow::BuildTabs() {
  wxArrayString speciesChoices = m_speciesConfig.Names();
  wxArrayString eventTypeChoices = m_eventsConfig.Names();
  wxArrayString observerChoices = m_observersConfig.Names();
  wxArrayString behaviorChoices = m_behaviorsConfig.Names();

  // ---- Sightings ---------------------------------------------------
  {
    DataTabConfig cfg;
    cfg.title = "Sightings";
    cfg.csvFilename = "sightings.csv";
    ColumnDef behaviorsCol("Behavs", ColumnDef::MULTI_CHOICE, 170);
    behaviorsCol.choices = behaviorChoices;
    cfg.columns = {
        // SightNo first per direct request.
        ColumnDef("SightNo", ColumnDef::TEXT, 70),
        ColumnDef("Map", ColumnDef::BOOL, 50),
        ColumnDef("Time", ColumnDef::TEXT, 90),
        ColumnDef("Lat", ColumnDef::TEXT, 90),
        ColumnDef("Lon", ColumnDef::TEXT, 90),
        ColumnDef("Species", speciesChoices),
        OrderPreserved(ColumnDef(
            "SpecConf",
            ToArrayString(kConfidenceChoices, WXSIZEOF(kConfidenceChoices)),
            100)),
        ColumnDef("Num", ColumnDef::TEXT, 60),
        OrderPreserved(ColumnDef(
            "NumConf",
            ToArrayString(kNumConfChoices, WXSIZEOF(kNumConfChoices)), 100)),
        ColumnDef("NumCalf", ColumnDef::TEXT, 70),
        ColumnDef("AnHead", ColumnDef::TEXT, 80),
        ColumnDef("BearingMag", ColumnDef::TEXT, 90),
        ColumnDef("Dist", ColumnDef::TEXT, 70),
        WithDefault(ColumnDef("DistUnit",
                              ToArrayString(kDistUnitChoices,
                                            WXSIZEOF(kDistUnitChoices)),
                              90),
                    "nm"),
        ColumnDef("SigLat", ColumnDef::TEXT, 90),
        ColumnDef("SigLon", ColumnDef::TEXT, 90),
        ColumnDef("Obs", observerChoices),
        ColumnDef("Img",
                  ToArrayString(kImagesCollectedChoices,
                                WXSIZEOF(kImagesCollectedChoices)),
                  110),
        ColumnDef("FieldID", ColumnDef::TEXT, 70),
        behaviorsCol,
        ColumnDef("Notes", ColumnDef::TEXT, 220),
        // Observer height (feet) at the moment this row was created --
        // used by the "reticles" DistUnit's distance calculation. Kept
        // HIDDEN (not useful to look at directly) and deliberately
        // *trailing*: an earlier round found that a non-trailing hidden
        // column silently breaks every column lookup declared after it
        // (GenericGridTable::FindColByName() returns a raw index;
        // wxGrid expects a visible one; they only coincide when hidden
        // columns are last).
        ColumnDef("ObsHeightFt", ColumnDef::HIDDEN),
    };
    auto idx = [&](const wxString& name) {
      for (size_t i = 0; i < cfg.columns.size(); i++)
        if (cfg.columns[i].name == name) return static_cast<int>(i);
      return -1;
    };
    cfg.chartCol = idx("Map");
    cfg.chartColorKeyCol = idx("Species");
    cfg.timeCol = idx("Time");
    cfg.latCol = idx("Lat");
    cfg.lonCol = idx("Lon");
    cfg.overlayLatCol = idx("SigLat");
    cfg.overlayLonCol = idx("SigLon");
    cfg.overlayTextCol = idx("SightNo");
    cfg.autoIncrementCol = idx("SightNo");
    cfg.labelCol = idx("Species");
    // The map label is configurable (see the Settings tab's Map
    // section) -- defaults to Species + FieldID. Re-reads the current
    // setting on every call (not captured once here), so changing it
    // in Settings takes effect immediately without needing to rebuild
    // the tab.
    cfg.labelTextFn = [this](GenericGridTable* t, int row) {
      wxArrayString cols =
          m_plugin->GetDisplaySettings()
              ? m_plugin->GetDisplaySettings()->SightingsLabelColumns()
              : wxArrayString();
      wxString label;
      for (const auto& colName : cols) {
        int col = t->FindColByName(colName);
        if (col < 0) continue;
        wxString v = t->RawGet(row, col);
        if (v.IsEmpty()) continue;
        if (!label.IsEmpty()) label += " ";
        label += v;
      }
      return label;
    };
    cfg.columns[idx("Lat")].geoRole = ColumnDef::GeoRole::Latitude;
    cfg.columns[idx("Lon")].geoRole = ColumnDef::GeoRole::Longitude;
    cfg.columns[idx("SigLat")].geoRole = ColumnDef::GeoRole::Latitude;
    cfg.columns[idx("SigLon")].geoRole = ColumnDef::GeoRole::Longitude;
    // A not-yet-identified sighting shouldn't silently look identified:
    // leave Species/IDrel/etc. genuinely blank on a new row rather than
    // defaulting to whatever's listed first, so the observer has to
    // actively fill them in.
    cfg.defaultChoicesToFirstOption = false;
    cfg.recompute = [](DataTab* tab, GenericGridTable* t, int row,
                       int editedCol) {
      RecomputeBearingDistancePosition(tab, t, row, editedCol, "SigLat",
                                       "SigLon");
    };
    m_tabs.push_back(std::make_unique<DataTab>(
        m_notebook, cfg, m_plugin->GetSurveyDataDir(), m_filePrefix));
    m_sightings = m_tabs.back().get();
  }

  // ---- Effort (environmental conditions + effort ON/OFF, merged) -----
  // Two originally-separate tabs, merged into one: an Effort column was
  // added to what used to be the standalone Environmental tab, and the
  // old standalone Effort tab was removed. The C++ identifiers
  // (m_environmental / Environmental()) stay as they were, since the
  // data is still primarily environmental conditions -- only the
  // user-visible tab title changes to "Effort".
  {
    DataTabConfig cfg;
    cfg.title = "Effort";
    cfg.csvFilename = "effort.csv";
    // Notes are inherently a one-off observation -- repeating the
    // previous check's freeform text would almost never be wanted, even
    // though the other conditions below usually carry over unchanged.
    ColumnDef notesCol("Notes", ColumnDef::TEXT, 220);
    notesCol.skipInherit = true;
    // SegNo is entirely derived (see RecomputeEffortSegNo) -- never
    // inherited or hand-edited.
    ColumnDef segNoCol("SegNo", ColumnDef::TEXT, 60);
    segNoCol.skipInherit = true;
    wxArrayString positionChoices = m_positionHeights.GetPositionNames();
    cfg.columns = {
        // EffortNo first, behaving like SightNo -- auto-incremented, an
        // easy stable reference number for this row.
        ColumnDef("EffortNo", ColumnDef::TEXT, 80),
        ColumnDef("Time", ColumnDef::TEXT, 90),
        ColumnDef("Lat", ColumnDef::TEXT, 90),
        ColumnDef("Lon", ColumnDef::TEXT, 90),
        ColumnDef(
            "Effort",
            ToArrayString(kEffortStatusChoices, WXSIZEOF(kEffortStatusChoices)),
            80),
        segNoCol,
        ColumnDef("Position", positionChoices),
        ColumnDef("Vis", ColumnDef::TEXT, 60),
        ColumnDef("Beaufort",
                  ToArrayString(kSeaStateChoices, WXSIZEOF(kSeaStateChoices)),
                  80),
        ColumnDef("Weather",
                  ToArrayString(kWeatherChoices, WXSIZEOF(kWeatherChoices)),
                  120),
        OrderPreserved(ColumnDef(
            "Glare", ToArrayString(kGlareChoices, WXSIZEOF(kGlareChoices)),
            90)),
        ColumnDef("GlareBegin", ColumnDef::TEXT, 90),
        ColumnDef("GlareEnd", ColumnDef::TEXT, 90),
        ColumnDef("Port", observerChoices),
        ColumnDef("Recorder", observerChoices),
        ColumnDef("Starboard", observerChoices),
        notesCol,
    };
    auto idx = [&](const wxString& name) {
      for (size_t i = 0; i < cfg.columns.size(); i++)
        if (cfg.columns[i].name == name) return static_cast<int>(i);
      return -1;
    };
    cfg.timeCol = idx("Time");
    cfg.latCol = idx("Lat");
    cfg.lonCol = idx("Lon");
    cfg.autoIncrementCol = idx("EffortNo");
    cfg.labelCol = idx("Effort");
    cfg.columns[idx("Lat")].geoRole = ColumnDef::GeoRole::Latitude;
    cfg.columns[idx("Lon")].geoRole = ColumnDef::GeoRole::Longitude;
    // Conditions (sea state, weather, who's on watch) -- and effort
    // status -- usually haven't changed since the last check: starting a
    // new row from those same values is a better default than blank or
    // "first item in the list".
    cfg.inheritPreviousRowValues = true;
    cfg.enableReminderTimer = true;
    cfg.reminderMinutes = 30;
    cfg.recompute = RecomputeEffortSegNo;
    m_tabs.push_back(std::make_unique<DataTab>(
        m_notebook, cfg, m_plugin->GetSurveyDataDir(), m_filePrefix));
    m_environmental = m_tabs.back().get();
    // Being on effort without a track being recorded doesn't make
    // sense -- if Effort is ever set to ON (a direct edit, or inherited
    // onto a new row from the previous one), make sure tracking is
    // turned on too, the same as "Start New Survey" does.
    m_environmental->WatchColumnValue("Effort", "ON", [this]() {
      if (m_plugin->GetTrackRecorder() &&
          !m_plugin->GetTrackRecorder()->IsEnabled()) {
        m_plugin->GetTrackRecorder()->SetEnabled(true);
      }
      if (m_plugin->GetTrackingSettings() &&
          !m_plugin->GetTrackingSettings()->Enabled()) {
        m_plugin->GetTrackingSettings()->SetEnabled(true);
      }
    });
  }

  // ---- Events (CTD casts, drifters, drone flights, tagging, etc.) -----
  {
    DataTabConfig cfg;
    cfg.title = "Events";
    cfg.csvFilename = "events.csv";
    cfg.columns = {
        // EventNo first, behaving like SightNo -- auto-incremented, an
        // easy stable reference number for this row.
        ColumnDef("EventNo", ColumnDef::TEXT, 80),
        ColumnDef("Map", ColumnDef::BOOL, 50),
        ColumnDef("Time", ColumnDef::TEXT, 90),
        ColumnDef("Lat", ColumnDef::TEXT, 90),
        ColumnDef("Lon", ColumnDef::TEXT, 90),
        ColumnDef("Event", eventTypeChoices),
        ColumnDef("ID", ColumnDef::TEXT, 150),
        ColumnDef("Notes", ColumnDef::TEXT, 220),
    };
    auto idx = [&](const wxString& name) {
      for (size_t i = 0; i < cfg.columns.size(); i++)
        if (cfg.columns[i].name == name) return static_cast<int>(i);
      return -1;
    };
    cfg.chartCol = idx("Map");
    cfg.chartColorKeyCol = idx("Event");
    cfg.timeCol = idx("Time");
    cfg.latCol = idx("Lat");
    cfg.lonCol = idx("Lon");
    cfg.autoIncrementCol = idx("EventNo");
    cfg.labelCol = idx("Event");
    cfg.columns[idx("Lat")].geoRole = ColumnDef::GeoRole::Latitude;
    cfg.columns[idx("Lon")].geoRole = ColumnDef::GeoRole::Longitude;
    // A not-yet-classified event shouldn't silently default to
    // whatever's first in the Event dropdown -- leave it genuinely
    // blank on a new row, the same reasoning Sightings already uses for
    // Species/etc.
    cfg.defaultChoicesToFirstOption = false;
    // The map label is configurable (see the Settings tab's Map
    // section, "Event label") -- defaults to Event + ID. Same pattern
    // as Sightings' labelTextFn: re-read fresh on every call, so
    // changing the setting takes effect immediately.
    cfg.labelTextFn = [this](GenericGridTable* t, int row) {
      wxArrayString cols =
          m_plugin->GetDisplaySettings()
              ? m_plugin->GetDisplaySettings()->EventsLabelColumns()
              : wxArrayString();
      wxString label;
      for (const auto& colName : cols) {
        int col = t->FindColByName(colName);
        if (col < 0) continue;
        wxString v = t->RawGet(row, col);
        if (v.IsEmpty()) continue;
        if (!label.IsEmpty()) label += " ";
        label += v;
      }
      return label;
    };
    m_tabs.push_back(std::make_unique<DataTab>(
        m_notebook, cfg, m_plugin->GetSurveyDataDir(), m_filePrefix));
    m_events = m_tabs.back().get();
  }

  // ---- Surfacings -----------------------------------------------
  // Disabled per direct request, pending more thought about how this
  // tab's data should actually relate to Sightings -- the "auto-create
  // a linked row, one-time copy only" design questioned itself in
  // practice (see the code below, kept intact rather than deleted, for
  // when this gets revisited). m_surfacing stays null; every other part
  // of this file that touches it already checks for that (drawing,
  // exporting, marker controls, etc.), so nothing else needs to change
  // to disable it here.
#if 0
  {
    DataTabConfig cfg;
    cfg.title = "Surfacings";
    cfg.csvFilename = "surfacings.csv";
    cfg.columns = {
        // SightNo first per direct request.
        ColumnDef("SightNo", ColumnDef::TEXT, 70),
        ColumnDef("Map", ColumnDef::BOOL, 50),
        ColumnDef("Time", ColumnDef::TEXT, 90),
        ColumnDef("Lat", ColumnDef::TEXT, 90),
        ColumnDef("Lon", ColumnDef::TEXT, 90),
        ColumnDef("BearingMag", ColumnDef::TEXT, 90),
        ColumnDef("Dist", ColumnDef::TEXT, 70),
        WithDefault(ColumnDef("DistUnit", ToArrayString(kDistUnitChoices,
                                             WXSIZEOF(kDistUnitChoices)),
                   90), "nm"),
        ColumnDef("SurfLat", ColumnDef::TEXT, 90),
        ColumnDef("SurfLon", ColumnDef::TEXT, 90),
        ColumnDef("Event", ToArrayString(kSurfacingEventChoices,
                                         WXSIZEOF(kSurfacingEventChoices)),
                   130),
    };
    auto idx = [&](const wxString &name) {
      for (size_t i = 0; i < cfg.columns.size(); i++)
        if (cfg.columns[i].name == name) return static_cast<int>(i);
      return -1;
    };
    cfg.chartCol = idx("Map");
    cfg.timeCol = idx("Time");
    cfg.latCol = idx("Lat");
    cfg.lonCol = idx("Lon");
    cfg.overlayLatCol = idx("SurfLat");
    cfg.overlayLonCol = idx("SurfLon");
    cfg.overlayTextCol = idx("SightNo");
    cfg.labelCol = idx("Event");
    cfg.columns[idx("Lat")].geoRole = ColumnDef::GeoRole::Latitude;
    cfg.columns[idx("Lon")].geoRole = ColumnDef::GeoRole::Longitude;
    cfg.columns[idx("SurfLat")].geoRole = ColumnDef::GeoRole::Latitude;
    cfg.columns[idx("SurfLon")].geoRole = ColumnDef::GeoRole::Longitude;
    cfg.recompute = [](DataTab *tab, GenericGridTable *t, int row,
                        int editedCol) {
      RecomputeBearingDistancePosition(tab, t, row, editedCol, "SurfLat",
                                        "SurfLon");
    };
    m_tabs.push_back(
        std::make_unique<DataTab>(m_notebook, cfg, m_plugin->GetSurveyDataDir(), m_filePrefix));
    m_surfacing = m_tabs.back().get();
  }

  // Every new Sightings row automatically creates a linked Surfacings
  // row, sharing Time, Bearing, BearUnit, Dist, DistUnit, and SightNo --
  // a one-time copy at creation time, not an ongoing sync (editing
  // Bearing/Dist afterward on one tab does not update the other).
  // Replaces an earlier version's manually-triggered "+ Surf" button
  // per direct request.
  if (m_sightings && m_surfacing) {
    m_sightings->on_row_added_external = [this](int row) {
      wxString sightno = m_sightings->GetCellValueByName(row, "SightNo");
      wxString time = m_sightings->GetCellValueByName(row, "Time");
      wxString bearing = m_sightings->GetCellValueByName(row, "BearingMag");
      wxString dist = m_sightings->GetCellValueByName(row, "Dist");
      wxString distUnit = m_sightings->GetCellValueByName(row, "DistUnit");

      m_surfacing->AddRow();
      int newRow = m_surfacing->RowCount() - 1;
      if (newRow < 0) return;
      m_surfacing->SetCellValueByName(newRow, "SightNo", sightno);
      m_surfacing->SetCellValueByName(newRow, "Time", time);
      m_surfacing->SetCellValueByName(newRow, "DistUnit", distUnit);
      m_surfacing->SetCellValueByName(newRow, "BearingMag", bearing);
      m_surfacing->SetCellValueByName(newRow, "Dist", dist);
    };
  }
#endif  // Surfacings tab, disabled

  // Any add/edit/delete that could change what's drawn on the chart
  // overlay should trigger a redraw so the change is reflected right
  // away, matching what changing a row's Color implies.
  int gridFontSize = m_plugin->GetDisplaySettings()
                         ? m_plugin->GetDisplaySettings()->GridFontSize()
                         : 11;
  for (auto& tab : m_tabs) {
    DataTab* t = tab.get();
    t->SetGridFontSize(gridFontSize);
    t->on_chart_changed = [this]() { m_plugin->RequestOverlayRedraw(); };
    wxString tabTitle = t->Title();
    // Resolves a charted row's marker color via its Species/Event to
    // the species/event's own configured color -- species.csv for
    // Sightings, event_types.csv for Events. Falls back to a plain
    // default color if the species/event isn't found in that file
    // (e.g. a name typed in that doesn't match any row there) or has
    // no color set.
    if (tabTitle == "Sightings") {
      t->chart_default_color_lookup = [this](const wxString& key) {
        wxString name = m_speciesConfig.GetField(key, "color");
        if (!name.IsEmpty()) return NamedColorToColour(name);
        return m_plugin->GetDisplaySettings()
                   ? m_plugin->GetDisplaySettings()->MarkerColor(
                         "Sightings", wxColour(230, 120, 20))
                   : wxColour(230, 120, 20);
      };
    } else if (tabTitle == "Events") {
      t->chart_default_color_lookup = [this](const wxString& key) {
        wxString name = m_eventsConfig.GetField(key, "color");
        if (!name.IsEmpty()) return NamedColorToColour(name);
        return m_plugin->GetDisplaySettings()
                   ? m_plugin->GetDisplaySettings()->MarkerColor(
                         "Events", wxColour(30, 100, 220))
                   : wxColour(30, 100, 220);
      };
    }
    t->on_cell_selected = [this, tabTitle](const wxString& columnName) {
      wxString def = m_columnDefinitions.GetDefinition(tabTitle, columnName);
      if (m_columnDefLabel) {
        m_columnDefRawText =
            def.IsEmpty() ? wxString(wxEmptyString) : columnName + ": " + def;
        m_columnDefLabel->SetLabel(m_columnDefRawText);
        int width = m_columnDefLabel->GetParent()->GetClientSize().GetWidth();
        if (width > 40) m_columnDefLabel->Wrap(width - 24);
      }
    };
  }
}
void LogWindow::NotifyVesselFix(double lat, double lon,
                                const wxDateTime& utc) {
  for (auto& tab : m_tabs) tab->SetVesselFix(lat, lon, utc);
}

namespace {
// Escapes text for safe use inside GPX/XML element content.
wxString XmlEscape(const wxString& in) {
  wxString out = in;
  out.Replace("&", "&amp;");
  out.Replace("<", "&lt;");
  out.Replace(">", "&gt;");
  out.Replace("\"", "&quot;");
  out.Replace("'", "&apos;");
  return out;
}

// Parses this plugin's "YYYY-MM-DD HH:MM:SS <tz>" local-time strings
// (ignoring the trailing timezone text, same as ExportMergedCsv's
// parseTimestamp) and converts to UTC for a GPX <time> element. Assumes
// the local timezone the data was recorded in matches the one this
// plugin is currently running in -- true for the common case (viewing/
// exporting a survey shortly after recording it), potentially off for
// much older data from a different timezone, which isn't otherwise
// recoverable from a bare "EST"-style abbreviation.
wxString ToGpxTime(const wxString& raw) {
  if (raw.length() < 19) return wxString();
  wxDateTime dt;
  wxString::const_iterator end;
  if (!dt.ParseFormat(raw.Left(19), "%Y-%m-%d %H:%M:%S", &end)) {
    return wxString();
  }
  return dt.ToUTC().Format("%Y-%m-%dT%H:%M:%SZ");
}
}  // namespace

namespace {
wxDateTime ParseLocalTimestamp(const wxString& raw) {
  wxDateTime dt;
  if (raw.length() >= 19) {
    wxString::const_iterator end;
    dt.ParseFormat(raw.Left(19), "%Y-%m-%d %H:%M:%S", &end);
  }
  return dt;
}
}  // namespace

SurveySummary LogWindow::ComputeSummary() const {
  SurveySummary s;

  // --- Track: total distance/time, contribution to the bounding box.
  if (m_plugin->GetTrackRecorder()) {
    auto rows = CsvUtils::ReadAll(m_plugin->GetTrackRecorder()->GetCsvPath());
    double prevLat = 0.0, prevLon = 0.0;
    bool havePrev = false;
    wxDateTime firstTime, lastTime;
    for (size_t r = 1; r < rows.size(); r++) {
      if (rows[r].size() < 3) continue;
      double lat = 0.0, lon = 0.0;
      if (!rows[r][1].ToDouble(&lat) || !rows[r][2].ToDouble(&lon)) continue;
      wxDateTime t = ParseLocalTimestamp(rows[r][0]);
      if (t.IsValid()) {
        if (!firstTime.IsValid()) firstTime = t;
        lastTime = t;
      }
      if (!s.haveBoundingBox) {
        s.minLat = s.maxLat = lat;
        s.minLon = s.maxLon = lon;
        s.haveBoundingBox = true;
      } else {
        s.minLat = wxMin(s.minLat, lat);
        s.maxLat = wxMax(s.maxLat, lat);
        s.minLon = wxMin(s.minLon, lon);
        s.maxLon = wxMax(s.maxLon, lon);
      }

      bool thisOnEffort = rows[r].size() > 5 && rows[r][5] == "ON";
      bool prevOnEffort =
          havePrev && rows[r - 1].size() > 5 && rows[r - 1][5] == "ON";
      if (havePrev) {
        double brg = 0.0, dist = 0.0;
        DistanceBearingMercator_Plugin(prevLat, prevLon, lat, lon, &brg, &dist);
        s.trackNm += dist;
        s.haveTrack = true;
        if (thisOnEffort && prevOnEffort) {
          s.effortNm += dist;
          s.haveEffort = true;
        }
      }
      prevLat = lat;
      prevLon = lon;
      havePrev = true;
    }
    if (firstTime.IsValid() && lastTime.IsValid()) {
      s.trackTime = lastTime - firstTime;
      s.trackStartTime = firstTime;
      s.trackEndTime = lastTime;
    }
  }

  // --- Effort: on-effort time (sum of gaps between consecutive Effort
  // rows where Effort was ON for that whole gap, plus -- if Effort is
  // still ON as of the most recent row -- the time from that row up to
  // right now, so this keeps increasing live rather than freezing at
  // the last logged row), visibility/Beaufort range, unique weather
  // conditions.
  if (m_environmental) {
    auto rows = CsvUtils::ReadAll(m_environmental->GetCsvPath());
    if (!rows.empty()) {
      auto colIdx = [&](const wxString& name) -> int {
        for (size_t c = 0; c < rows[0].size(); c++) {
          if (rows[0][c] == name) return static_cast<int>(c);
        }
        return -1;
      };
      int timeCol = colIdx("Time");
      int effortCol = colIdx("Effort");
      int visCol = colIdx("Vis");
      int beaufortCol = colIdx("Beaufort");
      int weatherCol = colIdx("Weather");

      wxDateTime prevTime;
      wxString prevEffort;
      for (size_t r = 1; r < rows.size(); r++) {
        const auto& row = rows[r];
        if (timeCol >= 0 && static_cast<size_t>(timeCol) < row.size()) {
          wxDateTime t = ParseLocalTimestamp(row[timeCol]);
          if (t.IsValid() && prevTime.IsValid() && prevEffort == "ON") {
            s.effortTime += (t - prevTime);
            s.haveEffort = true;
          }
          if (t.IsValid()) prevTime = t;
        }
        if (effortCol >= 0 && static_cast<size_t>(effortCol) < row.size()) {
          prevEffort = row[effortCol];
        }
        if (visCol >= 0 && static_cast<size_t>(visCol) < row.size() &&
            !row[visCol].IsEmpty()) {
          double v = 0.0;
          if (row[visCol].ToDouble(&v)) {
            if (!s.haveVis) {
              s.minVis = s.maxVis = v;
              s.haveVis = true;
            } else {
              s.minVis = wxMin(s.minVis, v);
              s.maxVis = wxMax(s.maxVis, v);
            }
          }
        }
        if (beaufortCol >= 0 && static_cast<size_t>(beaufortCol) < row.size() &&
            !row[beaufortCol].IsEmpty()) {
          double b = 0.0;
          if (row[beaufortCol].ToDouble(&b)) {
            if (!s.haveBeaufort) {
              s.minBeaufort = s.maxBeaufort = b;
              s.haveBeaufort = true;
            } else {
              s.minBeaufort = wxMin(s.minBeaufort, b);
              s.maxBeaufort = wxMax(s.maxBeaufort, b);
            }
          }
        }
        if (weatherCol >= 0 && static_cast<size_t>(weatherCol) < row.size() &&
            !row[weatherCol].IsEmpty() &&
            s.uniqueWeather.Index(row[weatherCol]) == wxNOT_FOUND) {
          s.uniqueWeather.Add(row[weatherCol]);
        }
      }

      // If Effort is still ON as of the most recent row, count the time
      // from that row up to right now too -- confirmed as a real,
      // reported bug without this: the loop above only sums gaps
      // *between* consecutive rows, so on-effort time appeared frozen
      // rather than steadily increasing while still actively on effort
      // (the gap after the last row was never closed by a "next" row to
      // measure against, since there isn't one yet).
      if (prevEffort == "ON" && prevTime.IsValid()) {
        wxDateTime now = wxDateTime::Now();
        if (now > prevTime) {
          s.effortTime += (now - prevTime);
          s.haveEffort = true;
        }
      }
    }
  }

  // --- Sightings: counts, species breakdown, bounding-box contribution.
  if (m_sightings) {
    auto rows = CsvUtils::ReadAll(m_sightings->GetCsvPath());
    if (!rows.empty()) {
      auto colIdx = [&](const wxString& name) -> int {
        for (size_t c = 0; c < rows[0].size(); c++) {
          if (rows[0][c] == name) return static_cast<int>(c);
        }
        return -1;
      };
      int latCol = colIdx("SigLat");
      int lonCol = colIdx("SigLon");
      int speciesCol = colIdx("Species");
      int numCol = colIdx("Num");
      int numCalfCol = colIdx("NumCalf");

      wxArrayString uniqueSpecies;
      std::vector<SurveySummary::SpeciesRow> breakdown;
      auto findOrAddSpecies =
          [&](const wxString& species) -> SurveySummary::SpeciesRow& {
        for (auto& sp : breakdown) {
          if (sp.species == species) return sp;
        }
        SurveySummary::SpeciesRow sp;
        sp.species = species;
        breakdown.push_back(sp);
        return breakdown.back();
      };

      for (size_t r = 1; r < rows.size(); r++) {
        const auto& row = rows[r];
        s.numSightings++;

        if (latCol >= 0 && lonCol >= 0 &&
            static_cast<size_t>(latCol) < row.size() &&
            static_cast<size_t>(lonCol) < row.size()) {
          double lat = 0.0, lon = 0.0;
          if (row[latCol].ToDouble(&lat) && row[lonCol].ToDouble(&lon)) {
            if (!s.haveBoundingBox) {
              s.minLat = s.maxLat = lat;
              s.minLon = s.maxLon = lon;
              s.haveBoundingBox = true;
            } else {
              s.minLat = wxMin(s.minLat, lat);
              s.maxLat = wxMax(s.maxLat, lat);
              s.minLon = wxMin(s.minLon, lon);
              s.maxLon = wxMax(s.maxLon, lon);
            }
          }
        }

        wxString species =
            speciesCol >= 0 && static_cast<size_t>(speciesCol) < row.size()
                ? row[speciesCol]
                : wxString();
        if (!species.IsEmpty() && uniqueSpecies.Index(species) == wxNOT_FOUND) {
          uniqueSpecies.Add(species);
        }

        // Every sighting with a species counts toward the breakdown --
        // no longer restricted to Probable/Definite SpecConf/NumConf
        // (an earlier version filtered on those; removed per direct
        // request).
        if (!species.IsEmpty()) {
          SurveySummary::SpeciesRow& sp = findOrAddSpecies(species);
          sp.sightings++;
          bool haveNum = numCol >= 0 &&
                         static_cast<size_t>(numCol) < row.size() &&
                         !row[numCol].IsEmpty();
          if (haveNum) {
            long n = 0;
            if (row[numCol].ToLong(&n)) {
              sp.individuals += static_cast<int>(n);
              sp.knownIndividualsCount++;
            } else {
              sp.hasUnknownIndividuals = true;  // present but not a
                                                // valid number
            }
          } else {
            // Blank Num is genuinely unknown, not zero -- doesn't add
            // anything to the sum, and flags the total as a floor
            // rather than a confirmed count.
            sp.hasUnknownIndividuals = true;
          }
          bool haveCalf = numCalfCol >= 0 &&
                          static_cast<size_t>(numCalfCol) < row.size() &&
                          !row[numCalfCol].IsEmpty();
          if (haveCalf) {
            long n = 0;
            if (row[numCalfCol].ToLong(&n)) {
              sp.calves += static_cast<int>(n);
              sp.knownCalvesCount++;
            } else {
              sp.hasUnknownCalves = true;
            }
          } else {
            sp.hasUnknownCalves = true;
          }
        }
      }
      s.numSpeciesSighted = static_cast<int>(uniqueSpecies.size());
      s.speciesBreakdown = std::move(breakdown);
    }
  }

  // --- Events: bounding-box contribution, plus a tally of how many
  // times each Event type has occurred.
  if (m_events) {
    auto rows = CsvUtils::ReadAll(m_events->GetCsvPath());
    if (!rows.empty()) {
      int latCol = -1, lonCol = -1, eventCol = -1;
      for (size_t c = 0; c < rows[0].size(); c++) {
        if (rows[0][c] == "Lat") latCol = static_cast<int>(c);
        if (rows[0][c] == "Lon") lonCol = static_cast<int>(c);
        if (rows[0][c] == "Event") eventCol = static_cast<int>(c);
      }
      for (size_t r = 1; r < rows.size(); r++) {
        if (eventCol >= 0 && static_cast<size_t>(eventCol) < rows[r].size() &&
            !rows[r][eventCol].IsEmpty()) {
          const wxString& eventType = rows[r][eventCol];
          bool found = false;
          for (auto& ec : s.eventBreakdown) {
            if (ec.eventType == eventType) {
              ec.count++;
              found = true;
              break;
            }
          }
          if (!found) {
            SurveySummary::EventCount ec;
            ec.eventType = eventType;
            ec.count = 1;
            s.eventBreakdown.push_back(ec);
          }
        }

        if (latCol < 0 || lonCol < 0 ||
            static_cast<size_t>(latCol) >= rows[r].size() ||
            static_cast<size_t>(lonCol) >= rows[r].size()) {
          continue;
        }
        double lat = 0.0, lon = 0.0;
        if (!rows[r][latCol].ToDouble(&lat) ||
            !rows[r][lonCol].ToDouble(&lon)) {
          continue;
        }
        if (!s.haveBoundingBox) {
          s.minLat = s.maxLat = lat;
          s.minLon = s.maxLon = lon;
          s.haveBoundingBox = true;
        } else {
          s.minLat = wxMin(s.minLat, lat);
          s.maxLat = wxMax(s.maxLat, lat);
          s.minLon = wxMin(s.minLon, lon);
          s.maxLon = wxMax(s.maxLon, lon);
        }
      }
    }
  }

  return s;
}

void LogWindow::ExportSummaryCsv(const wxString& destDir) const {
  SurveySummary s = ComputeSummary();
  std::vector<wxString> header = {"Metric", "Value"};
  std::vector<std::vector<wxString>> rows;
  auto add = [&](const wxString& metric, const wxString& value) {
    rows.push_back({metric, value});
  };

  add("Survey start", s.trackStartTime.IsValid()
                          ? s.trackStartTime.Format("%Y-%m-%d %H:%M:%S")
                          : wxString());
  add("Survey end", s.trackEndTime.IsValid()
                        ? s.trackEndTime.Format("%Y-%m-%d %H:%M:%S")
                        : wxString());
  add("Trackline (nm)",
      s.haveTrack ? wxString::FromDouble(s.trackNm, 2) : wxString());
  add("Trackline time",
      s.haveTrack ? s.trackTime.Format("%H:%M:%S") : wxString());
  add("On-effort (nm)",
      s.haveEffort ? wxString::FromDouble(s.effortNm, 2) : wxString());
  add("On-effort time",
      s.haveEffort ? s.effortTime.Format("%H:%M:%S") : wxString());
  add("Total sightings", wxString::Format("%d", s.numSightings));
  add("Species sighted", wxString::Format("%d", s.numSpeciesSighted));
  add("Bounding box min lat",
      s.haveBoundingBox ? wxString::FromDouble(s.minLat, 6) : wxString());
  add("Bounding box max lat",
      s.haveBoundingBox ? wxString::FromDouble(s.maxLat, 6) : wxString());
  add("Bounding box min lon",
      s.haveBoundingBox ? wxString::FromDouble(s.minLon, 6) : wxString());
  add("Bounding box max lon",
      s.haveBoundingBox ? wxString::FromDouble(s.maxLon, 6) : wxString());
  add("Visibility min",
      s.haveVis ? wxString::FromDouble(s.minVis, 1) : wxString());
  add("Visibility max",
      s.haveVis ? wxString::FromDouble(s.maxVis, 1) : wxString());
  add("Beaufort min",
      s.haveBeaufort ? wxString::FromDouble(s.minBeaufort, 0) : wxString());
  add("Beaufort max",
      s.haveBeaufort ? wxString::FromDouble(s.maxBeaufort, 0) : wxString());
  wxString weatherJoined = wxJoin(s.uniqueWeather, ';');
  add("Weather conditions encountered", weatherJoined);

  rows.push_back({"", ""});
  rows.push_back({"Sightings Breakdown", "Sightings,Individuals,Calves"});
  for (const auto& sp : s.speciesBreakdown) {
    wxString individualsStr = FormatKnownCountForCsv(
        sp.individuals, sp.knownIndividualsCount, sp.hasUnknownIndividuals);
    wxString calvesStr = FormatKnownCountForCsv(sp.calves, sp.knownCalvesCount,
                                                sp.hasUnknownCalves);
    wxString valueStr;
    valueStr << sp.sightings << "," << individualsStr << "," << calvesStr;
    add(sp.species, valueStr);
  }

  rows.push_back({"", ""});
  rows.push_back({"Events Breakdown", "Count"});
  for (const auto& ec : s.eventBreakdown) {
    add(ec.eventType, wxString::Format("%d", ec.count));
  }

  wxString filename = m_filePrefix.IsEmpty() ? wxString("summary.csv")
                                             : m_filePrefix + "_summary.csv";
  wxFileName outFn(destDir, filename);
  CsvUtils::WriteAll(outFn.GetFullPath(), header, rows);
}

void LogWindow::ExportGpxLayer(const wxString& destDir) {
  wxString content;
  content << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  content << "<gpx version=\"1.1\" creator=\"Spotter (spotter_pi)\" "
             "xmlns=\"http://www.topografix.com/GPX/1/1\">\n";

  // Trackline, as one <trk>/<trkseg>.
  if (m_plugin->GetTrackRecorder()) {
    const auto& points = m_plugin->GetTrackRecorder()->GetPoints();
    if (!points.empty()) {
      content << "  <trk>\n    <name>"
              << XmlEscape(m_surveyName.IsEmpty() ? wxString("Track")
                                                  : m_surveyName)
              << "</name>\n    <trkseg>\n";
      for (const auto& p : points) {
        content << "      <trkpt lat=\"" << wxString::FromDouble(p.lat, 6)
                << "\" lon=\"" << wxString::FromDouble(p.lon, 6) << "\"/>\n";
      }
      content << "    </trkseg>\n  </trk>\n";
    }
  }

  // Sightings, as <wpt> waypoints -- reads straight from the saved CSV
  // (rather than the live grid) so this reflects exactly what's on
  // disk, the same as the other exports.
  if (m_sightings) {
    auto rows = CsvUtils::ReadAll(m_sightings->GetCsvPath());
    if (rows.size() > 1) {
      const auto& header = rows[0];
      auto colIdx = [&](const wxString& name) -> int {
        for (size_t i = 0; i < header.size(); i++) {
          if (header[i] == name) return static_cast<int>(i);
        }
        return -1;
      };
      int latCol = colIdx("SigLat");
      int lonCol = colIdx("SigLon");
      int sightNoCol = colIdx("SightNo");
      int speciesCol = colIdx("Species");
      int fieldIdCol = colIdx("FieldID");
      int timeCol = colIdx("Time");
      int notesCol = colIdx("Notes");

      for (size_t r = 1; r < rows.size(); r++) {
        const auto& row = rows[r];
        if (latCol < 0 || lonCol < 0 ||
            static_cast<size_t>(latCol) >= row.size() ||
            static_cast<size_t>(lonCol) >= row.size()) {
          continue;
        }
        double lat = 0.0, lon = 0.0;
        if (!row[latCol].ToDouble(&lat) || !row[lonCol].ToDouble(&lon))
          continue;

        wxString name;
        if (speciesCol >= 0 && static_cast<size_t>(speciesCol) < row.size() &&
            !row[speciesCol].IsEmpty()) {
          name = row[speciesCol];
        }
        if (fieldIdCol >= 0 && static_cast<size_t>(fieldIdCol) < row.size() &&
            !row[fieldIdCol].IsEmpty()) {
          name += (name.IsEmpty() ? "" : " ") + row[fieldIdCol];
        }
        if (name.IsEmpty() && sightNoCol >= 0 &&
            static_cast<size_t>(sightNoCol) < row.size()) {
          name = "Sighting " + row[sightNoCol];
        }

        content << "  <wpt lat=\"" << wxString::FromDouble(lat, 6)
                << "\" lon=\"" << wxString::FromDouble(lon, 6) << "\">\n";
        if (!name.IsEmpty()) {
          content << "    <name>" << XmlEscape(name) << "</name>\n";
        }
        if (notesCol >= 0 && static_cast<size_t>(notesCol) < row.size() &&
            !row[notesCol].IsEmpty()) {
          content << "    <desc>" << XmlEscape(row[notesCol]) << "</desc>\n";
        }
        if (timeCol >= 0 && static_cast<size_t>(timeCol) < row.size()) {
          wxString gpxTime = ToGpxTime(row[timeCol]);
          if (!gpxTime.IsEmpty()) {
            content << "    <time>" << gpxTime << "</time>\n";
          }
        }
        content << "  </wpt>\n";
      }
    }
  }

  content << "</gpx>\n";

  wxString gpxFilename = m_filePrefix.IsEmpty() ? wxString("layer.gpx")
                                                : m_filePrefix + "_layer.gpx";
  wxFileName outFn(destDir, gpxFilename);
  wxFile f;
  if (f.Create(outFn.GetFullPath(), true /* overwrite */)) {
    f.Write(content);
  }
}

void LogWindow::ExportMergedCsv(const wxString& destDir) {
  // One row per Sightings row, per Effort row, and per recorded track
  // point, all interleaved chronologically by timestamp -- track points
  // vastly outnumber the others (typically one every several seconds
  // the whole survey), so most rows in the result will have only the
  // Track_* columns filled in and everything else blank. Column names
  // are prefixed by source (Sightings_/Effort_/Track_) since the
  // sources have overlapping raw names (e.g. both Sightings and Effort
  // have their own Time/Lat/Lon).

  // Parses just the "YYYY-MM-DD HH:MM:SS" prefix of a timestamp string
  // and ignores whatever trailing timezone text follows (a locale-
  // dependent abbreviation like "EST", not a fixed-width or reliably
  // parseable field) -- fine for sorting purposes since every timestamp
  // in this plugin's own files is in the same local timezone anyway.
  auto parseTimestamp = [](const wxString& raw) -> wxDateTime {
    wxDateTime dt;
    if (raw.length() >= 19) {
      wxString::const_iterator end;
      if (dt.ParseFormat(raw.Left(19), "%Y-%m-%d %H:%M:%S", &end)) {
        return dt;
      }
    }
    return wxDateTime(static_cast<time_t>(1));  // unparseable -- sorts to
                                                // the very front,
                                                // visibly, rather than
                                                // silently vanishing
  };

  struct SourceRow {
    wxString timestampSort;  // "YYYY-MM-DD HH:MM:SS", for sorting
    wxString sourceName;
    std::vector<wxString> rawRow;
  };
  struct SourceInfo {
    wxString name;
    std::vector<wxString> fileHeader;
    size_t headerStart;  // offset into the final combined header
  };

  std::vector<wxString> combinedHeader = {"Timestamp", "Source"};
  std::vector<SourceInfo> sources;
  std::vector<SourceRow> sourceRows;

  auto addSource = [&](const wxString& sourceName, const wxString& csvPath,
                       const wxString& timeColName) {
    if (csvPath.IsEmpty()) return;
    auto rows = CsvUtils::ReadAll(csvPath);
    if (rows.size() < 2) return;  // header only, or unreadable
    const auto& fileHeader = rows[0];
    int timeColIdx = -1;
    for (size_t c = 0; c < fileHeader.size(); c++) {
      if (fileHeader[c] == timeColName) {
        timeColIdx = static_cast<int>(c);
        break;
      }
    }
    if (timeColIdx < 0) return;

    SourceInfo info;
    info.name = sourceName;
    info.fileHeader = fileHeader;
    info.headerStart = combinedHeader.size();
    for (const auto& col : fileHeader) {
      combinedHeader.push_back(sourceName + "_" + col);
    }
    sources.push_back(info);

    for (size_t r = 1; r < rows.size(); r++) {
      if (static_cast<size_t>(timeColIdx) >= rows[r].size()) continue;
      SourceRow sr;
      sr.timestampSort =
          parseTimestamp(rows[r][timeColIdx]).Format("%Y-%m-%d %H:%M:%S");
      sr.sourceName = sourceName;
      sr.rawRow = rows[r];
      sourceRows.push_back(std::move(sr));
    }
  };

  addSource("Sightings", m_sightings ? m_sightings->GetCsvPath() : wxString(),
            "Time");
  addSource("Effort",
            m_environmental ? m_environmental->GetCsvPath() : wxString(),
            "Time");
  // Reads the already-*exported* track file (in destDir, not the raw
  // source file) since it includes the nm_since_prev column computed
  // by TrackRecorder::ExportCopyTo() -- which OnExportClicked() always
  // calls before this, so it's already there by the time this runs.
  {
    wxString trackFilename = m_filePrefix.IsEmpty()
                                 ? wxString("track.csv")
                                 : m_filePrefix + "_track.csv";
    wxFileName exportedTrackFn(destDir, trackFilename);
    addSource("Track", exportedTrackFn.GetFullPath(), "local_time");
  }

  // Chronological order, before building final rows -- string-sortable
  // since every timestamp is normalized to "YYYY-MM-DD HH:MM:SS".
  std::sort(sourceRows.begin(), sourceRows.end(),
            [](const SourceRow& a, const SourceRow& b) {
              return a.timestampSort < b.timestampSort;
            });

  std::vector<std::vector<wxString>> outRows;
  outRows.reserve(sourceRows.size());
  for (const auto& sr : sourceRows) {
    std::vector<wxString> full(combinedHeader.size(), wxString());
    full[0] = sr.timestampSort;
    full[1] = sr.sourceName;
    for (const auto& info : sources) {
      if (info.name != sr.sourceName) continue;
      for (size_t c = 0;
           c < sr.rawRow.size() && info.headerStart + c < full.size(); c++) {
        full[info.headerStart + c] = sr.rawRow[c];
      }
      break;
    }
    outRows.push_back(std::move(full));
  }

  wxString mergedFilename = m_filePrefix.IsEmpty()
                                ? wxString("merged.csv")
                                : m_filePrefix + "_merged.csv";
  wxFileName outFn(destDir, mergedFilename);
  CsvUtils::WriteAll(outFn.GetFullPath(), combinedHeader, outRows);
}

void LogWindow::OnExportClicked(wxCommandEvent&) {
  wxString exportName =
      m_filePrefix.IsEmpty() ? wxString("spotter_export") : m_filePrefix;

  // A folder-choice dialog -- the <prefix> folder itself gets created
  // as a new subfolder of whatever's chosen here, rather than asking
  // the user to also type/confirm that subfolder's own name. (An
  // earlier version of this also offered a zip file as an alternative;
  // removed per direct request in favor of always just producing this
  // plain folder.)
  wxDirDialog dirDlg(
      this, "Choose where to create the \"" + exportName + "\" export folder",
      wxStandardPaths::Get().GetDocumentsDir());
  if (dirDlg.ShowModal() != wxID_OK) return;
  wxString destParentDir = dirDlg.GetPath();

  // Every existing per-file export function (ExportCopyTo,
  // ExportMergedCsv, etc) already knows how to write its output given a
  // destination directory -- reused completely unchanged here, just
  // pointed at a temporary staging directory instead of the final
  // <prefix> folder directly, so a partially-failed export doesn't
  // leave a half-populated folder behind at the real destination.
  wxString stagingDir = wxFileName::CreateTempFileName("spotter_export_");
  wxRemoveFile(stagingDir);  // CreateTempFileName() creates an empty
                             // *file*; removed immediately so the same
                             // path can be (re)created as a directory
                             // instead, just below.
  if (!wxFileName::Mkdir(stagingDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    wxMessageBox(
        "Couldn't create a temporary folder to stage the "
        "export in -- nothing was saved.",
        "Spotter", wxOK | wxICON_ERROR, this);
    return;
  }

  for (auto& tab : m_tabs) tab->ExportCopyTo(stagingDir);
  if (m_plugin->GetTrackRecorder()) {
    m_plugin->GetTrackRecorder()->ExportCopyTo(stagingDir);
  }
  ExportMergedCsv(stagingDir);
  ExportGpxLayer(stagingDir);
  ExportSummaryCsv(stagingDir);

  // Creates <destParentDir>/<exportName>/ and copies every staged file
  // into it directly.
  wxFileName destFolderFn(destParentDir, "");
  destFolderFn.AppendDir(exportName);
  wxString destFolder = destFolderFn.GetPath();
  bool ok = false;
  int fileCount = 0;
  if (wxFileName::Mkdir(destFolder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    wxDir dir(stagingDir);
    wxString filename;
    bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
    ok = true;
    while (cont) {
      wxString srcPath = wxFileName(stagingDir, filename).GetFullPath();
      wxString dstPath = wxFileName(destFolder, filename).GetFullPath();
      if (wxCopyFile(srcPath, dstPath)) {
        fileCount++;
      } else {
        ok = false;
      }
      cont = dir.GetNext(&filename);
    }
  }

  // The staging directory was only ever a scratch area -- removed
  // either way, successful export or not, so nothing's left behind in
  // the system temp folder.
  if (wxDir::Exists(stagingDir)) {
    wxFileName::Rmdir(stagingDir, wxPATH_RMDIR_RECURSIVE);
  }

  if (!ok || fileCount == 0) {
    wxMessageBox("Couldn't create the export at:\n" + destFolder, "Spotter",
                 wxOK | wxICON_ERROR, this);
    return;
  }

  wxMessageBox("Saved a \"" + exportName + "\" folder to:\n" + destFolder +
                   "\n\nContains Sightings, Effort (environmental conditions + "
                   "effort status), Events, the trackline (track.csv, "
                   "including which portions were on-effort), the merged CSV, "
                   "GPX layer, and summary CSV.",
               "Spotter", wxOK | wxICON_INFORMATION, this);
}

wxString LogWindow::StartNewSurvey(const wxString& surveyName) {
  wxString prefix = SanitizeForFilename(surveyName);

  for (auto& tab : m_tabs) tab->StartNewFile(prefix);
  // The trackline also starts fresh -- otherwise the overlay would draw
  // a straight line connecting wherever the vessel was at the end of the
  // previous survey to wherever it is now, which is meaningless/
  // misleading, not a real transit track.
  if (m_plugin->GetTrackRecorder()) {
    m_plugin->GetTrackRecorder()->StartNewFile(prefix);
    m_plugin->GetTrackRecorder()->SetEnabled(false);
  }
  // Tracking and Effort both start OFF on a new survey, per direct
  // request -- the user turns each on deliberately (Settings tab for
  // Tracking; the Effort tab itself, or Cmd+Shift+F, for Effort) once
  // actually underway, rather than this plugin assuming either is
  // already true the moment a survey is named. An earlier version
  // turned Tracking on automatically here; that's removed. Keeps the
  // Settings tab's own tracking checkbox in sync with this, the same as
  // Load Survey already does when it turns tracking off.
  if (m_plugin->GetTrackingSettings()) {
    m_plugin->GetTrackingSettings()->SetEnabled(false);
  }
  if (m_trackingEnabledCheck) m_trackingEnabledCheck->SetValue(false);

  m_filePrefix = prefix;
  m_surveyName = surveyName;
  SaveCurrentSurveyInfo(surveyName, prefix);
  m_plugin->RequestOverlayRedraw();
  return prefix;
}

void LogWindow::OnStartNewSurveyClicked(wxCommandEvent&) {
  wxDialog dlg(this, wxID_ANY, "Start New Survey");
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxFlexGridSizer* grid = new wxFlexGridSizer(2, 8, 8);
  grid->Add(new wxStaticText(&dlg, wxID_ANY, "Survey Name:"), 0,
            wxALIGN_CENTER_VERTICAL);
  wxTextCtrl* surveyCtrl =
      new wxTextCtrl(&dlg, wxID_ANY, "", wxDefaultPosition, wxSize(220, -1));
  grid->Add(surveyCtrl, 1, wxEXPAND);
  sizer->Add(grid, 0, wxALL | wxEXPAND, 12);

  sizer->Add(new wxStaticText(&dlg, wxID_ANY,
                              "New data will be saved to files named for "
                              "this survey name. The chart trackline "
                              "will also start a fresh line rather than "
                              "connecting to the previous survey's."),
             0, wxALL, 12);

  wxStdDialogButtonSizer* btnSizer = new wxStdDialogButtonSizer();
  btnSizer->AddButton(new wxButton(&dlg, wxID_OK, "Continue"));
  btnSizer->AddButton(new wxButton(&dlg, wxID_CANCEL, "Cancel"));
  btnSizer->Realize();
  sizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 8);

  dlg.SetSizerAndFit(sizer);
  dlg.CentreOnParent();
  if (dlg.ShowModal() != wxID_OK) return;

  wxString surveyName = surveyCtrl->GetValue();

  int confirm = wxMessageBox(
      "This will clear the current Sightings, Effort and Events tables "
      "(data already saved remains safely stored under its current file "
      "names -- nothing on disk is deleted) and begin saving new "
      "entries to files named for this survey. The chart trackline will "
      "also start fresh.\n\nContinue?",
      "Start New Survey", wxYES_NO | wxICON_WARNING, this);
  if (confirm != wxYES) return;

  wxString prefix = StartNewSurvey(surveyName);

  wxMessageBox(
      "Started a new survey. New data will be saved with the "
      "prefix:\n" +
          prefix,
      "Spotter", wxOK | wxICON_INFORMATION, this);
}

void LogWindow::OnClearSurveyDataClicked(wxCommandEvent&) {
  // Genuinely destructive -- unlike everything else in this plugin
  // (Start New Survey, deleting rows, even overwriting a schema-
  // mismatched CSV on load), which always preserves the previous data
  // under its own file name rather than erasing it. Worded and
  // confirmed accordingly, including naming the current survey (if any)
  // so it's unambiguous which data is about to be erased.
  wxString surveyDesc = m_surveyName.IsEmpty()
                            ? wxString("the current (unnamed) survey")
                            : "\"" + m_surveyName + "\"";
  int confirm = wxMessageBox(
      "This will permanently erase all Sightings, Effort, Events, "
      "Surfacing, and trackline data for " +
          surveyDesc +
          " -- the CSV files themselves are cleared, not just the on-"
          "screen tables. This is different from every other action in "
          "this plugin: it cannot be undone, and there is no backup "
          "copy of what's erased.\n\nIf you just want to start a "
          "*new* survey while keeping this one's data intact, use "
          "\"Start New Survey...\" instead.\n\nErase all data for " +
          surveyDesc + "?",
      "Spotter -- Clear Survey Data", wxYES_NO | wxICON_ERROR | wxNO_DEFAULT,
      this);
  if (confirm != wxYES) return;

  // A second confirmation specifically because this is the one
  // genuinely irreversible action in the whole plugin -- everything
  // else here goes out of its way to avoid ever actually deleting data.
  int confirm2 = wxMessageBox(
      "Really erase all data for " + surveyDesc + "? This is permanent.",
      "Spotter -- Confirm Erase", wxYES_NO | wxICON_ERROR | wxNO_DEFAULT, this);
  if (confirm2 != wxYES) return;

  for (auto& tab : m_tabs) tab->ClearAllData();
  if (m_plugin->GetTrackRecorder()) {
    m_plugin->GetTrackRecorder()->ClearAllData();
  }
  m_plugin->RequestOverlayRedraw();

  wxMessageBox("All data for " + surveyDesc + " has been erased.", "Spotter",
               wxOK | wxICON_INFORMATION, this);
}

wxArrayString LogWindow::FindSurveyPrefixesInDir(const wxString& dir) const {
  wxArrayString prefixes;
  if (!wxDir::Exists(dir)) return prefixes;
  wxDir d(dir);
  if (!d.IsOpened()) return prefixes;
  // "_sightings.csv" specifically -- every survey this plugin creates
  // always has a Sightings file (even if empty, just a header), so this
  // alone reliably finds every survey without needing to also check
  // for effort/events/track files.
  wxString filename;
  bool cont = d.GetFirst(&filename, "*_sightings.csv", wxDIR_FILES);
  const wxString kSuffix = "_sightings.csv";
  while (cont) {
    if (filename.length() > kSuffix.length()) {
      wxString prefix = filename.Left(filename.length() - kSuffix.length());
      if (prefixes.Index(prefix) == wxNOT_FOUND) prefixes.Add(prefix);
    }
    cont = d.GetNext(&filename);
  }
  prefixes.Sort();
  return prefixes;
}

void LogWindow::OnLoadSurveyClicked(wxCommandEvent&) {
  const wxString kBrowseOption = "Load from a different folder...";

  wxArrayString choices = FindSurveyPrefixesInDir(m_plugin->GetSurveyDataDir());
  choices.Add(kBrowseOption);

  wxSingleChoiceDialog pickDlg(
      this, "Choose a survey to load (by its date_survey_vessel prefix):",
      "Load Survey", choices);
  if (pickDlg.ShowModal() != wxID_OK) return;
  wxString chosen = pickDlg.GetStringSelection();

  wxString prefixToLoad;
  if (chosen == kBrowseOption) {
    wxDirDialog dirDlg(this, "Choose a folder containing survey data files",
                       wxStandardPaths::Get().GetDocumentsDir());
    if (dirDlg.ShowModal() != wxID_OK) return;
    wxString externalDir = dirDlg.GetPath();

    wxArrayString externalPrefixes = FindSurveyPrefixesInDir(externalDir);
    if (externalPrefixes.IsEmpty()) {
      wxMessageBox(
          "No survey data files were found in that folder (looking for "
          "files named like \"<date>_<survey>_<vessel>_sightings.csv\").",
          "Spotter", wxOK | wxICON_WARNING, this);
      return;
    }

    wxString externalPrefix;
    if (externalPrefixes.size() == 1) {
      externalPrefix = externalPrefixes[0];
    } else {
      wxSingleChoiceDialog prefixDlg(
          this, "Multiple surveys were found in that folder -- choose one:",
          "Load Survey", externalPrefixes);
      if (prefixDlg.ShowModal() != wxID_OK) return;
      externalPrefix = prefixDlg.GetStringSelection();
    }

    // Copies every file for this prefix into this plugin's own data
    // directory, per direct request -- once loaded, this survey is
    // modified the same way as any other, in this plugin's own,
    // regular data folder, not the external one it was found in.
    wxDir extDir(externalDir);
    wxString filename;
    bool cont = extDir.GetFirst(&filename, externalPrefix + "_*", wxDIR_FILES);
    int copiedCount = 0;
    while (cont) {
      wxString src = wxFileName(externalDir, filename).GetFullPath();
      wxString dest =
          wxFileName(m_plugin->GetSurveyDataDir(), filename).GetFullPath();
      if (wxCopyFile(src, dest, true /* overwrite */)) copiedCount++;
      cont = extDir.GetNext(&filename);
    }
    if (copiedCount == 0) {
      wxMessageBox("Couldn't copy any files for \"" + externalPrefix +
                       "\" from that folder -- nothing was loaded.",
                   "Spotter", wxOK | wxICON_ERROR, this);
      return;
    }
    prefixToLoad = externalPrefix;
  } else {
    prefixToLoad = chosen;
  }

  // Turned off, per direct request, so new GPS fixes don't silently
  // start appending to what's very likely a historical/past survey --
  // the user has to deliberately turn it back on (Settings tab) to
  // resume recording, rather than this plugin guessing whether that's
  // wanted.
  if (m_plugin->GetTrackRecorder())
    m_plugin->GetTrackRecorder()->SetEnabled(false);
  if (m_plugin->GetTrackingSettings())
    m_plugin->GetTrackingSettings()->SetEnabled(false);
  // Keeps the Settings tab's own checkbox in sync with the state change
  // above -- confirmed as a real, reported bug without this: tracking
  // was correctly turned off, but the checkbox stayed visually checked
  // until the Settings tab was rebuilt some other way.
  if (m_trackingEnabledCheck) m_trackingEnabledCheck->SetValue(false);

  ApplyLoadedSurveyPrefix(prefixToLoad);

  wxMessageBox(
      "Loaded \"" + prefixToLoad +
          "\".\n\nTracking has been turned OFF, so new GPS fixes won't "
          "be silently added to this survey's track. Turn it back on "
          "from the Settings tab if you want to resume recording.",
      "Spotter", wxOK | wxICON_INFORMATION, this);
}

void LogWindow::ApplyLoadedSurveyPrefix(const wxString& prefixToLoad) {
  for (auto& tab : m_tabs) tab->LoadSurvey(prefixToLoad);
  if (m_plugin->GetTrackRecorder()) {
    m_plugin->GetTrackRecorder()->StartNewFile(prefixToLoad);
  }

  m_filePrefix = prefixToLoad;
  // The prefix *is* the survey name here, unchanged -- no date-stripping
  // (this used to call the now-removed ParseSurveyNameFromPrefix()).
  // That function existed only to recover a survey's real name from an
  // older version of this plugin that automatically prepended today's
  // date to every new survey's prefix; StartNewSurvey() hasn't done
  // that in a long time, so the prefix is always already exactly what
  // the user typed (sanitized for use as a filename). Confirmed as a
  // real, reported bug: loading a survey whose name genuinely started
  // with something date-shaped (e.g. "2026-07-23_test3", typed by the
  // user, not auto-added) had that portion silently stripped off,
  // making the survey name shown afterward inconsistent with the
  // prefix used for its actual files.
  m_surveyName = prefixToLoad;
  OnStatusTick();

  m_plugin->RequestOverlayRedraw();
}

void LogWindow::OnClose(wxCloseEvent& evt) {
  wxUnusedVar(evt);
  if (on_closed) on_closed();
  // Explicitly Destroy() rather than relying on default close handling,
  // which isn't guaranteed once we've bound our own handler for this
  // event. All of this plugin's real data (CSVs) lives independently of
  // this window, so there's nothing to lose --
  // SpotterPlugin::EnsureLogWindow() transparently recreates a
  // fresh one (reloading the CSVs, a fast operation) the next time the
  // toolbar button is clicked.
  Destroy();
}
