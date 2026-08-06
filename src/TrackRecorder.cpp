#include "TrackRecorder.h"
#include "CsvUtils.h"
#include "TimeZoneSetting.h"
#include "ocpn_plugin.h"

#include <wx/filename.h>
#include <wx/dir.h>
#include <vector>

namespace {
const char* kHeader = "local_time,lat,lon,sog_kts,cog_deg,effort,seg_no\n";
}

TrackRecorder::TrackRecorder(const wxString& dataDir)
    : m_dataDir(dataDir), m_haveRecordedAny(false) {
  if (!wxDir::Exists(dataDir)) {
    wxFileName::Mkdir(dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  OpenAtPath(wxFileName(dataDir, "track.csv").GetFullPath());
}

TrackRecorder::~TrackRecorder() {
  if (m_file.IsOpened()) m_file.Close();
}

void TrackRecorder::OpenAtPath(const wxString& path) {
  if (m_file.IsOpened()) m_file.Close();
  m_csvPath = path;

  bool exists = wxFileExists(m_csvPath);

  // Load whatever was already recorded at this path (e.g. earlier in the
  // same survey, before a restart) so the overlay renders the complete
  // trackline, not just points from this particular session.
  if (exists) {
    auto rows = CsvUtils::ReadAll(m_csvPath);
    for (size_t i = 1; i < rows.size(); i++) {
      if (rows[i].size() < 3) continue;
      double lat = 0.0, lon = 0.0;
      if (rows[i][1].ToDouble(&lat) && rows[i][2].ToDouble(&lon)) {
        Point p;
        p.lat = lat;
        p.lon = lon;
        if (rows[i].size() > 5) p.effortStatus = rows[i][5];
        if (rows[i].size() > 6) p.effortSegNo = rows[i][6];
        m_points.push_back(p);
      }
    }
  }

  bool ok = exists ? m_file.Open(m_csvPath, wxFile::write_append)
                   : m_file.Create(m_csvPath, false);
  if (ok && !exists) {
    m_file.Write(kHeader);
  }
}

void TrackRecorder::StartNewFile(const wxString& newPrefix) {
  m_points.clear();           // don't draw a line connecting to the old
                              // survey's last position
  m_haveRecordedAny = false;  // first fix of the new file is always
                              // recorded immediately, same as at startup
  wxString filename = newPrefix + "_track.csv";
  OpenAtPath(wxFileName(m_dataDir, filename).GetFullPath());
}

void TrackRecorder::ClearAllData() {
  m_points.clear();
  m_haveRecordedAny = false;
  if (m_file.IsOpened()) m_file.Close();
  // Directly truncate and recreate, rather than going through
  // OpenAtPath() -- that reloads whatever points already exist at a
  // path when the file is already there, which is exactly the opposite
  // of what "clear" needs. wxFile::Create(path, true) with overwrite=true
  // truncates any existing file.
  if (m_file.Create(m_csvPath, true /* overwrite */)) {
    m_file.Write(kHeader);
  }
}

void TrackRecorder::ExportCopyTo(const wxString& destDir) const {
  if (m_file.IsOpened()) const_cast<wxFile&>(m_file).Flush();
  if (!wxDir::Exists(destDir)) {
    wxFileName::Mkdir(destDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }

  // Computed here, at export time, rather than recorded live into
  // track.csv as each fix comes in -- simpler (no format change to an
  // already-append-only, crash-safe-by-design file, and no risk of a
  // half-written or stale distance value if a point were ever
  // corrected/removed), and the distance for the whole trackline is
  // just as easy to compute in one pass over what's already on disk.
  auto rows = CsvUtils::ReadAll(m_csvPath);
  if (!rows.empty()) {
    std::vector<wxString> header = rows[0];
    header.push_back("nm_since_prev");
    std::vector<std::vector<wxString>> outRows;
    outRows.reserve(rows.size() - 1);
    double prevLat = 0.0, prevLon = 0.0;
    bool havePrev = false;
    for (size_t i = 1; i < rows.size(); i++) {
      std::vector<wxString> row = rows[i];
      double lat = 0.0, lon = 0.0;
      wxString nmStr;
      if (row.size() > 2 && row[1].ToDouble(&lat) && row[2].ToDouble(&lon)) {
        if (havePrev) {
          double brg = 0.0, dist = 0.0;
          DistanceBearingMercator_Plugin(prevLat, prevLon, lat, lon, &brg,
                                         &dist);
          nmStr = wxString::FromDouble(dist, 3);
        }
        prevLat = lat;
        prevLon = lon;
        havePrev = true;
      }
      row.push_back(nmStr);
      outRows.push_back(std::move(row));
    }
    wxFileName dest(destDir, wxFileName(m_csvPath).GetFullName());
    CsvUtils::WriteAll(dest.GetFullPath(), header, outRows);
    return;
  }

  // Fallback for an empty/unreadable file -- a plain copy is at least
  // as good as nothing.
  wxFileName src(m_csvPath);
  wxFileName dest(destDir, src.GetFullName());
  wxCopyFile(m_csvPath, dest.GetFullPath());
}

void TrackRecorder::RecordFix(double lat, double lon, double sog, double cog,
                              const wxDateTime& localTime,
                              const wxString& effortStatus,
                              const wxString& effortSegNo) {
  if (!m_enabled || !m_file.IsOpened()) return;

  wxDateTime t = localTime.IsValid() ? localTime : wxDateTime::Now();

  if (m_haveRecordedAny) {
    wxTimeSpan sinceLast = t - m_lastRecordedAt;
    if (sinceLast.GetSeconds() < m_intervalSeconds) return;
  }

  // Formatted per the Settings tab's General > Timezone choice
  // (TimeZoneSetting) -- same as every data tab's Time column, so
  // track.csv's timestamps stay consistent with the rest of a survey's
  // data rather than always reflecting the computer's own configured
  // timezone regardless of what the data tabs are using.
  wxString line;
  line << TimeZoneSetting::FormatInSelectedZone(t) << ","
       << wxString::FromDouble(lat, 6) << "," << wxString::FromDouble(lon, 6)
       << "," << wxString::FromDouble(sog, 1) << ","
       << wxString::FromDouble(cog, 1) << "," << effortStatus << ","
       << effortSegNo << "\n";
  m_file.Write(line);
  m_file.Flush();

  m_lastRecordedAt = t;
  m_haveRecordedAny = true;
  Point p;
  p.lat = lat;
  p.lon = lon;
  p.effortStatus = effortStatus;
  p.effortSegNo = effortSegNo;
  m_points.push_back(p);
}
