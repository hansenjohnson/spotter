#include "spotter_pi.h"
#include "LogWindow.h"
#include "LatLonFormat.h"
#include "TimeZoneSetting.h"
#include "SpotterIcon.h"

#include <wx/image.h>
#include <wx/file.h>
#include <wx/mstream.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/glcanvas.h>
#include <wx/hyperlink.h>
#include <limits>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------
// The two required factory functions. OpenCPN loads the plugin shared
// library and calls these to create/destroy the plugin instance.
// ---------------------------------------------------------------------
extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new SpotterPlugin(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

// ---------------------------------------------------------------------
// The plugin's icon/logo -- binoculars with a whale fluke in one
// eyepiece and a dorsal fin in the other. Decoded from an embedded PNG
// (SpotterIcon.h) at runtime and scaled to whatever size is actually
// needed (toolbar buttons and the Options > Plugins list icon both call
// this, currently at the same size, but each can ask for a different
// one if that's ever useful) -- avoids needing a separate,
// fixed-resolution copy for each use, and avoids any dependency on an
// external resource file being findable at some particular install-time
// path (this plugin has no such files at all otherwise).
//
// White-on-transparent, matching every other OpenCPN toolbar icon's
// convention -- a black variant, switched to based on the active color
// scheme via the documented SetColorScheme() plugin API hook, was tried
// and then reverted per direct request in favor of just matching that
// existing convention directly.
static wxBitmap MakeLogIcon(int size = 32) {
  wxMemoryInputStream stream(kSpotterIconPng, kSpotterIconPngLen);
  wxImage img(stream, wxBITMAP_TYPE_PNG);
  if (!img.IsOk()) {
    // Shouldn't normally happen (the embedded data is fixed at build
    // time), but fall back to a plain transparent square rather than
    // crashing if it somehow does.
    wxImage blank(size, size);
    blank.InitAlpha();
    for (int x = 0; x < size; x++)
      for (int y = 0; y < size; y++) blank.SetAlpha(x, y, 0);
    return wxBitmap(blank);
  }
  img.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
  return wxBitmap(img);
}

// Default marker colors, used unless overridden per-tab via display.csv
// (see DisplaySettings::MarkerColor) -- kept distinct from each other
// and from typical chart colors so charted rows are easy to spot at a
// glance.
static const wxColour kSightingColor(230, 120, 20);    // orange
static const wxColour kEventColor(30, 100, 220);       // blue
static const wxColour kSurfacingColor(40, 160, 90);    // green
static const wxColour kTrackColor(160, 30, 200, 180);  // translucent purple
static const wxColour kEffortSegmentColor(0, 160, 60, 220);  // green

namespace {

// Marker shape is now a per-*tab* setting (configurable via
// DisplaySettings, one shape/color per tab, changed from a toolbar
// button on that tab -- see DataTab's "Marker:"/color-swatch controls),
// not per-row. An earlier version let each *row* pick its own shape via
// a "Mark Type" dropdown, which caused a crash that couldn't be reliably
// reproduced or root-caused even with live GL-context testing; a single
// shape/color read once per repaint, rather than looked up per point,
// is a meaningfully simpler and safer design.
std::vector<wxPoint2DDouble> ShapeVertices(const wxString& shapeName,
                                           double radius) {
  std::vector<wxPoint2DDouble> pts;
  if (shapeName == "Square") {
    pts = {{-radius, -radius},
           {radius, -radius},
           {radius, radius},
           {-radius, radius}};
  } else if (shapeName == "Triangle") {
    pts = {{0, -radius},
           {radius * 0.87, radius * 0.5},
           {-radius * 0.87, radius * 0.5}};
  } else if (shapeName == "Circle") {
    const int kSegments = 16;
    for (int i = 0; i < kSegments; i++) {
      double a = 2.0 * M_PI * i / kSegments;
      pts.emplace_back(radius * cos(a), radius * sin(a));
    }
  } else if (shapeName == "Star") {
    const int kPoints = 5;
    for (int i = 0; i < kPoints * 2; i++) {
      double a = M_PI * i / kPoints - M_PI / 2.0;
      double r = (i % 2 == 0) ? radius : radius * 0.45;
      pts.emplace_back(r * cos(a), r * sin(a));
    }
  } else {  // "Diamond" (default)
    pts = {{0, -radius}, {radius, 0}, {0, radius}, {-radius, 0}};
  }
  return pts;
}

}  // namespace

// =======================================================================
SpotterPlugin::SpotterPlugin(void* ppimgr)
    : opencpn_plugin_118(ppimgr),
      m_logToolId(-1),
      m_haveFix(false),
      m_lastLat(0.0),
      m_lastLon(0.0),
      m_lastSog(0.0),
      m_havePositionChangeBaseline(false),
      m_lastDistinctLat(0.0),
      m_lastDistinctLon(0.0),
      m_logWindow(nullptr),
      m_trackRecorder(nullptr),
      m_displaySettings(nullptr),
      m_trackingSettings(nullptr) {}

SpotterPlugin::~SpotterPlugin() {}

wxString SpotterPlugin::ResolveDataDir() const {
  wxString base = *GetpPrivateApplicationDataLocation();
  wxFileName dir(base, "");
  dir.AppendDir("spotter");
  wxString path = dir.GetPath();
  if (!wxDir::Exists(path)) {
    wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  return path;
}

wxString SpotterPlugin::ResolveSurveyDataDir() const {
  wxFileName dir(m_dataDir, "");
  dir.AppendDir("data");
  wxString path = dir.GetPath();
  if (!wxDir::Exists(path)) {
    wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  return path;
}

void SpotterPlugin::MigrateSurveyDataFilesToSubfolder() const {
  // Survey data files (sightings/effort/events/surfacings/track, each
  // either bare or "<prefix>_"-prefixed) used to live directly in
  // m_dataDir, before an earlier round moved them into their own "data"
  // subfolder (m_surveyDataDir) to keep them separate from this
  // plugin's settings/config files. Anyone who used this plugin before
  // that change would have their existing survey files stranded at the
  // old location -- invisible to "Load Survey..." (which only ever
  // looks in the new subfolder) even though the files themselves are
  // still sitting right there. Run once per plugin load; cheap even
  // when there's nothing to do (just one directory listing).
  static const wxString kSuffixes[] = {"sightings.csv", "effort.csv",
                                       "events.csv", "surfacings.csv",
                                       "track.csv"};
  wxDir dir(m_dataDir);
  if (!dir.IsOpened()) return;
  wxString filename;
  bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
  while (cont) {
    for (const auto& suffix : kSuffixes) {
      if (filename == suffix || filename.EndsWith("_" + suffix)) {
        wxString src = wxFileName(m_dataDir, filename).GetFullPath();
        wxString dst = wxFileName(m_surveyDataDir, filename).GetFullPath();
        // Never overwrite something already in the new location (e.g.
        // if this migration somehow partially ran before, or a file of
        // the same name was independently created there already) --
        // skip rather than risk clobbering newer data with older.
        if (!wxFileExists(dst)) wxRenameFile(src, dst);
        break;
      }
    }
    cont = dir.GetNext(&filename);
  }
}

wxString SpotterPlugin::ResolveSettingsDir() const {
  wxFileName dir(m_dataDir, "");
  dir.AppendDir("settings");
  wxString path = dir.GetPath();
  if (!wxDir::Exists(path)) {
    wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  return path;
}

void SpotterPlugin::MigrateSettingsFilesToSubfolder() const {
  // Same reasoning and same one-time-per-load approach as
  // MigrateSurveyDataFilesToSubfolder() just above, for this plugin's
  // own settings files (current_survey.txt, latlon_format.txt,
  // timezone.txt, tracking.csv) instead of survey data -- these used to
  // live directly in m_dataDir too, before being split into their own
  // "settings" subfolder (per direct request, to keep files this
  // plugin alone ever writes separate from ones a user is meant to
  // edit directly, like species.csv).
  static const wxString kFilenames[] = {"current_survey.txt",
                                        "latlon_format.txt", "timezone.txt",
                                        "tracking.csv"};
  for (const auto& filename : kFilenames) {
    wxString src = wxFileName(m_dataDir, filename).GetFullPath();
    wxString dst = wxFileName(m_settingsDir, filename).GetFullPath();
    if (wxFileExists(src) && !wxFileExists(dst)) wxRenameFile(src, dst);
  }
}

int SpotterPlugin::Init(void) {
  // Needed to decode the embedded PNG icon (SpotterIcon.h/MakeLogIcon())
  // -- OpenCPN itself normally already registers this before loading any
  // plugin, but registering it explicitly here doesn't depend on that
  // still being true in some future OpenCPN version, and is harmless if
  // it's already registered (wxImage tracks handlers by type internally).
  wxImage::AddHandler(new wxPNGHandler());

  m_dataDir = ResolveDataDir();
  m_surveyDataDir = ResolveSurveyDataDir();
  m_settingsDir = ResolveSettingsDir();
  MigrateSurveyDataFilesToSubfolder();
  MigrateSettingsFilesToSubfolder();

  m_trackRecorder = new TrackRecorder(m_surveyDataDir);
  m_displaySettings = new DisplaySettings(m_dataDir);
  m_trackingSettings = new TrackingSettings(m_settingsDir);
  LatLonFormat::LoadFromFile(
      wxFileName(m_settingsDir, "latlon_format.txt").GetFullPath());
  TimeZoneSetting::LoadFromFile(
      wxFileName(m_settingsDir, "timezone.txt").GetFullPath());

  // Restores the current survey's file prefix onto the track recorder,
  // if one was ever set (i.e. "Start New Survey" has been used at some
  // point) -- confirmed as a real, reported bug without this: the
  // track recorder is otherwise always constructed pointed at the
  // plain, unprefixed track.csv above, and nothing else ever re-applies
  // the saved prefix to it automatically. That meant every OpenCPN
  // restart mid-survey had a window (potentially the entire session, if
  // the log window was never opened) where new track points were
  // silently recorded to the wrong file. current_survey.txt's prefix is
  // always its last non-blank line, in both the current (survey,
  // prefix) and an older (vessel, survey, prefix) layout -- see
  // LogWindow::LoadCurrentSurveyInfo() for the fuller, display-name-
  // aware version of this same read, used once the log window is
  // actually opened.
  {
    wxFileName currentSurveyFn(m_settingsDir, "current_survey.txt");
    if (wxFileExists(currentSurveyFn.GetFullPath())) {
      wxFile f(currentSurveyFn.GetFullPath());
      if (f.IsOpened()) {
        wxString contents;
        f.ReadAll(&contents);
        wxArrayString lines = wxSplit(contents, '\n');
        while (!lines.IsEmpty() && lines.Last().IsEmpty()) {
          lines.RemoveAt(lines.size() - 1);
        }
        if (!lines.IsEmpty()) {
          wxString prefix = lines.Last();
          prefix.Trim(true).Trim(false);
          if (!prefix.IsEmpty()) m_trackRecorder->StartNewFile(prefix);
        }
      }
    }
  }

  m_trackRecorder->SetEnabled(m_trackingSettings->Enabled());
  m_trackRecorder->SetIntervalSeconds(m_trackingSettings->IntervalSeconds());

  EnsureLogWindow();
  CreateToolbarItems();

  // WANTS_NMEA_EVENTS is what actually gates OpenCPN calling
  // SetPositionFix()/SetPositionFixEx() at all (see
  // SendPositionFixToAllPlugIns() in OpenCPN's plugin_comm.cpp) --
  // despite the name suggesting it's only about raw NMEA sentences, it's
  // the single flag controlling position-fix delivery too.
  return (WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
          WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_NMEA_EVENTS);
}

void SpotterPlugin::EnsureLogWindow() {
  if (m_logWindow) return;
  m_logWindow = new LogWindow(GetOCPNCanvasWindow(), this, m_dataDir);
  m_logWindow->on_closed = [this]() { m_logWindow = nullptr; };
}

void SpotterPlugin::CreateToolbarItems() {
  wxBitmap logIcon = MakeLogIcon();
  m_logToolId = InsertPlugInTool(
      "Spotter", &logIcon, &logIcon, wxITEM_NORMAL, "Open the Spotter log",
      "Open the spreadsheet log window", nullptr, -1, 0, this);
}

bool SpotterPlugin::DeInit(void) {
  if (m_logWindow) {
    // Deliberately NOT calling wxWindow::Destroy() here -- see the
    // long-form explanation in earlier project history / git blame:
    // Destroy() defers actual deletion to the next idle event, but
    // OpenCPN unloads this library essentially immediately after
    // DeInit() returns, with no intervening idle-loop iteration, so a
    // still-pending deferred deletion later jumps to a vtable that no
    // longer exists in memory and crashes. An immediate `delete` here
    // guarantees the whole window hierarchy is torn down synchronously
    // while the code is still mapped.
    m_logWindow->on_closed = nullptr;  // about to delete it ourselves
    delete m_logWindow;
    m_logWindow = nullptr;
  }
  delete m_trackRecorder;
  m_trackRecorder = nullptr;
  delete m_displaySettings;
  m_displaySettings = nullptr;
  delete m_trackingSettings;
  m_trackingSettings = nullptr;
  return true;
}

int SpotterPlugin::GetAPIVersionMajor() { return API_VERSION_MAJOR; }
int SpotterPlugin::GetAPIVersionMinor() { return API_VERSION_MINOR; }
int SpotterPlugin::GetPlugInVersionMajor() { return SPOTTER_VERSION_MAJOR; }
int SpotterPlugin::GetPlugInVersionMinor() { return SPOTTER_VERSION_MINOR; }

wxBitmap* SpotterPlugin::GetPlugInBitmap() {
  static wxBitmap bmp = MakeLogIcon();
  return &bmp;
}

wxString SpotterPlugin::GetCommonName() { return "Spotter"; }

wxString SpotterPlugin::GetShortDescription() {
  return "Spreadsheet log for sightings, environment, events and "
         "survey effort";
}

wxString SpotterPlugin::GetLongDescription() {
  return "Records wildlife sightings, environmental conditions, "
         "other survey events (CTD casts, drifter deployments, drone "
         "flights, tagging) and on/off survey-effort status in an "
         "editable spreadsheet-style log window, one tab per data type. "
         "Every edit is written straight to CSV files on disk. Charted "
         "rows and the recorded trackline are drawn directly on the "
         "chart via a custom overlay.\n\ngithub.com/hansenjohnson/spotter";
}

void SpotterPlugin::SetPositionFix(PlugIn_Position_Fix& pfix) {
  m_haveFix = true;
  m_lastLat = pfix.Lat;
  m_lastLon = pfix.Lon;
  m_lastSog = pfix.Sog;
  m_lastFixReceivedAt = wxDateTime::Now();
  if (pfix.FixTime > 0) {
    m_lastFixTime = wxDateTime(static_cast<time_t>(pfix.FixTime));
  } else {
    m_lastFixTime = wxDateTime::Now();
  }

  // A second, distinct "something's off" signal from plain "no fix at
  // all": fixes keep arriving right on schedule, but the position isn't
  // actually moving. ~0.00005 degrees is roughly 5 meters -- comfortably
  // bigger than ordinary GPS jitter, so jitter alone won't keep counting
  // as "changed" and reset this.
  const double kMeaningfulChangeDegrees = 0.00005;
  if (!m_havePositionChangeBaseline ||
      std::abs(pfix.Lat - m_lastDistinctLat) > kMeaningfulChangeDegrees ||
      std::abs(pfix.Lon - m_lastDistinctLon) > kMeaningfulChangeDegrees) {
    m_lastDistinctLat = pfix.Lat;
    m_lastDistinctLon = pfix.Lon;
    m_lastPositionChangeAt = wxDateTime::Now();
    m_havePositionChangeBaseline = true;
  }

  if (m_logWindow)
    m_logWindow->NotifyVesselFix(m_lastLat, m_lastLon, m_lastFixTime);
  if (m_trackRecorder) {
    wxString effortStatus =
        m_logWindow ? m_logWindow->CurrentEffortStatus() : wxString();
    wxString effortSegNo =
        m_logWindow ? m_logWindow->CurrentEffortSegNo() : wxString();
    m_trackRecorder->RecordFix(pfix.Lat, pfix.Lon, pfix.Sog, pfix.Cog,
                               m_lastFixTime, effortStatus, effortSegNo);
    RequestOverlayRedraw();
  }
}

int SpotterPlugin::SecondsSinceLastFix() const {
  if (!m_haveFix || !m_lastFixReceivedAt.IsValid()) {
    return std::numeric_limits<int>::max();
  }
  wxTimeSpan elapsed = wxDateTime::Now() - m_lastFixReceivedAt;
  return static_cast<int>(elapsed.GetSeconds().ToLong());
}

int SpotterPlugin::SecondsSincePositionChanged() const {
  if (!m_havePositionChangeBaseline || !m_lastPositionChangeAt.IsValid()) {
    return 0;
  }
  wxTimeSpan elapsed = wxDateTime::Now() - m_lastPositionChangeAt;
  return static_cast<int>(elapsed.GetSeconds().ToLong());
}

void SpotterPlugin::GetLastFix(double& lat, double& lon) const {
  lat = m_lastLat;
  lon = m_lastLon;
}

void SpotterPlugin::OnToolbarToolCallback(int id) {
  if (id == m_logToolId) {
    EnsureLogWindow();  // recreates it if the user closed it earlier
    m_logWindow->Show();
    m_logWindow->Raise();
  }
}

void SpotterPlugin::RequestOverlayRedraw() {
  RequestRefresh(GetOCPNCanvasWindow());
}

// -----------------------------------------------------------------------
// Chart overlay: draws charted Sightings/Events markers and the recorded
// trackline. Both a wxDC path (RenderOverlay, for non-GL chart rendering)
// and an OpenGL path (RenderGLOverlay, for GL-mode chart rendering,
// which is the default in recent OpenCPN) are implemented since it's not
// obvious ahead of time which one an actual running OpenCPN will invoke.
// The multi-canvas variants just delegate to the same two functions.
// -----------------------------------------------------------------------

void SpotterPlugin::ShowPreferencesDialog(wxWindow* parent) {
  wxDialog dlg(parent, wxID_ANY, "Spotter Preferences", wxDefaultPosition,
               wxSize(420, -1));
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText* introText = new wxStaticText(
      &dlg, wxID_ANY,
      "Launch Spotter (the toolbar button) to access all of its "
      "settings, on the Settings tab. The links below open the same "
      "underlying CSV files directly, if that's ever handier -- "
      "editing one of these files while Spotter is also open and "
      "using it isn't recommended.");
  introText->Wrap(390);
  sizer->Add(introText, 0, wxALL | wxEXPAND, 12);

  wxStaticBoxSizer* filesBox = new wxStaticBoxSizer(wxVERTICAL, &dlg, "Files");
  auto addLinkToPath = [&](const wxString& text, const wxString& path) {
    wxHyperlinkCtrl* link =
        new wxHyperlinkCtrl(&dlg, wxID_ANY, text, wxEmptyString);
    wxColour linkColour(90, 150, 240);
    link->SetNormalColour(linkColour);
    link->SetVisitedColour(linkColour);
    link->SetHoverColour(linkColour.ChangeLightness(130));
    link->Bind(wxEVT_HYPERLINK, [&dlg, path](wxHyperlinkEvent&) {
      if (!wxLaunchDefaultApplication(path)) {
        wxMessageBox("Couldn't open it automatically. It's here:\n" + path,
                     "Spotter", wxOK | wxICON_INFORMATION, &dlg);
      }
    });
    filesBox->Add(link, 0, wxALL, 6);
  };
  auto addLink = [&](const wxString& text, const wxString& filename) {
    addLinkToPath(text, wxFileName(m_dataDir, filename).GetFullPath());
  };
  addLinkToPath("Open data folder", m_dataDir);
  addLink("Edit species list", "species.csv");
  addLink("Edit events list", "event_types.csv");
  addLink("Edit observers list", "observers.csv");
  addLink("Edit behaviors list", "behaviors.csv");
  addLink("Edit keyboard shortcuts", "shortcuts.csv");
  addLink("Edit observer positions/heights", "positions.csv");
  addLink("Edit column definitions", "column_definitions.csv");
  addLink("Edit marker/label/line sizes", "display.csv");
  sizer->Add(filesBox, 0, wxEXPAND | wxALL, 12);

  wxButton* closeBtn = new wxButton(&dlg, wxID_OK, "Close");
  sizer->Add(closeBtn, 0, wxALIGN_RIGHT | wxALL, 8);

  dlg.SetSizerAndFit(sizer);
  dlg.CentreOnParent();
  dlg.ShowModal();
}

bool SpotterPlugin::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {
  DrawOverlayDC(dc, vp);
  return true;
}

bool SpotterPlugin::RenderGLOverlay(wxGLContext* pcontext,
                                    PlugIn_ViewPort* vp) {
  wxUnusedVar(pcontext);
  DrawOverlayGL(vp);
  return true;
}

bool SpotterPlugin::RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp,
                                             int canvasIndex, int priority) {
  wxUnusedVar(canvasIndex);
  wxUnusedVar(priority);
  DrawOverlayDC(dc, vp);
  return true;
}

bool SpotterPlugin::RenderGLOverlayMultiCanvas(wxGLContext* pcontext,
                                               PlugIn_ViewPort* vp,
                                               int canvasIndex, int priority) {
  wxUnusedVar(pcontext);
  wxUnusedVar(canvasIndex);
  wxUnusedVar(priority);
  DrawOverlayGL(vp);
  return true;
}

void SpotterPlugin::DrawOverlayDC(wxDC& dc, PlugIn_ViewPort* vp) {
  if (!vp) return;

  double markerRadius =
      m_displaySettings ? m_displaySettings->MarkerRadius() : 14.0;
  int lineWidth = m_displaySettings ? m_displaySettings->TrackLineWidth() : 4;
  int fontSize = m_displaySettings ? m_displaySettings->LabelFontSize() : 14;
  bool trackVisible =
      m_displaySettings ? m_displaySettings->TrackVisible() : true;
  wxColour trackColor = m_displaySettings
                            ? m_displaySettings->TrackColor(kTrackColor)
                            : kTrackColor;
  bool segmentsVisible =
      m_displaySettings ? m_displaySettings->EffortSegmentsVisible() : true;
  wxColour segmentColor =
      m_displaySettings
          ? m_displaySettings->EffortSegmentColor(kEffortSegmentColor)
          : kEffortSegmentColor;

  // Trackline and effort segments only need m_trackRecorder, which
  // exists from Init() onward -- deliberately *not* gated behind
  // m_logWindow being non-null (unlike the marker drawing further down,
  // which genuinely does need it). m_logWindow is only ever created
  // lazily, the first time the toolbar button is clicked -- a real,
  // reported bug was that the trackline/effort segments simply didn't
  // draw at all after a restart until the log window had been opened
  // at least once, because this whole function used to bail out at the
  // top whenever m_logWindow was null, without regard for which parts
  // of the drawing below actually needed it.

  // Trackline first, so markers draw on top of it.
  if (m_trackRecorder && trackVisible) {
    const auto& points = m_trackRecorder->GetPoints();
    if (points.size() >= 2) {
      dc.SetPen(wxPen(trackColor, lineWidth));
      wxPoint prev;
      bool havePrev = false;
      for (const auto& p : points) {
        wxPoint px;
        GetCanvasPixLL(vp, &px, p.lat, p.lon);
        if (havePrev) dc.DrawLine(prev, px);
        prev = px;
        havePrev = true;
      }
    }
  }

  // Effort segments: drawn as a second, distinctly-colored pass directly
  // over the trackline, covering only the consecutive runs of points
  // recorded while Effort was ON (grouped by SegNo, so two separate
  // on-effort periods don't visually merge into one line even if they
  // happen to retrace similar ground).
  if (m_trackRecorder && segmentsVisible) {
    const auto& points = m_trackRecorder->GetPoints();
    dc.SetPen(wxPen(segmentColor, lineWidth + 1));
    size_t i = 0;
    while (i < points.size()) {
      if (points[i].effortStatus != "ON" || points[i].effortSegNo.IsEmpty()) {
        i++;
        continue;
      }
      wxString segNo = points[i].effortSegNo;
      size_t start = i;
      while (i < points.size() && points[i].effortStatus == "ON" &&
             points[i].effortSegNo == segNo) {
        i++;
      }
      // [start, i) is one continuous on-effort run.
      wxPoint prev;
      bool havePrev = false;
      for (size_t j = start; j < i; j++) {
        wxPoint px;
        GetCanvasPixLL(vp, &px, points[j].lat, points[j].lon);
        if (havePrev) dc.DrawLine(prev, px);
        prev = px;
        havePrev = true;
      }
    }
  }

  if (!m_logWindow) return;  // everything below (markers) needs it

  wxFont labelFont(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                   wxFONTWEIGHT_BOLD);

  auto drawMarkers = [&](const std::vector<ChartPoint>& pts,
                         const wxColour& fallbackColor,
                         const wxString& shapeName) {
    dc.SetFont(labelFont);
    dc.SetTextForeground(*wxBLACK);
    auto verts = ShapeVertices(shapeName, markerRadius);
    for (const auto& pt : pts) {
      // Sightings/Events set their own per-point color (see the Color
      // column and DataTab::chart_default_color_lookup); anything that
      // doesn't (Surfacing, still on the older BOOL Map column) falls
      // back to the tab-wide color passed in here.
      wxColour color = pt.color.IsOk() ? pt.color : fallbackColor;
      dc.SetBrush(wxBrush(color));
      dc.SetPen(wxPen(color.ChangeLightness(60), 1));
      wxPoint px;
      GetCanvasPixLL(vp, &px, pt.lat, pt.lon);
      std::vector<wxPoint> poly;
      for (const auto& v : verts) {
        poly.emplace_back(px.x + static_cast<int>(v.m_x),
                          px.y + static_cast<int>(v.m_y));
      }
      dc.DrawPolygon(static_cast<int>(poly.size()), poly.data());

      if (!pt.labelText.IsEmpty()) {
        wxSize textSize = dc.GetTextExtent(pt.labelText);
        wxPoint textPos(px.x + static_cast<int>(markerRadius) + 2,
                        px.y - textSize.GetHeight() / 2);
        dc.DrawText(pt.labelText, textPos);
      }
    }
  };

  auto tabColor = [&](const wxString& tabKey, const wxColour& fallback) {
    return m_displaySettings ? m_displaySettings->MarkerColor(tabKey, fallback)
                             : fallback;
  };
  auto tabShape = [&](const wxString& tabKey, const wxString& fallback) {
    return m_displaySettings ? m_displaySettings->MarkerShape(tabKey, fallback)
                             : fallback;
  };

  if (m_logWindow->Sightings()) {
    drawMarkers(m_logWindow->Sightings()->GetChartedPoints(),
                tabColor("Sightings", kSightingColor),
                tabShape("Sightings", "Diamond"));
  }
  if (m_logWindow->Events()) {
    drawMarkers(m_logWindow->Events()->GetChartedPoints(),
                tabColor("Events", kEventColor), tabShape("Events", "Square"));
  }
  if (m_logWindow->Surfacing()) {
    drawMarkers(m_logWindow->Surfacing()->GetChartedPoints(),
                tabColor("Surfacing", kSurfacingColor),
                tabShape("Surfacing", "Triangle"));
  }
}

void SpotterPlugin::DrawOverlayGL(PlugIn_ViewPort* vp) {
  if (!vp) return;

  double markerRadius =
      m_displaySettings ? m_displaySettings->MarkerRadius() : 14.0;
  int lineWidth = m_displaySettings ? m_displaySettings->TrackLineWidth() : 4;
  int fontSize = m_displaySettings ? m_displaySettings->LabelFontSize() : 14;
  bool trackVisible =
      m_displaySettings ? m_displaySettings->TrackVisible() : true;
  wxColour trackColor = m_displaySettings
                            ? m_displaySettings->TrackColor(kTrackColor)
                            : kTrackColor;
  bool segmentsVisible =
      m_displaySettings ? m_displaySettings->EffortSegmentsVisible() : true;
  wxColour segmentColor =
      m_displaySettings
          ? m_displaySettings->EffortSegmentColor(kEffortSegmentColor)
          : kEffortSegmentColor;

  // Legacy (fixed-function, no shaders) immediate-mode OpenGL, matching
  // the classic style OpenCPN's own codebase and most existing plugins
  // use for overlay rendering. Vertex coordinates are plain screen pixels
  // (from GetCanvasPixLL), the same coordinate space RenderOverlay's wxDC
  // path uses -- OpenCPN sets up the GL context's projection so this
  // lines up with the visible canvas.
  glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_TEXTURE_2D);

  if (m_trackRecorder && trackVisible) {
    const auto& points = m_trackRecorder->GetPoints();
    if (points.size() >= 2) {
      glColor4ub(trackColor.Red(), trackColor.Green(), trackColor.Blue(),
                 trackColor.Alpha());
      glLineWidth(static_cast<float>(lineWidth));
      glBegin(GL_LINE_STRIP);
      for (const auto& p : points) {
        wxPoint px;
        GetCanvasPixLL(vp, &px, p.lat, p.lon);
        glVertex2f(static_cast<float>(px.x), static_cast<float>(px.y));
      }
      glEnd();
    }
  }

  if (m_trackRecorder && segmentsVisible) {
    const auto& points = m_trackRecorder->GetPoints();
    glColor4ub(segmentColor.Red(), segmentColor.Green(), segmentColor.Blue(),
               segmentColor.Alpha());
    glLineWidth(static_cast<float>(lineWidth + 1));
    size_t i = 0;
    while (i < points.size()) {
      if (points[i].effortStatus != "ON" || points[i].effortSegNo.IsEmpty()) {
        i++;
        continue;
      }
      wxString segNo = points[i].effortSegNo;
      size_t start = i;
      while (i < points.size() && points[i].effortStatus == "ON" &&
             points[i].effortSegNo == segNo) {
        i++;
      }
      glBegin(GL_LINE_STRIP);
      for (size_t j = start; j < i; j++) {
        wxPoint px;
        GetCanvasPixLL(vp, &px, points[j].lat, points[j].lon);
        glVertex2f(static_cast<float>(px.x), static_cast<float>(px.y));
      }
      glEnd();
    }
  }

  if (!m_logWindow) {
    glPopAttrib();  // must stay balanced with the glPushAttrib() above,
                    // even on this early exit
    return;
  }

  wxFont labelFont(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                   wxFONTWEIGHT_BOLD);

  auto drawMarkersGL = [&](const std::vector<ChartPoint>& pts,
                           const wxColour& fallbackColor,
                           const wxString& shapeName) {
    auto verts = ShapeVertices(shapeName, markerRadius);
    for (const auto& pt : pts) {
      wxPoint px;
      GetCanvasPixLL(vp, &px, pt.lat, pt.lon);

      // Sightings/Events set their own per-point color (see the Color
      // column and DataTab::chart_default_color_lookup); anything that
      // doesn't (Surfacing, still on the older BOOL Map column) falls
      // back to the tab-wide color passed in here.
      wxColour color = pt.color.IsOk() ? pt.color : fallbackColor;
      glColor4ub(color.Red(), color.Green(), color.Blue(), 255);
      glBegin(GL_TRIANGLE_FAN);
      glVertex2f(static_cast<float>(px.x), static_cast<float>(px.y));
      for (const auto& v : verts) {
        glVertex2f(static_cast<float>(px.x + v.m_x),
                   static_cast<float>(px.y + v.m_y));
      }
      // Close the fan back to the first outline vertex.
      if (!verts.empty()) {
        glVertex2f(static_cast<float>(px.x + verts.front().m_x),
                   static_cast<float>(px.y + verts.front().m_y));
      }
      glEnd();

      if (!pt.labelText.IsEmpty()) {
        // Render the label to a small offscreen bitmap via wxDC, then
        // draw it as a textured quad. IMPORTANT: a wxMemoryDC needs a
        // valid bitmap selected into it before any drawing/measurement
        // call -- calling GetTextExtent() on a bitmap-less wxMemoryDC
        // (an earlier version of this code did exactly that, to size
        // the bitmap before creating it) is undefined behavior and was
        // crashing on marker-heavy repaints. Fixed by creating a
        // generously-sized bitmap first, then measuring/cropping via
        // that same, now-valid, DC.
        int estW = static_cast<int>(pt.labelText.length()) * (fontSize + 4) + 8;
        int estH = fontSize + 10;
        wxBitmap bmp(estW, estH, 32);
        wxMemoryDC dc(bmp);
        // Rendered onto a chroma-key color (pure magenta -- as far from
        // both black text and any realistic chart color as possible)
        // rather than solid white, so the background can be made
        // transparent below instead of showing as an opaque white box
        // behind the label (removed per direct request). wxMemoryDC
        // text rendering doesn't reliably preserve real per-pixel alpha
        // across platforms, so this chroma-key approach is used instead
        // of trying to draw with genuine alpha directly.
        const wxColour kChromaKey(255, 0, 255);
        dc.SetBackground(wxBrush(kChromaKey));
        dc.Clear();
        dc.SetFont(labelFont);
        dc.SetTextForeground(*wxBLACK);
        wxSize textSize = dc.GetTextExtent(pt.labelText);
        int w = wxMin(estW, textSize.GetWidth() + 4);
        int h = wxMin(estH, textSize.GetHeight() + 2);
        dc.DrawText(pt.labelText, 2, 1);
        dc.SelectObject(wxNullBitmap);

        wxBitmap cropped = bmp.GetSubBitmap(wxRect(0, 0, w, h));
        wxImage img = cropped.ConvertToImage();
        int iw = img.GetWidth(), ih = img.GetHeight();
        unsigned char* rgb = img.GetData();

        // Builds an RGBA buffer from the chroma-keyed RGB pixels: alpha
        // is the (normalized) distance from the chroma-key color, so
        // background pixels are fully transparent, black text pixels
        // are fully opaque, and anti-aliased edge pixels in between get
        // a smoothly graded alpha rather than a hard, jagged cutoff.
        std::vector<unsigned char> rgba(static_cast<size_t>(iw) * ih * 4);
        const double kMaxDist = std::sqrt(255.0 * 255.0 * 2.0);  // pure
                                                                 // black
                                                                 // vs
                                                                 // pure
                                                                 // magenta
        for (int p = 0; p < iw * ih; p++) {
          unsigned char r = rgb[p * 3 + 0];
          unsigned char g = rgb[p * 3 + 1];
          unsigned char b = rgb[p * 3 + 2];
          double dr = static_cast<double>(r) - kChromaKey.Red();
          double dg = static_cast<double>(g) - kChromaKey.Green();
          double db = static_cast<double>(b) - kChromaKey.Blue();
          double dist = std::sqrt(dr * dr + dg * dg + db * db);
          int alpha = static_cast<int>((dist / kMaxDist) * 255.0);
          if (alpha < 0) alpha = 0;
          if (alpha > 255) alpha = 255;
          rgba[p * 4 + 0] = r;
          rgba[p * 4 + 1] = g;
          rgba[p * 4 + 2] = b;
          rgba[p * 4 + 3] = static_cast<unsigned char>(alpha);
        }

        GLuint texId = 0;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba.data());

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4ub(255, 255, 255, 255);
        float x0 = static_cast<float>(px.x + markerRadius + 2);
        float y0 = static_cast<float>(px.y - ih / 2);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(x0, y0);
        glTexCoord2f(1, 0);
        glVertex2f(x0 + iw, y0);
        glTexCoord2f(1, 1);
        glVertex2f(x0 + iw, y0 + ih);
        glTexCoord2f(0, 1);
        glVertex2f(x0, y0 + ih);
        glEnd();
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glDeleteTextures(1, &texId);
      }
    }
  };

  auto tabColorGL = [&](const wxString& tabKey, const wxColour& fallback) {
    return m_displaySettings ? m_displaySettings->MarkerColor(tabKey, fallback)
                             : fallback;
  };
  auto tabShapeGL = [&](const wxString& tabKey, const wxString& fallback) {
    return m_displaySettings ? m_displaySettings->MarkerShape(tabKey, fallback)
                             : fallback;
  };

  if (m_logWindow->Sightings()) {
    drawMarkersGL(m_logWindow->Sightings()->GetChartedPoints(),
                  tabColorGL("Sightings", kSightingColor),
                  tabShapeGL("Sightings", "Diamond"));
  }
  if (m_logWindow->Events()) {
    drawMarkersGL(m_logWindow->Events()->GetChartedPoints(),
                  tabColorGL("Events", kEventColor),
                  tabShapeGL("Events", "Square"));
  }
  if (m_logWindow->Surfacing()) {
    drawMarkersGL(m_logWindow->Surfacing()->GetChartedPoints(),
                  tabColorGL("Surfacing", kSurfacingColor),
                  tabShapeGL("Surfacing", "Triangle"));
  }

  glPopAttrib();
}
