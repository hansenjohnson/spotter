#include "DisplaySettings.h"
#include "CsvUtils.h"

#include <wx/filename.h>
#include <wx/tokenzr.h>

namespace {
// Larger than this plugin's original hardcoded values (radius 7,
// default-size label text, line width 2) per direct user feedback that
// markers/labels/lines were too small to read on a chart at typical
// zoom levels.
const double kDefaultMarkerRadius = 14.0;
const int kDefaultLabelFontSize = 14;
const int kDefaultTrackLineWidth = 4;
const int kDefaultGridFontSize = 11;
const int kDefaultUiFontSize = 11;

wxString ColorToString(const wxColour& c) {
  return wxString::Format("%d,%d,%d", c.Red(), c.Green(), c.Blue());
}

bool ParseColor(const wxString& s, wxColour* out) {
  wxStringTokenizer tok(s, ",");
  int vals[3] = {0, 0, 0};
  for (int i = 0; i < 3; i++) {
    if (!tok.HasMoreTokens()) return false;
    long v = 0;
    if (!tok.GetNextToken().ToLong(&v)) return false;
    vals[i] = static_cast<int>(v);
  }
  *out = wxColour(vals[0], vals[1], vals[2]);
  return true;
}

}  // namespace

DisplaySettings::DisplaySettings(const wxString& dataDir)
    : m_markerRadius(kDefaultMarkerRadius),
      m_labelFontSize(kDefaultLabelFontSize),
      m_trackLineWidth(kDefaultTrackLineWidth),
      m_gridFontSize(kDefaultGridFontSize),
      m_uiFontSize(kDefaultUiFontSize) {
  wxFileName fn(dataDir, "display.csv");
  m_path = fn.GetFullPath();

  if (!wxFileExists(m_path)) {
    CreateDefaults();
  }
  Load();
}

void DisplaySettings::CreateDefaults() {
  std::vector<wxString> header = {"key", "value"};
  std::vector<std::vector<wxString>> rows = {
      {"marker_radius", wxString::Format("%.0f", kDefaultMarkerRadius)},
      {"label_font_size", wxString::Format("%d", kDefaultLabelFontSize)},
      {"track_line_width", wxString::Format("%d", kDefaultTrackLineWidth)},
      {"grid_font_size", wxString::Format("%d", kDefaultGridFontSize)},
      {"ui_font_size", wxString::Format("%d", kDefaultUiFontSize)},
  };
  CsvUtils::WriteAll(m_path, header, rows);
}

void DisplaySettings::Load() {
  auto rows = CsvUtils::ReadAll(m_path);
  for (size_t i = 1; i < rows.size(); i++) {  // skip header row
    if (rows[i].size() < 2) continue;
    wxString key = rows[i][0];
    wxString value = rows[i][1];
    key.Trim(true).Trim(false);
    value.Trim(true).Trim(false);

    double d = 0.0;
    long l = 0;
    if (key == "marker_radius" && value.ToDouble(&d) && d > 0) {
      m_markerRadius = d;
    } else if (key == "label_font_size" && value.ToLong(&l) && l > 0) {
      m_labelFontSize = static_cast<int>(l);
    } else if (key == "track_line_width" && value.ToLong(&l) && l > 0) {
      m_trackLineWidth = static_cast<int>(l);
    } else if (key == "grid_font_size" && value.ToLong(&l) && l > 0) {
      m_gridFontSize = static_cast<int>(l);
    } else if (key == "ui_font_size" && value.ToLong(&l) && l > 0) {
      m_uiFontSize = static_cast<int>(l);
    } else if (key == "track_visible") {
      m_trackVisible = (value.CmpNoCase("true") == 0 || value == "1");
    } else if (key == "effort_segments_visible") {
      m_effortSegmentsVisible = (value.CmpNoCase("true") == 0 || value == "1");
    } else {
      // Anything else (per-tab marker shape/color keys, and any future
      // addition) is kept as a generic string so it round-trips through
      // Save() even if this particular build doesn't have a dedicated
      // field for it.
      m_values[key] = value;
    }
  }
}

