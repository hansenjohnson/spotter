#ifndef SPOTTER_PI_H
#define SPOTTER_PI_H

// wx/wx.h must come before ocpn_plugin.h: that vendored header's
// DECL_EXP macro (which makes create_pi/destroy_pi actually exported
// from the plugin DLL on Windows, via __declspec(dllexport)) depends
// on __WXMSW__, which wxWidgets' own headers define -- not us. With
// ocpn_plugin.h included first, __WXMSW__ isn't defined yet when that
// macro's platform check runs, so DECL_EXP silently expands to
// nothing even on a real Windows build. Confirmed as the exact,
// direct cause of a real failure loading the built DLL in OpenCPN on
// Windows ("Couldn't find symbol 'create_pi'") -- not a hypothetical
// concern.
#include <wx/wx.h>
#include "ocpn_plugin.h"
#include "TrackRecorder.h"
#include "DisplaySettings.h"
#include "TrackingSettings.h"
#include <memory>

#define SPOTTER_VERSION_MAJOR 0
#define SPOTTER_VERSION_MINOR 1

class LogWindow;

// ----------------------------------------------------------------------
// SpotterPlugin
//
// Records wildlife sightings, environmental conditions, other survey
// events (CTD casts, drifter deployments, drone flights, tagging), and
// on/off survey-effort status in a spreadsheet-style log window with one
// tab per data type. Every edit is written straight to CSV files on
// disk. Charted rows and the internally-recorded trackline are drawn
// directly on the chart via a custom overlay -- see RenderOverlay/
// RenderGLOverlay below.
// ----------------------------------------------------------------------
class SpotterPlugin : public opencpn_plugin_118 {
public:
  explicit SpotterPlugin(void* ppimgr);
  ~SpotterPlugin();

  // Required overrides
  int Init(void) override;
  bool DeInit(void) override;

  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;
  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  wxBitmap* GetPlugInBitmap() override;

  wxString GetCommonName() override;
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;

  // Optional overrides we use
  void SetPositionFix(PlugIn_Position_Fix& pfix) override;
  void OnToolbarToolCallback(int id) override;

  // Chart overlay: draws charted Sightings/Events markers and the
  // recorded trackline. Both the legacy single-canvas and the newer
  // multi-canvas entry points are implemented (delegating to the same
  // drawing code) since it's not obvious which a given OpenCPN build
  // actually calls -- see README.
  bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) override;
  bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp) override;
  bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex,
                                int priority) override;
  bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                  int canvasIndex, int priority) override;

  // Shown when the user clicks "Preferences" next to this plugin in
  // Options > Plugins -- OpenCPN calls this directly, potentially
  // without the log window ever having been opened, so it's a
  // standalone dialog rather than anything routed through LogWindow.
  // Per direct request, just links to the same CSV settings files the
  // Settings tab links to, with a note that the full settings UI
  // (including the ones that aren't just "open this file," like the
  // Map label columns or Beaufort-scale-aware sorting) lives there,
  // not here.
  void ShowPreferencesDialog(wxWindow* parent) override;

  // Called whenever charted data changes (row added/edited/deleted) so
  // the chart redraws promptly instead of waiting for some unrelated
  // repaint trigger.
  void RequestOverlayRedraw();

  wxString GetDataDir() const { return m_dataDir; }
  // Where this survey's own CSV files live (<data dir>/data) -- kept
  // separate from the settings/config files directly in GetDataDir()
  // (species.csv, timezone.txt, etc) per direct request, so a survey's
  // own data isn't sitting at the same folder level as this plugin's
  // configuration.
  wxString GetSurveyDataDir() const { return m_surveyDataDir; }
  // Where this plugin's own settings/config files live
  // (<data dir>/settings) -- current_survey.txt, latlon_format.txt,
  // timezone.txt, tracking.csv. Kept separate from GetDataDir() itself
  // (species.csv, shortcuts.csv, etc, which users are meant to edit
  // directly) per direct request, since these four are only ever
  // written by this plugin itself and aren't meant for users to edit.
  wxString GetSettingsDir() const { return m_settingsDir; }
  int GetLogToolId() const { return m_logToolId; }
  LogWindow* GetLogWindow() const { return m_logWindow; }
  TrackRecorder* GetTrackRecorder() const { return m_trackRecorder; }
  DisplaySettings* GetDisplaySettings() { return m_displaySettings; }
  TrackingSettings* GetTrackingSettings() { return m_trackingSettings; }

  // GPS watchdog support (see LogWindow's periodic check). Note this
  // plugin API has no push notification for "signal lost" -- SetPositionFix
  // simply stops being called -- so LogWindow polls these on a timer
  // rather than being told directly.
  bool HasEverHadFix() const { return m_haveFix; }
  int SecondsSinceLastFix() const;
  void GetLastFix(double& lat, double& lon) const;
  double GetLastSog() const { return m_lastSog; }

  // A second, distinct kind of "something's wrong with the GPS" signal:
  // fixes are arriving on schedule (SecondsSinceLastFix() stays low), but
  // the actual position value hasn't meaningfully moved in a while --
  // could be a stuck/repeating feed, or could just be a genuinely
  // stationary vessel, but it's still worth flagging separately from
  // "no fix at all". A tiny position-change threshold (roughly 5 meters)
  // is used so ordinary GPS jitter doesn't itself look like "changed".
  int SecondsSincePositionChanged() const;

private:
  void CreateToolbarItems();
  wxString ResolveDataDir() const;
  // Resolves (and creates if needed) the "data" subfolder within
  // GetDataDir() where survey data files (<prefix>_sightings.csv,
  // <prefix>_track.csv, etc) live.
  wxString ResolveSurveyDataDir() const;
  wxString ResolveSettingsDir() const;
  // One-time-per-load migration of any survey data files still sitting
  // at the old, pre-"data subfolder" location directly in m_dataDir.
  void MigrateSurveyDataFilesToSubfolder() const;
  void MigrateSettingsFilesToSubfolder() const;
  void EnsureLogWindow();  // (re)creates m_logWindow if it's currently null
  void DrawOverlayDC(wxDC& dc, PlugIn_ViewPort* vp);
  void DrawOverlayGL(PlugIn_ViewPort* vp);

  int m_logToolId;

  bool m_haveFix;
  double m_lastLat;
  double m_lastLon;
  double m_lastSog;
  wxDateTime m_lastFixTime;        // timestamp carried in the fix itself
  wxDateTime m_lastFixReceivedAt;  // wall-clock time we got the callback

  bool m_havePositionChangeBaseline;
  double m_lastDistinctLat;
  double m_lastDistinctLon;
  wxDateTime m_lastPositionChangeAt;

  wxString m_dataDir;
  wxString m_surveyDataDir;
  wxString m_settingsDir;
  LogWindow* m_logWindow;  // created lazily, reused for the whole session
  TrackRecorder* m_trackRecorder;
  DisplaySettings* m_displaySettings;
  TrackingSettings* m_trackingSettings;
};

#endif  // SPOTTER_PI_H
