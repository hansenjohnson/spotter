#ifndef WHALE_DATA_TAB_H
#define WHALE_DATA_TAB_H

#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/datetime.h>
#include <functional>
#include <vector>
#include <tuple>
#include <limits>

#include "GenericGridTable.h"

class DataTab;

// Static definition of one tab: its columns and the handful of column
// indices the generic add-row logic needs to know about. `columns` must
// list any HIDDEN columns last (see GenericGridTable).
struct DataTabConfig {
  wxString title;        // Tab label, e.g. "Sightings"
  wxString csvFilename;  // e.g. "sightings.csv" -- the *suffix* used
                         // when building the actual file name, which
                         // is prefixed with the current survey's
                         // date_survey_vessel identifier (see
                         // DataTab::StartNewFile / LogWindow's
                         // "Start New Survey").
  std::vector<ColumnDef> columns;

  int chartCol = -1;  // BOOL "Map" column: show/hide this row on the
                      // chart overlay. An intermediate version of this
                      // plugin replaced this with a CHOICE "Color"
                      // column (per-row color override, including a
                      // "None" to hide); reverted back to this
                      // simpler BOOL column per direct request, with
                      // color now always resolved via
                      // chart_default_color_lookup instead (below).
  // Which column's value is the lookup key for chart_default_color_lookup
  // -- e.g. Species for Sightings, Event for Events. Ignored if
  // chartCol < 0.
  int chartColorKeyCol = -1;
  int timeCol = -1;  // Time column, auto-filled on Add Row with the
                     // full local timestamp
  int latCol = -1;   // decimal-degrees latitude column
  int lonCol = -1;   // decimal-degrees longitude column

  // Where to actually draw the chart overlay marker, if different from
  // latCol/lonCol. Sightings uses this: latCol/lonCol are the *vessel's*
  // position (used to derive the sighting position from bearing+distance),
  // but the marker itself should sit at the animal's computed position,
  // not the boat's. Leave at -1 to just use latCol/lonCol directly (the
  // right choice for Environmental/Events/Effort, where the vessel's own
  // position *is* the point of interest).
  int overlayLatCol = -1;
  int overlayLonCol = -1;

  int labelCol = -1;        // column identifying the row (used in tooltips)
  int overlayTextCol = -1;  // column whose value is drawn as a text label
  // If set, overrides overlayTextCol entirely -- called fresh for every
  // charted row, so it can reflect a setting that changes at runtime
  // (e.g. Sightings' configurable "which columns to use as the map
  // label" -- see the Settings tab's Map section).
  std::function<wxString(GenericGridTable* table, int row)> labelTextFn;
  // next to the marker (e.g. Sighting #)

  // If >= 0, auto-filled on Add Row with (highest existing integer value
  // in this column across all rows) + 1, starting at 1. Used for the
  // Sightings tab's "Sighting #" column.
  int autoIncrementCol = -1;

  // If true (the default), a new row's empty CHOICE columns are set to
  // their first listed option rather than left blank. Sightings sets
  // this false: auto-filling e.g. Species to its first option would
  // mean a freshly-added, not-yet-observed row could easily be left
  // showing a plausible-looking but simply wrong species if the
  // observer doesn't happen to touch that cell.
  bool defaultChoicesToFirstOption = true;

  // If true, a new row copies values from the immediately preceding row
  // for every column except chart/time/lat/lon/autoIncrement, and any
  // column individually marked ColumnDef::skipInherit. Used by
  // Environmental: conditions (sea state, weather, who's on watch)
  // usually haven't changed since the last check, so starting from
  // those same values is a better default than blank -- but takes
  // priority over defaultChoicesToFirstOption only when there *is* a
  // previous row to copy from.
  bool inheritPreviousRowValues = false;

  // Optional: called after any cell edit so derived columns (e.g.
  // sighting lat/lon computed from bearing+distance) can be recomputed.
  std::function<void(DataTab* tab, GenericGridTable* table, int row,
                     int editedCol)>
      recompute;

  // Optional periodic reminder (currently used by the Environmental tab):
  // counts down from `reminderMinutes`, resets whenever a row is added to
  // this tab, and visibly flags the tab (grid tint) once it expires. The
  // countdown display and interval control both live in LogWindow's
  // status bar, not on the tab itself -- see
  // GetReminderIntervalMinutes()/SetReminderIntervalMinutes().
  bool enableReminderTimer = false;
  int reminderMinutes = 30;
};

