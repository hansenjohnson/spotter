#ifndef SPOTTER_CATEGORY_CONFIG_FILE_H
#define SPOTTER_CATEGORY_CONFIG_FILE_H

#include <wx/wx.h>
#include <map>
#include <vector>

// Loads one category's editable list (species, events, observers, or
// behaviors) from its own CSV file in the plugin's data directory --
// replaces the single, all-categories-in-one-file dropdowns.csv from an
// earlier version of this plugin, per direct request, so each list can
// have its own extra columns (a default map color for species/events, a
// species code, an observer's full name, a behavior code) without
// those columns being meaningless for the other categories sharing the
// same file.
//
// Every file's first column is always "name" (the value shown in the
// dropdown itself); which other columns exist, and in what order, is
// specified by extraColumnNames when constructing this class. If the
// file doesn't exist yet, it's created with the given starter defaults
// on first run, so it's immediately useful and self-documenting -- open
// species.csv (for example) in any spreadsheet app, add/remove/reorder
// rows, or fill in the color/code columns for your own survey, and
// restart the plugin (or just reopen the log window) to pick up the
// changes.
class CategoryConfigFile {
public:
  // extraColumnNames: e.g. {"color", "species_code"} for species.csv.
  // defaultRows: each inner vector is {name, extraCol1Value,
  // extraCol2Value, ...} for one starter row, written to the file only
  // if it doesn't already exist.
  CategoryConfigFile(const wxString& dataDir, const wxString& filename,
                     const std::vector<wxString>& extraColumnNames,
                     const std::vector<std::vector<wxString>>& defaultRows);

  // The "name" column values, in the file's own order -- what's shown
  // in the actual dropdown.
  wxArrayString Names() const { return m_names; }

  // A specific extra column's value for a given name (e.g.
  // GetField("Humpback whale", "color")) -- blank if the name or column
  // doesn't exist, or the cell itself was left blank in the file.
  wxString GetField(const wxString& name, const wxString& columnName) const;

  wxString GetPath() const { return m_path; }

private:
  void CreateDefaults(const std::vector<std::vector<wxString>>& defaultRows);
  void Load();

  wxString m_path;
  std::vector<wxString> m_extraColumnNames;
  wxArrayString m_names;
  std::map<wxString, std::map<wxString, wxString>> m_fields;  // name ->
                                                              // {columnName
                                                              // -> value}
};

#endif  // SPOTTER_CATEGORY_CONFIG_FILE_H
