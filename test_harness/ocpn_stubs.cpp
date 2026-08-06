// Minimal stand-in implementations of the small set of OpenCPN host
// functions that spotter_pi.cpp / SightingDialog.cpp / DataLogger.cpp
// call. When the plugin is loaded inside real OpenCPN, OpenCPN itself
// provides these symbols; here we fake them just well enough to exercise
// the plugin's own UI and logging code in isolation, on your desktop,
// without installing OpenCPN first.
//
// This file is ONLY used by the test harness executable. It is never
// linked into the real spotter_pi plugin library.

#include "ocpn_plugin.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <cstdio>

extern "C" DECL_EXP int InsertPlugInTool(wxString label, wxBitmap* bitmap,
                                         wxBitmap* bmpRollover, wxItemKind kind,
                                         wxString shortHelp, wxString longHelp,
                                         wxObject* clientData, int position,
                                         int tool_sel,
                                         opencpn_plugin* pplugin) {
  static int nextId = 1000;
  wxUnusedVar(bitmap);
  wxUnusedVar(bmpRollover);
  wxUnusedVar(kind);
  wxUnusedVar(longHelp);
  wxUnusedVar(clientData);
  wxUnusedVar(position);
  wxUnusedVar(tool_sel);
  wxUnusedVar(pplugin);
  printf("[stub] InsertPlugInTool(\"%s\", help=\"%s\") -> id %d\n",
         (const char*)label.mb_str(), (const char*)shortHelp.mb_str(), nextId);
  return nextId++;
}

extern "C" DECL_EXP void RemovePlugInTool(int tool_id) {
  printf("[stub] RemovePlugInTool(%d)\n", tool_id);
}

extern "C" DECL_EXP void SetToolbarItemState(int item, bool toggle) {
  printf("[stub] SetToolbarItemState(%d, %s)\n", item,
         toggle ? "true" : "false");
}

extern "C" DECL_EXP void SetToolbarToolBitmaps(int item, wxBitmap* bitmap,
                                               wxBitmap* bmpRollover) {
  wxUnusedVar(bitmap);
  wxUnusedVar(bmpRollover);
  printf("[stub] SetToolbarToolBitmaps(%d)\n", item);
}

extern "C" DECL_EXP wxWindow* GetOCPNCanvasWindow() { return nullptr; }

extern "C" DECL_EXP void RequestRefresh(wxWindow* win) {
  wxUnusedVar(win);
  printf("[stub] RequestRefresh()\n");
}

extern "C" DECL_EXP void GetCanvasPixLL(PlugIn_ViewPort* vp, wxPoint* pp,
                                        double lat, double lon) {
  // Simple flat-earth approximation, good enough to exercise the overlay
  // drawing code in the test harness -- the real OpenCPN implementation
  // used at runtime handles the chart's actual projection/pan/zoom.
  if (!vp || !pp) return;
  double cosLat = cos(vp->clat * M_PI / 180.0);
  pp->x = static_cast<int>(vp->pix_width / 2.0 +
                           (lon - vp->clon) * vp->view_scale_ppm * cosLat);
  pp->y = static_cast<int>(vp->pix_height / 2.0 -
                           (lat - vp->clat) * vp->view_scale_ppm);
}

extern "C" DECL_EXP void PositionBearingDistanceMercator_Plugin(
    double lat, double lon, double brg, double dist, double* dlat,
    double* dlon) {
  // Simple flat-earth approximation -- good enough for local UI testing.
  // (The real OpenCPN implementation used at runtime is exact Mercator.)
  const double nm_per_deg_lat = 60.0;
  double rad = brg * M_PI / 180.0;
  double dlat_deg = (dist * cos(rad)) / nm_per_deg_lat;
  double nm_per_deg_lon = nm_per_deg_lat * cos(lat * M_PI / 180.0);
  double dlon_deg =
      nm_per_deg_lon > 1e-6 ? (dist * sin(rad)) / nm_per_deg_lon : 0.0;
  *dlat = lat + dlat_deg;
  *dlon = lon + dlon_deg;
}

extern "C" DECL_EXP void DistanceBearingMercator_Plugin(
    double lat0, double lon0, double lat1, double lon1, double* brg,
    double* dist) {
  // Same flat-earth approximation as above, in reverse -- good enough
  // for local UI testing (the real OpenCPN implementation is exact
  // Mercator).
  const double nm_per_deg_lat = 60.0;
  double dlat_nm = (lat1 - lat0) * nm_per_deg_lat;
  double nm_per_deg_lon = nm_per_deg_lat * cos(lat0 * M_PI / 180.0);
  double dlon_nm = (lon1 - lon0) * nm_per_deg_lon;
  *dist = sqrt(dlat_nm * dlat_nm + dlon_nm * dlon_nm);
  if (brg) {
    *brg = atan2(dlon_nm, dlat_nm) * 180.0 / M_PI;
    if (*brg < 0) *brg += 360.0;
  }
}

extern "C" DECL_EXP wxString* GetpPrivateApplicationDataLocation() {
  static wxString dir;
  wxFileName fn(wxStandardPaths::Get().GetUserDataDir(), "");
  fn.AppendDir("spotter_pi_test");
  dir = fn.GetPath();
  return &dir;
}
