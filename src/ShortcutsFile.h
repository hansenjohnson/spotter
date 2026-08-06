#ifndef WHALE_SHORTCUTS_FILE_H
#define WHALE_SHORTCUTS_FILE_H

#include <wx/wx.h>
#include <map>

// Loads keyboard shortcut definitions from a plain external CSV file
// (shortcuts.csv, separate from dropdowns.csv) in the plugin's data
// directory: two columns, key_combo and action, one shortcut per row.
// An action can be a bare "AddSighting"-style row-add, or extended with
// ":Field=Value" pairs to also pre-fill specific columns (see
// ShortcutsFile.cpp's default entries for examples). Created with
// sensible defaults on first run.
class ShortcutsFile {
public:
  explicit ShortcutsFile(const wxString& dataDir);

  // Key combo string (e.g. "Cmd+1") -> action name (e.g. "AddSighting").
  const std::map<wxString, wxString>& Get() const { return m_shortcuts; }

  wxString GetPath() const { return m_path; }

private:
  void CreateDefaults();
  void Load();

  wxString m_path;
  std::map<wxString, wxString> m_shortcuts;
};

#endif  // WHALE_SHORTCUTS_FILE_H
