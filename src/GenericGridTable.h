#ifndef WHALE_GENERIC_GRID_TABLE_H
#define WHALE_GENERIC_GRID_TABLE_H

#include <wx/wx.h>
#include <wx/grid.h>
#include <vector>
#include <functional>

// One column of a data tab.
struct ColumnDef {
  // MULTI_CHOICE behaves like TEXT for direct typing/editing (stored as
  // a comma-separated string), but double-clicking the cell opens a
  // checklist dialog (populated from `choices`) for picking several
  // values at once -- see DataTab::OnCellDoubleClick.
  enum Type { TEXT, BOOL, CHOICE, MULTI_CHOICE, HIDDEN, BUTTON };

  wxString name;
  Type type = TEXT;
  wxArrayString choices;  // only used when type == CHOICE
  int width = 110;
  // Fixed display caption, used only when type == BUTTON -- every cell
  // in the column shows this same text regardless of stored data (the
  // column doesn't actually store meaningful per-row data at all, it's
  // just a clickable action).
  wxString buttonCaption;

  // If true, this column is never copied from the previous row even
  // when the owning tab has DataTabConfig::inheritPreviousRowValues
  // set -- for genuinely one-off, per-row text like Notes, where
  // repeating the last row's value would almost never be wanted.
  bool skipInherit = false;

  // Marks this column as holding a latitude or longitude in decimal
  // degrees (the raw stored value never changes), so GetValue() can
  // format it for display per the current LatLonFormat setting while
  // GetRawValue() (used when editing) still returns/accepts plain
  // decimal degrees.
  enum class GeoRole { None, Latitude, Longitude };
  GeoRole geoRole = GeoRole::None;

  // If true, this CHOICE column's dropdown keeps `choices`' exact
  // as-defined order, rather than being alphabetically (or, for an
  // all-numeric list, numerically) sorted for the dropdown popup --
  // for columns like Glare (None/Mild/Moderate/Severe) or a confidence
  // scale (Definite/Probable/Possible), where the defined order *is*
  // the meaningful one (severity, confidence), and alphabetizing it
  // would scramble that. Confirmed as a real, reported bug: Glare's
  // intended None/Mild/Moderate/Severe order was coming out as
  // Mild/Moderate/None/Severe once the general alphabetical-sort
  // behavior (added for a different, genuine reason: helping type-
  // ahead navigation for long, naturally-unordered lists like Species)
  // was applied indiscriminately to every CHOICE column.
  bool preserveChoiceOrder = false;

  // Explicit default for a CHOICE column, applied to every new row
  // regardless of the tab-wide DataTabConfig::defaultChoicesToFirstOption
  // flag -- for a column where one specific option is always the right
  // starting point (e.g. DistUnit defaulting to "nm", the unit distance
  // calculations already assume), even on a tab (like Sightings) that
  // otherwise deliberately leaves CHOICE columns blank rather than
  // auto-filling them (so a not-yet-identified Species, for instance,
  // doesn't end up looking like it was actually identified). Empty
  // (the default) means no override -- falls back to whatever the
  // tab-wide flag says.
  wxString defaultValue;

  ColumnDef() = default;
  ColumnDef(const wxString& n, Type t, int w = 110)
      : name(n), type(t), width(w) {}
  ColumnDef(const wxString& n, const wxArrayString& ch, int w = 130)
      : name(n), type(CHOICE), choices(ch), width(w) {}
};

// A simple string-backed table for wxGrid: every cell is stored as a
// wxString and interpreted according to its column's ColumnDef. HIDDEN
// columns are still stored in each row (used to remember a per-row
// waypoint GUID across restarts) but are not exposed to the grid widget --
// they must be the trailing columns in `cols`.
//
// After every edit, insert, or delete, `on_changed` is invoked so the
// owning DataTab can recompute derived cells, sync the chart waypoint,
// and persist the whole table to disk immediately.
class GenericGridTable : public wxGridTableBase {
public:
  GenericGridTable(std::vector<ColumnDef> cols);

  int GetNumberRows() override;
  int GetNumberCols() override;
  bool IsEmptyCell(int row, int col) override;
  wxString GetValue(int row, int col) override;
  void SetValue(int row, int col, const wxString& value) override;
  wxString GetColLabelValue(int col) override;
  wxString GetTypeName(int row, int col) override;
  bool CanGetValueAs(int row, int col, const wxString& typeName) override;
  bool CanSetValueAs(int row, int col, const wxString& typeName) override;
  bool GetValueAsBool(int row, int col) override;
  void SetValueAsBool(int row, int col, bool value) override;

  bool AppendRows(size_t numRows = 1) override;
  bool DeleteRows(size_t pos, size_t numRows = 1) override;

  // Direct (non-UI) access, used for load/save/derived-column logic.
  int NumDataCols() const { return static_cast<int>(m_cols.size()); }
  int NumVisibleCols() const { return m_numVisible; }
  wxString RawGet(int row, int dataCol) const;
  void RawSet(int row, int dataCol, const wxString& value);
  int AppendDataRow(const std::vector<wxString>& values);  // returns row idx
  void RemoveDataRow(int row);
  int FindColByName(const wxString& name) const;

  // AppendDataRow() (unlike AppendRows()) doesn't notify the attached
  // wxGrid of the new row count -- by design, since it's meant for bulk
  // loading at startup, before a grid is even attached yet. Anything
  // that calls AppendDataRow() *after* the grid is already attached and
  // showing (e.g. DataTab::Undo(), restoring rows from a snapshot) must
  // call this afterward, or the grid's own internal row-count
  // bookkeeping silently desyncs from the table's actual row count --
  // confirmed via direct testing that this reliably produces a wxGrid
  // assertion failure ("invalid row index" in GetRowPos()) the next
  // time the grid tries to do almost anything.
  void NotifyGridRowCountChanged(int oldRowCount);

  std::vector<std::vector<wxString>>& Data() { return m_data; }
  const std::vector<ColumnDef>& Cols() const { return m_cols; }

  // Called after any SetValue / AppendRows / DeleteRows that originates
  // from the grid UI (not from bulk loading at startup).
  std::function<void(int row, int col)> on_cell_edited;
  std::function<void(int row)> on_row_added;
  std::function<void(int row)> on_row_about_to_delete;

private:
  std::vector<ColumnDef> m_cols;
  std::vector<std::vector<wxString>> m_data;
  int m_numVisible;
};

#endif  // WHALE_GENERIC_GRID_TABLE_H
