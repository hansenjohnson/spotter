#include "ColumnDefinitions.h"
#include "CsvUtils.h"

#include <wx/filename.h>

namespace {
struct DefEntry {
  const char* tab;
  const char* column;
  const char* definition;
};

const DefEntry kDefaults[] = {
    // --- Sightings ---
    {"Sightings", "SightNo",
     "Auto-numbered ID for this sighting. Used to cross-reference "
     "Surfacings rows for the same animal(s)."},
    {"Sightings", "Map",
     "Whether this row is drawn as a marker on the chart. Its color "
     "comes from the color configured for this Species in "
     "species.csv."},
    {"Sightings", "Time", "Local time this row was logged."},
    {"Sightings", "Lat",
     "Vessel latitude (decimal degrees) at the time of the sighting."},
    {"Sightings", "Lon",
     "Vessel longitude (decimal degrees) at the time of the sighting."},
    {"Sightings", "Species", "Species identification, or best guess."},
    {"Sightings", "Num", "Number of animals in the group."},
    {"Sightings", "NumConf",
     "Confidence in the Num count: Definite, Probable, Possible, or At "
     "Least (a known minimum/undercount)."},
    {"Sightings", "NumCalf", "Number of calves in the group, if any."},
    {"Sightings", "SpecConf",
     "Confidence in the species identification: Definite, Probable, or "
     "Possible."},
    {"Sightings", "AnHead",
     "Animal heading -- direction the animal was traveling, in degrees "
     "magnetic (0-360)."},
    {"Sightings", "BearingMag",
     "Magnetic compass bearing from the vessel to the sighting."},
    {"Sightings", "Dist",
     "Distance from the vessel to the sighting, in DistUnit."},
    {"Sightings", "DistUnit",
     "Unit for Dist: m (meters), nm (nautical miles), or reticles "
     "(binocular reticle marks below the horizon -- uses the current "
     "observer height from the Effort tab's Position)."},
    {"Sightings", "SigLat",
     "Computed latitude of the sighting itself (vessel position + "
     "bearing/distance), used to place its marker on the chart."},
    {"Sightings", "SigLon",
     "Computed longitude of the sighting itself (vessel position + "
     "bearing/distance), used to place its marker on the chart."},
    {"Sightings", "Behavs",
     "Observed behaviors (multi-select -- double-click, Enter, or "
     "Space to choose more than one)."},
    {"Sightings", "Obs", "Observer who logged this sighting."},
    {"Sightings", "Img",
     "Whether photos and/or video were collected of this sighting."},
    {"Sightings", "FieldID",
     "Field identifier assigned to this individual animal in the field "
     "(e.g. A, B, C), if any."},
    {"Sightings", "Notes", "Freeform notes about this sighting."},

    // --- Effort ---
    {"Effort", "EffortNo",
     "Auto-numbered ID for this Effort row -- an easy stable "
     "reference number, the same idea as SightNo/EventNo."},
    {"Effort", "Time", "Local time this row was logged."},
    {"Effort", "Lat",
     "Vessel latitude (decimal degrees) at the time of this check."},
    {"Effort", "Lon",
     "Vessel longitude (decimal degrees) at the time of this check."},
    {"Effort", "Effort",
     "Whether the vessel is actively on survey effort: ON or OFF."},
    {"Effort", "SegNo",
     "Effort segment number -- blank while Effort is OFF, and "
     "increases by one each time Effort switches from OFF back to ON "
     "(computed automatically, not editable)."},
    {"Effort", "Position",
     "Observer's location on the vessel (e.g. Wheelhouse, Topdeck, "
     "Bow), each with its own associated eye height -- see the "
     "\"Edit observer positions/heights\" link on the Settings tab."},
    {"Effort", "Vis", "Visibility, in nautical miles."},
    {"Effort", "Beaufort", "Sea state on the Beaufort scale (1-9)."},
    {"Effort", "Weather", "General weather conditions."},
    {"Effort", "Glare",
     "Severity of glare affecting observation: Mild, Moderate, or Severe."},
    {"Effort", "GlareBegin", "Magnetic bearing where the glare sector begins."},
    {"Effort", "GlareEnd", "Magnetic bearing where the glare sector ends."},
    {"Effort", "Port", "Observer stationed to port."},
    {"Effort", "Recorder", "Observer recording data."},
    {"Effort", "Starboard", "Observer stationed to starboard."},
    {"Effort", "Notes", "Freeform notes about conditions at this check."},

    // --- Events ---
    {"Events", "Map",
     "Whether this row is drawn as a marker on the chart. Its color "
     "comes from the color configured for this Event in "
     "event_types.csv."},
    {"Events", "EventNo",
     "Auto-numbered ID for this Events row -- an easy stable "
     "reference number, the same idea as SightNo/EffortNo."},
    {"Events", "Time", "Local time this row was logged."},
    {"Events", "Lat",
     "Vessel latitude (decimal degrees) at the time of the event."},
    {"Events", "Lon",
     "Vessel longitude (decimal degrees) at the time of the event."},
    {"Events", "Event",
     "Type of event (CTD cast, drifter deployment, drone flight, "
     "tagging, biopsy sample, acoustic recorder deployment/recovery, "
     "etc.)."},
    {"Events", "ID", "Identifier or short description for this event."},
    {"Events", "Notes", "Freeform notes about this event."},

    // --- Surfacings ---
    {"Surfacings", "SightNo",
     "The Sightings row this surfacing belongs to -- shared "
     "automatically when the Sightings row was first added (see the "
     "Sightings tab)."},
    {"Surfacings", "Map",
     "Whether this row is drawn as a marker on the chart."},
    {"Surfacings", "Time", "Local time this row was logged."},
    {"Surfacings", "Lat",
     "Vessel latitude (decimal degrees) at the time of this surfacing."},
    {"Surfacings", "Lon",
     "Vessel longitude (decimal degrees) at the time of this surfacing."},
    {"Surfacings", "BearingMag",
     "Magnetic compass bearing from the vessel to the surfacing."},
    {"Surfacings", "Dist",
     "Distance from the vessel to the surfacing, in DistUnit."},
    {"Surfacings", "DistUnit",
     "Unit for Dist: m (meters), nm (nautical miles), or reticles "
     "(binocular reticle marks below the horizon -- uses the current "
     "observer height from the Effort tab's Position)."},
    {"Surfacings", "SurfLat",
     "Computed latitude of the surfacing itself (vessel position + "
     "bearing/distance), used to place its marker on the chart."},
    {"Surfacings", "SurfLon",
     "Computed longitude of the surfacing itself (vessel position + "
     "bearing/distance), used to place its marker on the chart."},
    {"Surfacings", "Event",
     "Type of surfacing: Surfacing, First surfacing, or Fluking."},
};
}  // namespace