void DisplaySettings::Save() const {
  std::vector<wxString> header = {"key", "value"};
  std::vector<std::vector<wxString>> rows = {
      {"marker_radius", wxString::Format("%.0f", m_markerRadius)},
      {"label_font_size", wxString::Format("%d", m_labelFontSize)},
      {"track_line_width", wxString::Format("%d", m_trackLineWidth)},
      {"grid_font_size", wxString::Format("%d", m_gridFontSize)},
      {"ui_font_size", wxString::Format("%d", m_uiFontSize)},
      {"track_visible", m_trackVisible ? "true" : "false"},
      {"effort_segments_visible", m_effortSegmentsVisible ? "true" : "false"},
  };
  for (const auto& kv : m_values) {
    rows.push_back({kv.first, kv.second});
  }
  CsvUtils::WriteAll(m_path, header, rows);
}

wxString DisplaySettings::MarkerShape(const wxString& tabKey,
                                      const wxString& fallback) const {
  auto it = m_values.find(tabKey + "_marker_shape");
  if (it == m_values.end() || it->second.IsEmpty()) return fallback;
  return it->second;
}

wxColour DisplaySettings::MarkerColor(const wxString& tabKey,
                                      const wxColour& fallback) const {
  auto it = m_values.find(tabKey + "_marker_color");
  if (it == m_values.end()) return fallback;
  wxColour c;
  if (ParseColor(it->second, &c)) return c;
  return fallback;
}

void DisplaySettings::SetMarkerShape(const wxString& tabKey,
                                     const wxString& shape) {
  m_values[tabKey + "_marker_shape"] = shape;
  Save();
}

void DisplaySettings::SetMarkerColor(const wxString& tabKey,
                                     const wxColour& color) {
  m_values[tabKey + "_marker_color"] = ColorToString(color);
  Save();
}

wxColour DisplaySettings::TrackColor(const wxColour& fallback) const {
  auto it = m_values.find("track_color");
  if (it == m_values.end()) return fallback;
  wxColour c;
  if (ParseColor(it->second, &c)) return c;
  return fallback;
}

void DisplaySettings::SetTrackColor(const wxColour& color) {
  m_values["track_color"] = ColorToString(color);
  Save();
}

void DisplaySettings::SetTrackVisible(bool visible) {
  m_trackVisible = visible;
  Save();
}

wxColour DisplaySettings::EffortSegmentColor(const wxColour& fallback) const {
  auto it = m_values.find("effort_segment_color");
  if (it == m_values.end()) return fallback;
  wxColour c;
  if (ParseColor(it->second, &c)) return c;
  return fallback;
}

void DisplaySettings::SetEffortSegmentColor(const wxColour& color) {
  m_values["effort_segment_color"] = ColorToString(color);
  Save();
}

void DisplaySettings::SetEffortSegmentsVisible(bool visible) {
  m_effortSegmentsVisible = visible;
  Save();
}

wxArrayString DisplaySettings::SightingsLabelColumns() const {
  auto it = m_values.find("sightings_label_columns");
  wxArrayString out;
  wxString raw =
      it != m_values.end() ? it->second : wxString("Species,FieldID");
  wxStringTokenizer tok(raw, ",");
  while (tok.HasMoreTokens()) {
    wxString t = tok.GetNextToken();
    t.Trim(true).Trim(false);
    if (!t.IsEmpty()) out.Add(t);
  }
  return out;
}

void DisplaySettings::SetSightingsLabelColumns(const wxArrayString& columns) {
  m_values["sightings_label_columns"] = wxJoin(columns, ',');
  Save();
}

wxArrayString DisplaySettings::EventsLabelColumns() const {
  auto it = m_values.find("events_label_columns");
  wxArrayString out;
  wxString raw = it != m_values.end() ? it->second : wxString("Event,ID");
  wxStringTokenizer tok(raw, ",");
  while (tok.HasMoreTokens()) {
    wxString t = tok.GetNextToken();
    t.Trim(true).Trim(false);
    if (!t.IsEmpty()) out.Add(t);
  }
  return out;
}

void DisplaySettings::SetEventsLabelColumns(const wxArrayString& columns) {
  m_values["events_label_columns"] = wxJoin(columns, ',');
  Save();
}
