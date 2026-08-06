#include "CategoryConfigFile.h"
#include "CsvUtils.h"

#include <wx/filename.h>

CategoryConfigFile::CategoryConfigFile(
    const wxString& dataDir, const wxString& filename,
    const std::vector<wxString>& extraColumnNames,
    const std::vector<std::vector<wxString>>& defaultRows)
    : m_extraColumnNames(extraColumnNames) {
  wxFileName fn(dataDir, filename);
  m_path = fn.GetFullPath();

  if (!wxFileExists(m_path)) {
    CreateDefaults(defaultRows);
  }
  Load();
}

void CategoryConfigFile::CreateDefaults(
    const std::vector<std::vector<wxString>>& defaultRows) {
  std::vector<wxString> header = {"name"};
  for (const auto& col : m_extraColumnNames) header.push_back(col);
  CsvUtils::WriteAll(m_path, header, defaultRows);
}

void CategoryConfigFile::Load() {
  auto rows = CsvUtils::ReadAll(m_path);
  for (size_t i = 1; i < rows.size(); i++) {  // skip header row
    if (rows[i].empty()) continue;
    wxString name = rows[i][0];
    if (name.IsEmpty()) continue;
    if (m_names.Index(name) == wxNOT_FOUND) m_names.Add(name);
    for (size_t c = 0; c < m_extraColumnNames.size(); c++) {
      // Column index c+1, since column 0 is always "name" -- blank
      // (rather than skipped) if the row is short a trailing column or
      // two, so a partially-filled-in row (e.g. name and color given,
      // species_code left off) still loads the columns it does have.
      wxString value = (c + 1 < rows[i].size()) ? rows[i][c + 1] : wxString();
      m_fields[name][m_extraColumnNames[c]] = value;
    }
  }
}

wxString CategoryConfigFile::GetField(const wxString& name,
                                      const wxString& columnName) const {
  auto nameIt = m_fields.find(name);
  if (nameIt == m_fields.end()) return wxString();
  auto fieldIt = nameIt->second.find(columnName);
  if (fieldIt == nameIt->second.end()) return wxString();
  return fieldIt->second;
}
