// Default (no-op / trivial) implementations for opencpn_plugin base-class
// virtual methods that SpotterPlugin does not itself override.
//
// Why this file exists: OpenCPN's plugin API header (ocpn_plugin.h)
// declares these as ordinary (non-inline, non-pure) virtual methods, on
// the assumption that OpenCPN's own executable will export them and a
// plugin's dylib/so can simply borrow them at dlopen() time for any
// virtual it doesn't override -- Unix shared libraries are normally
// allowed to have such symbols left unresolved until load time.
//
// In practice, this OpenCPN build does not export these particular
// symbols (confirmed via `dlopen`: "symbol not found in flat namespace
// '__ZN14opencpn_plugin11SetDefaultsEv'"). Other bundled plugins (e.g.
// dashboard_pi) never hit this because they override nearly every
// virtual themselves, so their vtables never need to borrow a base-class
// default from the host. Since our plugin deliberately stays minimal and
// leaves many virtuals un-overridden, we supply our own trivial bodies
// here instead -- compiled directly into libspotter_pi, so
// nothing needs to be resolved from the host process at all. This keeps
// the plugin working regardless of what any given OpenCPN build happens
// to export.

#include "ocpn_plugin.h"

// ---- opencpn_plugin (base) --------------------------------------------
opencpn_plugin::~opencpn_plugin() {}
int opencpn_plugin::Init(void) { return 0; }
bool opencpn_plugin::DeInit(void) { return true; }
int opencpn_plugin::GetAPIVersionMajor() { return API_VERSION_MAJOR; }
int opencpn_plugin::GetAPIVersionMinor() { return API_VERSION_MINOR; }
int opencpn_plugin::GetPlugInVersionMajor() { return 1; }
int opencpn_plugin::GetPlugInVersionMinor() { return 0; }
wxBitmap* opencpn_plugin::GetPlugInBitmap() { return nullptr; }
wxString opencpn_plugin::GetCommonName() { return wxEmptyString; }
wxString opencpn_plugin::GetShortDescription() { return wxEmptyString; }
wxString opencpn_plugin::GetLongDescription() { return wxEmptyString; }
void opencpn_plugin::SetDefaults(void) {}
int opencpn_plugin::GetToolbarToolCount(void) { return 0; }
int opencpn_plugin::GetToolboxPanelCount(void) { return 0; }
void opencpn_plugin::SetupToolboxPanel(int, wxNotebook*) {}
void opencpn_plugin::OnCloseToolboxPanel(int, int) {}
void opencpn_plugin::ShowPreferencesDialog(wxWindow*) {}
bool opencpn_plugin::RenderOverlay(wxMemoryDC*, PlugIn_ViewPort*) {
  return false;
}
void opencpn_plugin::SetCursorLatLon(double, double) {}
void opencpn_plugin::SetCurrentViewPort(PlugIn_ViewPort&) {}
void opencpn_plugin::SetPositionFix(PlugIn_Position_Fix&) {}
void opencpn_plugin::SetNMEASentence(wxString&) {}
void opencpn_plugin::SetAISSentence(wxString&) {}
void opencpn_plugin::ProcessParentResize(int, int) {}
void opencpn_plugin::SetColorScheme(PI_ColorScheme) {}
void opencpn_plugin::OnToolbarToolCallback(int) {}
void opencpn_plugin::OnContextMenuItemCallback(int) {}
void opencpn_plugin::UpdateAuiStatus(void) {}
wxArrayString opencpn_plugin::GetDynamicChartClassNameArray(void) {
  return wxArrayString();
}

// ---- opencpn_plugin_16 --------------------------------------------------
opencpn_plugin_16::opencpn_plugin_16(void* pmgr) : opencpn_plugin(pmgr) {}
opencpn_plugin_16::~opencpn_plugin_16() {}
bool opencpn_plugin_16::RenderOverlay(wxDC&, PlugIn_ViewPort*) { return false; }
void opencpn_plugin_16::SetPluginMessage(wxString&, wxString&) {}

// ---- opencpn_plugin_17 --------------------------------------------------
opencpn_plugin_17::opencpn_plugin_17(void* pmgr) : opencpn_plugin(pmgr) {}
opencpn_plugin_17::~opencpn_plugin_17() {}
bool opencpn_plugin_17::RenderOverlay(wxDC&, PlugIn_ViewPort*) { return false; }
bool opencpn_plugin_17::RenderGLOverlay(wxGLContext*, PlugIn_ViewPort*) {
  return false;
}
void opencpn_plugin_17::SetPluginMessage(wxString&, wxString&) {}

