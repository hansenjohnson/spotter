#include "TimeZoneSetting.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/datetime.h>

namespace {

int g_zoneIndex = 0;  // "System Default"

bool IsUsCanadaDst(const wxDateTime& approxLocalTime) {
  // 2nd Sunday of March through 1st Sunday of November, both at 2am
  // local -- the rule in effect since 2007 (Energy Policy Act of 2005)
  // for every US/Canada zone offered here. approxLocalTime only needs
  // to be correct to within a few hours for this comparison to land on
  // the right day, which a fixed standard-time offset always is (the
  // ambiguity DST itself introduces only matters within the one-hour
  // window right at each transition, not for this day-level check).
  int year = approxLocalTime.GetYear();

  wxDateTime dstStart(1, wxDateTime::Mar, year);
  while (dstStart.GetWeekDay() != wxDateTime::Sun) {
    dstStart += wxDateSpan::Day();
  }
  dstStart += wxDateSpan::Week();  // 1st Sunday -> 2nd Sunday
  dstStart.SetHour(2);
  dstStart.SetMinute(0);
  dstStart.SetSecond(0);

  wxDateTime dstEnd(1, wxDateTime::Nov, year);
  while (dstEnd.GetWeekDay() != wxDateTime::Sun) {
    dstEnd += wxDateSpan::Day();
  }
  dstEnd.SetHour(2);
  dstEnd.SetMinute(0);
  dstEnd.SetSecond(0);

  return approxLocalTime >= dstStart && approxLocalTime < dstEnd;
}

}  // namespace

const std::vector<TimeZoneSetting::Zone>& TimeZoneSetting::AllZones() {
  static const std::vector<Zone> zones = {
      {"System Default", "", "", 0, false},
      {"UTC", "UTC", "UTC", 0, false},
      {"Eastern Time", "EST", "EDT", -5, true},
      {"Atlantic Time", "AST", "ADT", -4, true},
      {"Central Time", "CST", "CDT", -6, true},
      {"Mountain Time", "MST", "MDT", -7, true},
      {"Pacific Time", "PST", "PDT", -8, true},
  };
  return zones;
}

int TimeZoneSetting::Get() { return g_zoneIndex; }

void TimeZoneSetting::Set(int zoneIndex) {
  if (zoneIndex >= 0 && zoneIndex < static_cast<int>(AllZones().size())) {
    g_zoneIndex = zoneIndex;
  }
}

void TimeZoneSetting::LoadFromFile(const wxString& path) {
  if (!wxFileExists(path)) return;
  wxFile f(path);
  if (!f.IsOpened()) return;
  wxString contents;
  f.ReadAll(&contents);
  contents.Trim(true).Trim(false);
  const auto& zones = AllZones();
  for (size_t i = 0; i < zones.size(); i++) {
    if (zones[i].name == contents) {
      g_zoneIndex = static_cast<int>(i);
      return;
    }
  }
}

void TimeZoneSetting::SaveToFile(const wxString& path) {
  wxFile f;
  if (f.Create(path, true)) {
    f.Write(AllZones()[g_zoneIndex].name);
    f.Close();
  }
}

wxString TimeZoneSetting::FormatInSelectedZone(const wxDateTime& utcTime) {
  const Zone& zone = AllZones()[g_zoneIndex];
  if (g_zoneIndex == 0) {
    // "System Default" -- exactly this plugin's original behavior
    // (the computer's own configured timezone, via wxDateTime's own
    // %Z), unchanged unless a specific zone is explicitly selected.
    return utcTime.Format("%Y-%m-%d %H:%M:%S %Z");
  }

  // Approximate wall-clock time in this zone (a fixed standard-time
  // offset applied to the underlying instant) -- only needs to land on
  // the correct day for the DST-window check below, so the one-hour
  // uncertainty a real DST offset would introduce here doesn't matter.
  wxDateTime approxLocal = utcTime;
  approxLocal += wxTimeSpan::Hours(zone.stdOffsetHours);
  bool dst = zone.observesDst && IsUsCanadaDst(approxLocal);
  int offsetHours = zone.stdOffsetHours + (dst ? 1 : 0);

  // Formats the *original* instant directly in this explicit,
  // fixed-offset timezone -- not a mutated copy, so there's no risk of
  // double-applying an offset the way there would be if this reused
  // approxLocal (which already has the standard, not DST-adjusted,
  // offset baked in).
  wxDateTime::TimeZone tz(offsetHours * 3600);
  wxString abbrev = dst ? zone.dstAbbrev : zone.stdAbbrev;
  return utcTime.Format("%Y-%m-%d %H:%M:%S", tz) + " " + abbrev;
}
