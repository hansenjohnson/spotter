#include "PositionHeights.h"
#include "CsvUtils.h"

#include <wx/filename.h>

namespace {
struct DefaultEntry {
  const char* position;
  double heightFt;
};
const DefaultEntry kDefaults[] = {
    {"Wheelhouse", 20.0},
    {"Topdeck", 30.0},
    {"Bow", 10.0},
};
}  // namespace

PositionHeights::PositionHeights(const wxString& dataDir) {
  wxFileName fn(dataDir, "positions.csv");
  m_path = fn.GetFullPath();

  if (!wxFileExists(m_path)) {
    CreateDefaults();
  }
  Load();
}

void PositionHeights::CreateDefaults() {
  std::vector<wxString> header = {"position", "height_ft"};
  std::vector<std::vector<wxString>> rows;
  for (const auto& d : kDefaults) {
    rows.push_back(
        {wxString(d.position), wxString::Format("%.0f", d.heightFt)});
  }
  CsvUtils::WriteAll(m_path, header, rows);
}

void PositionHeights::Load() {
  auto rows = CsvUtils::ReadAll(m_path);
  for (size_t i = 1; i < rows.size(); i++) {  // skip header row
    if (rows[i].size() < 2) continue;
    wxString position = rows[i][0];
    double heightFt = 0.0;
    if (position.IsEmpty() || !rows[i][1].ToDouble(&heightFt)) continue;
    m_positions.emplace_back(position, heightFt);
  }
}

wxArrayString PositionHeights::GetPositionNames() const {
  wxArrayString out;
  for (const auto& p : m_positions) out.Add(p.first);
  return out;
}

double PositionHeights::GetHeightFt(const wxString& position,
                                    double fallbackFt) const {
  for (const auto& p : m_positions) {
    if (p.first == position) return p.second;
  }
  return fallbackFt;
}
