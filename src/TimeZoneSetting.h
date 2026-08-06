#ifndef SPOTTER_TIME_ZONE_SETTING_H
#define SPOTTER_TIME_ZONE_SETTING_H

#include <wx/wx.h>
#include <vector>

// An explicit, plugin-level timezone override for recorded timestamps
// -- added per direct request, since relying on the computer's own
// configured timezone (this plugin's previous behavior, and still the
// default here -- see "System Default" below) can get messy if a
// survey spans multiple timezones and the OS's timezone changes
// mid-survey (some systems auto-adjust it based on location).
//
// A small, fixed set of named zones (not the full IANA timezone
// database) -- this plugin's own domain is Atlantic-coast whale
// surveys, so the zones offered are the ones actually relevant there,
// plus the other continental US zones since including them costs
// almost nothing once the DST-computation logic already exists for
// one. All US/Canada zones observe daylight saving time on the same
// schedule (2nd Sunday of March to 1st Sunday of November, in effect
// since the Energy Policy Act of 2005 -- stable for two decades and
// not expected to change), computed here directly rather than via the
// platform's own timezone database, so the result doesn't depend on
// the computer's own timezone data being present/current and doesn't
// depend on the computer's own configured zone at all.
class TimeZoneSetting {
public:
  struct Zone {
    wxString name;       // shown in the dropdown, e.g. "Eastern Time"
    wxString stdAbbrev;  // e.g. "EST"
    wxString dstAbbrev;  // e.g. "EDT" -- same as stdAbbrev if
                         // observesDst is false
    int stdOffsetHours;  // hours from UTC during standard time, e.g.
                         // -5 for Eastern
    bool observesDst;
  };

  // "System Default" first (index 0) -- falls back to this plugin's
  // original behavior (the computer's own configured timezone), kept
  // as the default so existing behavior doesn't change unless a zone
  // is explicitly chosen.
  static const std::vector<Zone>& AllZones();

  // Index into AllZones() -- 0 (System Default) unless explicitly set.
  static int Get();
  static void Set(int zoneIndex);
  static void LoadFromFile(const wxString& path);
  static void SaveToFile(const wxString& path);

  // Formats a UTC-based instant as "YYYY-MM-DD HH:MM:SS ZZZ" in
  // whichever zone is currently selected (System Default: the
  // computer's own local time and abbreviation, via wxDateTime's own
  // %Z; a specific zone: that zone's wall-clock time and abbreviation,
  // computed directly here).
  static wxString FormatInSelectedZone(const wxDateTime& utcTime);
};

#endif  // SPOTTER_TIME_ZONE_SETTING_H
