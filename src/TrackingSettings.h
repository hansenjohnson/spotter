#ifndef WHALE_TRACKING_SETTINGS_H
#define WHALE_TRACKING_SETTINGS_H

#include <wx/wx.h>

// Loads/persists the "Tracking" tab's settings (enabled on/off, and the
// recording interval in seconds) from a plain external CSV file
// (tracking.csv) in the plugin's data directory: key,value pairs, same
// pattern as DisplaySettings/ShortcutsFile. Created with sensible
// defaults (enabled, 10 second interval) on first run.
class TrackingSettings {
public:
  explicit TrackingSettings(const wxString& dataDir);

  bool Enabled() const { return m_enabled; }
  int IntervalSeconds() const { return m_intervalSeconds; }

  // Both setters immediately rewrite the file, so a change made from the
  // Tracking tab survives an OpenCPN restart right away.
  void SetEnabled(bool enabled);
  void SetIntervalSeconds(int seconds);

  wxString GetPath() const { return m_path; }

private:
  void Save() const;
  void Load();

  wxString m_path;
  bool m_enabled;
  int m_intervalSeconds;
};

#endif  // WHALE_TRACKING_SETTINGS_H