// ---- opencpn_plugin_18 --------------------------------------------------
opencpn_plugin_18::opencpn_plugin_18(void* pmgr) : opencpn_plugin(pmgr) {}
opencpn_plugin_18::~opencpn_plugin_18() {}
bool opencpn_plugin_18::RenderOverlay(wxDC&, PlugIn_ViewPort*) { return false; }
bool opencpn_plugin_18::RenderGLOverlay(wxGLContext*, PlugIn_ViewPort*) {
  return false;
}
void opencpn_plugin_18::SetPluginMessage(wxString&, wxString&) {}
void opencpn_plugin_18::SetPositionFixEx(PlugIn_Position_Fix_Ex&) {}

// ---- opencpn_plugin_19 ----------------------------------------------------
opencpn_plugin_19::opencpn_plugin_19(void* pmgr) : opencpn_plugin_18(pmgr) {}
opencpn_plugin_19::~opencpn_plugin_19() {}
void opencpn_plugin_19::OnSetupOptions(void) {}

// ---- opencpn_plugin_110 ---------------------------------------------------
opencpn_plugin_110::opencpn_plugin_110(void* pmgr) : opencpn_plugin_19(pmgr) {}
opencpn_plugin_110::~opencpn_plugin_110() {}
void opencpn_plugin_110::LateInit(void) {}

// ---- opencpn_plugin_111 ---------------------------------------------------
opencpn_plugin_111::opencpn_plugin_111(void* pmgr) : opencpn_plugin_110(pmgr) {}
opencpn_plugin_111::~opencpn_plugin_111() {}

// ---- opencpn_plugin_112 ---------------------------------------------------
opencpn_plugin_112::opencpn_plugin_112(void* pmgr) : opencpn_plugin_111(pmgr) {}
opencpn_plugin_112::~opencpn_plugin_112() {}
bool opencpn_plugin_112::MouseEventHook(wxMouseEvent&) { return false; }
void opencpn_plugin_112::SendVectorChartObjectInfo(wxString&, wxString&,
                                                   wxString&, double, double,
                                                   double, int) {}

// ---- opencpn_plugin_113 ---------------------------------------------------
opencpn_plugin_113::opencpn_plugin_113(void* pmgr) : opencpn_plugin_112(pmgr) {}
opencpn_plugin_113::~opencpn_plugin_113() {}
bool opencpn_plugin_113::KeyboardEventHook(wxKeyEvent&) { return false; }
void opencpn_plugin_113::OnToolbarToolDownCallback(int) {}
void opencpn_plugin_113::OnToolbarToolUpCallback(int) {}

// ---- opencpn_plugin_114 ---------------------------------------------------
opencpn_plugin_114::opencpn_plugin_114(void* pmgr) : opencpn_plugin_113(pmgr) {}
opencpn_plugin_114::~opencpn_plugin_114() {}

// ---- opencpn_plugin_115 ---------------------------------------------------
opencpn_plugin_115::opencpn_plugin_115(void* pmgr) : opencpn_plugin_114(pmgr) {}
opencpn_plugin_115::~opencpn_plugin_115() {}

// ---- opencpn_plugin_116 ---------------------------------------------------
opencpn_plugin_116::opencpn_plugin_116(void* pmgr) : opencpn_plugin_115(pmgr) {}
opencpn_plugin_116::~opencpn_plugin_116() {}
bool opencpn_plugin_116::RenderGLOverlayMultiCanvas(wxGLContext*,
                                                    PlugIn_ViewPort*, int) {
  return false;
}
bool opencpn_plugin_116::RenderOverlayMultiCanvas(wxDC&, PlugIn_ViewPort*,
                                                  int) {
  return false;
}
void opencpn_plugin_116::PrepareContextMenu(int) {}

// ---- opencpn_plugin_117 ---------------------------------------------------
opencpn_plugin_117::opencpn_plugin_117(void* pmgr) : opencpn_plugin_116(pmgr) {}
int opencpn_plugin_117::GetPlugInVersionPatch() { return 0; }
int opencpn_plugin_117::GetPlugInVersionPost() { return 0; }
const char* opencpn_plugin_117::GetPlugInVersionPre() { return ""; }
const char* opencpn_plugin_117::GetPlugInVersionBuild() { return ""; }
void opencpn_plugin_117::SetActiveLegInfo(Plugin_Active_Leg_Info&) {}

// ---- opencpn_plugin_118 ---------------------------------------------------
opencpn_plugin_118::opencpn_plugin_118(void* pmgr) : opencpn_plugin_117(pmgr) {}
bool opencpn_plugin_118::RenderGLOverlayMultiCanvas(wxGLContext*,
                                                    PlugIn_ViewPort*, int,
                                                    int) {
  return false;
}
bool opencpn_plugin_118::RenderOverlayMultiCanvas(wxDC&, PlugIn_ViewPort*, int,
                                                  int) {
  return false;
}
