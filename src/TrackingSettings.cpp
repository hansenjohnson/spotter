#include "TrackingSettings.h"
#include "CsvUtils.h"

#include <wx/filename.h>

namespace {
// Tracking starts OFF -- it only turns on (and stays on across restarts,
// unless the user explicitly disables it from the Tracking tab) once
// "Start New Survey" is used for the first time, so a track is always
// associated with a specific survey rather than accumulating before any
// survey has actually begun. See LogWindow::StartNewSurvey().
const bool kDefaultEnabled = false;
const int kDefaultIntervalSeconds = 10;
}  // namespace

TrackingSettings::TrackingSettings(const wxString& dataDir)
    : m_enabled(kDefaultEnabled), m_intervalSeconds(kDefaultIntervalSeconds) {
  wxFileName fn(dataDir, "tracking.csv");
  m_path = fn.GetFullPath();

  if (!wxFileExists(m_path)) {
    Save();
  }
  Load();
}

void TrackingSettings::SetEnabled(bool enabled) {
  m_enabled = enabled;
  Save();
}

void TrackingSettings::SetIntervalSeconds(int seconds) {
  if (seconds <= 0) return;
  m_intervalSeconds = seconds;
  Save();
}

void TrackingSettings::Save() const {
  std::vector<wxString> header = {"key", "value"};
  std::vector<std::vector<wxString>> rows = {
      {"enabled", m_enabled ? "true" : "false"},
      {"interval_seconds", wxString::Format("%d", m_intervalSeconds)},
  };
  CsvUtils::WriteAll(m_path, header, rows);
}

void TrackingSettings::Load() {
  auto rows = CsvUtils::ReadAll(m_path);
  for (size_t i = 1; i < rows.size(); i++) {  // skip header row
    if (rows[i].size() < 2) continue;
    wxString key = rows[i][0];
    wxString value = rows[i][1];
    key.Trim(true).Trim(false);
    value.Trim(true).Trim(false);

    if (key == "enabled") {
      m_enabled = (value.CmpNoCase("true") == 0 || value == "1");
    } else if (key == "interval_seconds") {
      long v = 0;
      if (value.ToLong(&v) && v > 0) m_intervalSeconds = static_cast<int>(v);
    }
  }
}