// One charted (Map=checked) row's position + rendering hints, for the
// plugin's chart overlay renderer.
struct ChartPoint {
  double lat;
  double lon;
  wxString labelText;  // drawn next to the marker if non-empty (e.g. "3")
  wxColour color;      // this point's own marker color, resolved via
                       // this row's Species/Event -- see
                       // ColumnDef::chartColorKeyCol and
                       // DataTab::chart_default_color_lookup
};

// Maps a human-readable color name (as used in species.csv/
// event_types.csv's color column) to an actual wxColour. Shared between
// DataTab.cpp and LogWindow.cpp (which reads species.csv/
// event_types.csv's color column and needs to resolve it the same way).
wxColour NamedColorToColour(const wxString& name);

class DataTab {
public:
  DataTab(wxWindow* parent, DataTabConfig cfg, const wxString& dataDir,
          const wxString& filePrefix);

  wxWindow* GetPanel() const { return m_panel; }
  const wxString& Title() const { return m_cfg.title; }

  void AddRow();
  void DeleteSelectedRows();
  void SaveCsv();
  void ExportCopyTo(const wxString& destDir);

  // Clears all rows (used by "Start New Survey") and, if newPrefix is
  // given, switches this tab to a new CSV file named for that prefix --
  // the *previous* file is left untouched on disk (not deleted), so
  // past survey data is never lost, just no longer the active file.
  void StartNewFile(const wxString& newPrefix);

  // Switches this tab to an *existing* survey's file for the given
  // prefix, loading whatever data is already there (unlike
  // StartNewFile(), which always starts empty) -- see "Load Survey..."
  // in LogWindow. If no file exists yet at that prefix for this tab
  // specifically (e.g. the source survey never had any Events rows),
  // this tab just starts empty for it, the same as StartNewFile()
  // would.
  void LoadSurvey(const wxString& newPrefix);

  // Sets a cell by column name, going through the same code path a manual
  // grid edit would (fires recompute / overlay refresh / CSV save). Used
  // by keyboard-shortcut row insertion.
  void SetCellValueByName(int row, const wxString& colName,
                          const wxString& value);
  wxString GetCellValueByName(int row, const wxString& colName) const;

  wxString GetCsvPath() const { return m_csvPath; }
  int RowCount() const;

  // Current pixel width of each visible column, in display order. Also
  // exposes a way to force the same resize-to-fit recalculation a real
  // wxEVT_SIZE would trigger, without needing an actual window resize
  // event -- useful for testing.
  std::vector<int> GetColumnWidths() const;
  void ForceResizeColumnsToFit() { ResizeColumnsToFit(); }

  // Kept up to date by the plugin on every GPS fix, used to fill in
  // sensible defaults when a row is added.
  // `cog` (course over ground) is used as a proxy for vessel heading
  // when converting a DegRel/ClockRel bearing to an absolute one for
  // the sighting-position calculation -- the plugin API doesn't expose
  // a dedicated heading source, and COG is the standard substitute,
  // though it can diverge from true heading with current/leeway drift.
  void SetVesselFix(double lat, double lon, const wxDateTime& utc);

  // All rows with Chart=checked, positioned per overlayLatCol/overlayLonCol
  // (falling back to latCol/lonCol). Used by the plugin's RenderOverlay.
  std::vector<ChartPoint> GetChartedPoints() const;

  // Called whenever a row is added/deleted or edited in a way that could
  // change what's drawn on the chart overlay (set by LogWindow after
  // construction, since it needs to reach back into the plugin to
  // request a canvas redraw).
  std::function<void()> on_chart_changed;

  // Called by GetChartedPoints() to resolve a charted row's marker
  // color -- given the value of that row's chartColorKeyCol (e.g. the
  // Species name), returns the color configured for it in
  // species.csv/event_types.csv, or this tab's own fallback marker
  // color if there's no entry (set by LogWindow after construction,
  // since it needs to reach into whichever CategoryConfigFile is
  // relevant for this tab).
  std::function<wxColour(const wxString& key)> chart_default_color_lookup;

  // Fired whenever the grid's selected cell changes to a different
  // column, with that column's name -- used by LogWindow to show the
  // column's definition (see ColumnDefinitions) at the bottom of the
  // window.
  std::function<void(const wxString& columnName)> on_cell_selected;

  // Fired at the end of OnRowAdded(), after all of this tab's own
  // auto-fill logic has already run -- used by LogWindow to auto-create
  // a linked Surfacings row whenever a Sightings row is added.
  std::function<void(int row)> on_row_added_external;

