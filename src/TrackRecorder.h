#ifndef WHALE_TRACK_RECORDER_H
#define WHALE_TRACK_RECORDER_H

#include <wx/wx.h>
#include <wx/file.h>
#include <wx/datetime.h>
#include <vector>

// Since OpenCPN's plugin API has no exposed function to start/stop
// OpenCPN's own native track recording, this plugin keeps its own
// internal trackline instead: every position fix is throttled and
// appended to track.csv (crash-safe, append-only -- same idea as the
// other data tabs) in the plugin's data directory, and kept in memory
// for the chart overlay to draw as a line.
class TrackRecorder {
public:
  struct Point {
    double lat;
    double lon;
    wxString effortStatus;  // "ON", "OFF", or "" (not yet logged)
    wxString effortSegNo;   // blank unless effortStatus == "ON"
  };

  explicit TrackRecorder(const wxString& dataDir);
  ~TrackRecorder();

  // Called on every GPS fix. Internally throttled to at most one
  // recorded point every GetIntervalSeconds(), except the very first fix
  // of the session, which is always recorded immediately. A no-op while
  // disabled -- see SetEnabled(). `effortStatus` ("ON"/"OFF"/"") and
  // `effortSegNo` are the Effort tab's current state (read from its most
  // recent row), recorded alongside each point so the track file shows
  // which parts of the transit were on-effort.
  void RecordFix(double lat, double lon, double sog, double cog,
                 const wxDateTime& localTime, const wxString& effortStatus,
                 const wxString& effortSegNo);

  wxString GetCsvPath() const { return m_csvPath; }

  // Starts a fresh trackline: clears the in-memory points (so the
  // overlay draws a new, disconnected line rather than joining straight
  // across from wherever the vessel was at the end of the previous
  // survey) and switches to a newly-prefixed CSV file, leaving the
  // previous file untouched on disk. Used by "Start New Survey".
  void StartNewFile(const wxString& newPrefix);

  // Clears every recorded point and rewrites the *same* track file
  // (not a new prefixed one) with just the header -- used by "Clear
  // Survey Data". Genuinely destructive; the caller is responsible for
  // confirming with the user first.
  void ClearAllData();

  // Copies the current track file to destDir, same filename -- used by
  // "Export Data...", alongside each DataTab's own ExportCopyTo().
  void ExportCopyTo(const wxString& destDir) const;

  // All points recorded so far this session, *plus* whatever was already
  // in track.csv when the plugin started (loaded at construction) -- so
  // this is always the complete trackline, not just points since launch.
  // Used by the plugin's RenderOverlay/RenderGLOverlay.
  const std::vector<Point>& GetPoints() const { return m_points; }

  // Runtime-adjustable via the "Tracking" tab (see TrackingSettings for
  // the persisted defaults these are initialized from).
  bool IsEnabled() const { return m_enabled; }
  void SetEnabled(bool enabled) { m_enabled = enabled; }
  int GetIntervalSeconds() const { return m_intervalSeconds; }
  void SetIntervalSeconds(int seconds) {
    if (seconds > 0) m_intervalSeconds = seconds;
  }

private:
  void OpenAtPath(const wxString& path);

  wxString m_csvPath;
  wxString m_dataDir;
  wxFile m_file;
  wxDateTime m_lastRecordedAt;
  bool m_haveRecordedAny;
  std::vector<Point> m_points;
  bool m_enabled = true;
  int m_intervalSeconds = 10;
};

#endif  // WHALE_TRACK_RECORDER_H