ColumnDefinitions::ColumnDefinitions(const wxString& dataDir) {
  wxFileName fn(dataDir, "column_definitions.csv");
  m_path = fn.GetFullPath();

  if (!wxFileExists(m_path)) {
    CreateDefaults();
  }
  Load();
}

void ColumnDefinitions::CreateDefaults() {
  std::vector<wxString> header = {"tab", "column", "definition"};
  std::vector<std::vector<wxString>> rows;
  for (const auto& d : kDefaults) {
    rows.push_back(
        {wxString(d.tab), wxString(d.column), wxString(d.definition)});
  }
  CsvUtils::WriteAll(m_path, header, rows);
}

void ColumnDefinitions::Load() {
  auto rows = CsvUtils::ReadAll(m_path);
  for (size_t i = 1; i < rows.size(); i++) {  // skip header row
    if (rows[i].size() < 3) continue;
    wxString tab = rows[i][0];
    wxString column = rows[i][1];
    wxString definition = rows[i][2];
    if (tab.IsEmpty() || column.IsEmpty()) continue;
    m_definitions[{tab, column}] = definition;
  }
}

wxString ColumnDefinitions::GetDefinition(const wxString& tab,
                                          const wxString& column) const {
  auto it = m_definitions.find({tab, column});
  if (it == m_definitions.end()) return wxEmptyString;
  return it->second;
}