  // Current observer eye height in feet, looked up from the Effort
  // tab's Position column (see LogWindow's polling in OnStatusTick) --
  // needed for the "reticles" DistUnit's horizon-based range
  // calculation. Kept updated the same way vessel position/COG are,
  // just from a different source (Effort's Position, not a GPS fix).
  double GetObserverHeightFt() const { return m_observerHeightFt; }
  void SetObserverHeightFt(double heightFt) { m_observerHeightFt = heightFt; }

  // Fired whenever a cell in a column named `colName` is edited to
  // exactly `value`. Currently used so LogWindow can turn tracking on
  // automatically when Effort Status is set to "ON" -- being on effort
  // without a track being recorded isn't a state that makes sense.
  void WatchColumnValue(const wxString& colName, const wxString& value,
                        std::function<void()> callback);

  // Forces the grid to redraw (e.g. after the global lat/lon format
  // changed) without altering any data.
  // Reverts the most recent add/delete/edit on this tab -- a single
  // level of undo (not a full history stack): each mutating operation
  // overwrites the one saved snapshot, so only the *last* change can be
  // undone, not several changes back. Returns false (does nothing) if
  // there's nothing to undo.
  bool Undo();
  // Reverts the most recent Undo() -- also single-level: calling Redo()
  // twice in a row does nothing the second time, and making any new
  // edit after an Undo() clears the redo snapshot (the same way it
  // would in a spreadsheet or text editor: redo is only available
  // immediately after an undo, not after doing something else).
  bool Redo();

  // Opens the multi-select dialog for whatever cell currently has the
  // grid cursor, if it's a MULTI_CHOICE column. Returns false (does
  // nothing) otherwise. Used both by OnGridKeyDown (Enter/F2/Space with
  // the grid focused) and, as a backup, by LogWindow's top-level
  // CHAR_HOOK handler for a bare spacebar -- added after a reported
  // case where spacebar wasn't working, on the theory that something
  // upstream of the grid's own key handling might be intercepting a
  // bare Space before OnGridKeyDown ever sees it.
  bool TryOpenMultiSelectForCurrentCell();

  // Explicitly starts editing whatever cell currently has the grid
  // cursor (Enter or a plain typed character, when the grid isn't
  // already editing), showing the cell's *existing* content rather
  // than replacing it -- confirmed via direct testing that wxGrid's
  // supposed native "typing/Enter starts editing" behavior does not
  // reliably trigger on its own in this environment (only F2 reliably
  // does), and that consuming this at the grid's own wxEVT_KEY_DOWN
  // level isn't sufficient either -- something afterward (almost
  // certainly wxGrid's own internal wxEVT_CHAR-based handling, a
  // separate event neither KEY_DOWN nor CHAR_HOOK alone consuming
  // KEY_DOWN prevents) still cancels the edit state. Called from
  // LogWindow's CHAR_HOOK handler instead, which is early and
  // authoritative enough to actually stick. Returns false (does
  // nothing) for a MULTI_CHOICE/BUTTON/read-only cell, or if already
  // editing.
  bool TryStartEditingCurrentCell();

  // Clears every row and rewrites the CSV file (the *same* file, not a
  // new prefixed one -- unlike StartNewFile()) with just the header --
  // used by "Clear Survey Data". Genuinely destructive; the caller is
  // responsible for confirming with the user first.
  void ClearAllData();

  void RefreshDisplay();

  // Fired when a cell in a BUTTON-type column is clicked, with the row
  // index -- used by Sightings' "surf" column to add a pre-populated
  // row on the Surfacing tab and switch focus there.
  std::function<void(int row)> on_button_column_clicked;

  // Applies a point size to both the grid's cell contents and its
  // column headers -- see DisplaySettings::GridFontSize(), the
  // "increase text size" control (display.csv's grid_font_size key).
  void SetGridFontSize(int pointSize);

  // Re-measures and re-applies every row's height to fit its current
  // content/font -- see LogWindow's constructor for why this needs to
  // be called again, deferred, after the window has actually been
  // shown (SetGridFontSize() alone, called before that, can end up
  // measuring against a not-yet-real grid size for rows loaded from an
  // existing CSV).
  void ReapplyRowHeights();

