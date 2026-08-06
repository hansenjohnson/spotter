#ifndef WHALE_COLUMN_DEFINITIONS_H
#define WHALE_COLUMN_DEFINITIONS_H

#include <wx/wx.h>
#include <map>
#include <utility>

// Loads/creates column_definitions.csv: a short definition for every
// column on every tab (tab,column,definition rows), generated once on
// first run from this plugin's own built-in defaults (see
// CreateDefaults()) so it always starts in sync with the actual current
// columns. Not regenerated on later runs even if columns change in a
// newer version of the plugin -- edit the file directly to fix a
// definition that's gone stale, the same as dropdowns.csv/
// shortcuts.csv/display.csv.
class ColumnDefinitions {
public:
  explicit ColumnDefinitions(const wxString& dataDir);

  // Looks up the definition for `tab`/`column` (exact, case-sensitive
  // match against what's in the file). Returns an empty string if
  // there's no entry -- e.g. a column added after the file was last
  // regenerated, or a typo'd rename in the file itself.
  wxString GetDefinition(const wxString& tab, const wxString& column) const;

  wxString GetPath() const { return m_path; }

private:
  void CreateDefaults();
  void Load();

  wxString m_path;
  std::map<std::pair<wxString, wxString>, wxString> m_definitions;
};

#endif  // WHALE_COLUMN_DEFINITIONS_H
