#ifndef WHALE_DISPLAY_SETTINGS_H
#define WHALE_DISPLAY_SETTINGS_H

#include <wx/wx.h>
#include <map>

// Loads chart-overlay and grid display sizes (marker radius, label font
// size, trackline width, grid text size) plus per-tab marker shape/color
// choices from a plain external CSV file (display.csv) in the plugin's
// data directory: two columns, key and value, one setting per row.
// Created with sensible (larger-than-original-default) defaults on first
// run, editable without recompiling.
class DisplaySettings {
public:
  explicit DisplaySettings(const wxString& dataDir);

  double MarkerRadius() const { return m_markerRadius; }
  int LabelFontSize() const { return m_labelFontSize; }
  int TrackLineWidth() const { return m_trackLineWidth; }
  // Point size for every tab's grid text (cell contents and column
  // headers) -- distinct from LabelFontSize, which is the size of the
  // Sighting # text label drawn *on the chart* next to a marker.
  int GridFontSize() const { return m_gridFontSize; }
  // Point size for everything else in the window -- status bar labels,
  // buttons, tab titles, links, Settings tab text. Kept separate from
  // GridFontSize since table data and surrounding UI chrome are often
  // wanted at different sizes (e.g. a bigger grid for readability at a
  // glance, without also blowing up every button and label).
  int UiFontSize() const { return m_uiFontSize; }

  // Per-tab marker appearance for the chart overlay, keyed by tab title
  // ("Sightings", "Events", "Surfacing"). Falls back to a reasonable
  // built-in default (varies per tab, to keep them visually distinct)
  // if never customized. Shape is one of "Diamond", "Square",
  // "Triangle", "Circle", "Star". Both setters immediately rewrite the
  // file.
  wxString MarkerShape(const wxString& tabKey, const wxString& fallback) const;
  wxColour MarkerColor(const wxString& tabKey, const wxColour& fallback) const;
  void SetMarkerShape(const wxString& tabKey, const wxString& shape);
  void SetMarkerColor(const wxString& tabKey, const wxColour& color);

  wxString GetPath() const { return m_path; }

  // Trackline and effort-segment overlay appearance, set from the
  // Settings tab's Tracking/Effort sections respectively.
  wxColour TrackColor(const wxColour& fallback) const;
  void SetTrackColor(const wxColour& color);
  bool TrackVisible() const { return m_trackVisible; }
  void SetTrackVisible(bool visible);

  wxColour EffortSegmentColor(const wxColour& fallback) const;
  void SetEffortSegmentColor(const wxColour& color);
  bool EffortSegmentsVisible() const { return m_effortSegmentsVisible; }
  void SetEffortSegmentsVisible(bool visible);

  // Which Sightings columns to concatenate (space-separated, in the
  // order given) for the text label drawn next to each marker on the
  // chart -- e.g. Species + FieldID shows the Species value and FieldID
  // value separated by a space (however long the Species entry itself
  // is -- this doesn't abbreviate it). Stored as a comma-separated list
  // of column names; SetSightingsLabelColumns() takes the same format.
  wxArrayString SightingsLabelColumns() const;
  void SetSightingsLabelColumns(const wxArrayString& columns);

  // Same idea as SightingsLabelColumns(), for the Events tab's map
  // label -- defaults to Event + ID.
  wxArrayString EventsLabelColumns() const;
  void SetEventsLabelColumns(const wxArrayString& columns);

private:
  void CreateDefaults();
  void Load();
  void Save() const;

  wxString m_path;
  double m_markerRadius;
  int m_labelFontSize;
  int m_trackLineWidth;
  int m_gridFontSize;
  int m_uiFontSize;
  bool m_trackVisible = true;
  bool m_effortSegmentsVisible = true;
  std::map<wxString, wxString> m_values;  // per-tab marker shape/color,
                                          // and anything else added
                                          // later that doesn't need
                                          // its own dedicated field
};

#endif  // WHALE_DISPLAY_SETTINGS_H