  // Reminder timer, driven entirely from outside (LogWindow's status
  // bar) now -- there's no in-tab UI for it any more. No-ops / returns
  // defaults if this tab didn't enable one.
  bool HasReminderTimer() const { return m_cfg.enableReminderTimer; }
  bool IsReminderOverdue() const;
  wxString GetReminderCountdownText() const;
  int GetReminderIntervalMinutes() const { return m_reminderMinutes; }
  void SetReminderIntervalMinutes(int minutes);

private:
  void RebuildCsvPath();
  void LoadCsv();
  void OnCellEdited(int row, int col);
  void OnRowAdded(int row);
  void ApplyPerCellSetup(int row);
  void OnRowAboutToDelete(int row);

  void OnReminderTick();
  void ResetReminderTimer();

  void ResizeColumnsToFit();
  void UpdateContentMinWidths();
  int MinWidthForLabel(const wxString& label) const;
  void OnCellDoubleClick(wxGridEvent& evt);
  void OnCellLeftClick(wxGridEvent& evt);
  void OnCellSelected(wxGridEvent& evt);
  void OnGridKeyDown(wxKeyEvent& evt);
  void OnCellChanging(wxGridEvent& evt);
  void OpenMultiSelectEditor(int row, int col);

  DataTabConfig m_cfg;
  wxString m_csvPath;
  wxString m_dataDir;
  wxString m_filePrefix;

  GenericGridTable* m_table;  // owned by m_grid (SetTable ..., true)
  wxGrid* m_grid;
  wxPanel* m_panel;
  wxColour m_normalGridBg;  // captured at construction, restored after
                            // a reminder-overdue tint (respects dark
                            // mode / whatever the platform theme is)
  wxBoxSizer* m_toolbarSizer;

  bool m_haveFix;
  double m_fixLat, m_fixLon;
  // NAN (not a reasonable fallback number) before the Effort tab's
  // Position is ever set -- so the "reticles" DistUnit's calculation
  // also produces NAN in that case, rather than silently using some
  // made-up height and computing a distance that looks plausible but
  // isn't backed by anything real.
  double m_observerHeightFt = std::numeric_limits<double>::quiet_NaN();
  wxDateTime m_fixTime;

  // Reminder timer (Environmental tab only; null/unused otherwise). No
  // in-tab UI any more -- the countdown display and interval spinctrl
  // both live in LogWindow's status bar.
  wxTimer m_reminderTimer;
  int m_reminderMinutes;
  wxDateTime m_reminderDeadline;

  // See WatchColumnValue().
  std::vector<std::tuple<wxString, wxString, std::function<void()>>>
      m_watchedColumnValues;

  // Unlimited undo/redo: a stack of whole-table snapshots, one pushed
  // just before each mutating operation (add/delete/edit). A whole-
  // table snapshot rather than tracking a precise diff per change is
  // deliberately the simplest thing that works correctly for the row
  // counts a survey produces (hundreds, not millions, so the memory/
  // performance cost of keeping many full copies around is
  // negligible) -- no risk of a partial-undo bug leaving the table in a
  // state that doesn't correspond to anything that was ever actually
  // saved. An earlier version kept only a single most-recent snapshot;
  // switched to an actual stack per direct request, to match how
  // undo/redo work in most other applications (repeatedly undoing
  // steps back through a whole editing session, not just the last
  // change).
  std::vector<std::vector<std::vector<wxString>>> m_undoStack;
  void SaveUndoSnapshot();
  void RestoreSnapshot(const std::vector<std::vector<wxString>>& snapshot);

  // See Redo() -- every Undo() pushes what it's about to replace onto
  // this stack, so Redo() can restore it, and can keep being called to
  // work back through everything that's been undone. Cleared entirely
  // (not just left stale) whenever a genuinely new edit happens, via
  // SaveUndoSnapshot() itself, so redo is never available after
  // something new has been changed -- the same convention a spreadsheet
  // or text editor follows.
  std::vector<std::vector<std::vector<wxString>>> m_redoStack;

  // Per-visible-column content-based minimum width, refreshed by
  // UpdateContentMinWidths() after any data change -- used as an
  // additional floor in ResizeColumnsToFit(), so a column never shrinks
  // below what its own widest cell needs, even after the window is
  // resized narrower well after that content was entered (a plain
  // AutoSizeColumns() call alone only fixes the width at that moment;
  // ResizeColumnsToFit()'s later proportional-scaling could otherwise
  // shrink it right back below content width on the next resize).
  std::vector<int> m_contentMinWidths;
};

#endif  // WHALE_DATA_TAB_H
