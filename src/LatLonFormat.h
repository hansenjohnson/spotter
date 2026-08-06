#ifndef WHALE_LATLON_FORMAT_H
#define WHALE_LATLON_FORMAT_H

#include <wx/wx.h>

// Controls how latitude/longitude values are *displayed* throughout the
// plugin (grid cells, the status bar's vessel position). The underlying
// stored value -- in the CSV files, and in the grid's raw cell data --
// is always plain decimal degrees, regardless of this setting; only the
// on-screen text changes. A single global setting (not per-tab) so every
// part of the UI stays in sync, since a lat/lon column can be edited or
// viewed from multiple tabs.
//
// This intentionally isn't a class/singleton with instance state --
// there's only ever one of these settings active in a given OpenCPN
// process, and plumbing an object reference through GenericGridTable
// (which needs to consult this on every cell render) would add
// significant complexity for no real benefit here.
namespace LatLonFormat {

enum class Format {
  DecimalDegrees,
  DegreesDecimalMinutes,  // default
  DegreesMinutesSeconds,
};

Format Get();
void Set(Format format);

// Advances to the next format in the cycle (wrapping around), and
// returns the new format -- used by the format-toggle button.
Format CycleToNext();

// A short label for the *current* format, suitable for a toggle
// button's caption (e.g. "Lat/Lon: DDM").
wxString CurrentLabel();

// Loads/saves the current format as a single word in a plain text file
// (latlon_format.txt) in the plugin's data directory.
void LoadFromFile(const wxString& path);
void SaveToFile(const wxString& path);

// Formats a decimal-degrees value for display, per the *current* global
// format. `isLatitude` selects the N/S vs E/W suffix and the 2-digit
// (vs 3-digit) degrees field.
wxString FormatValue(double decimalDegrees, bool isLatitude);

// Parses user-typed text back into decimal degrees. Deliberately
// tolerant of format details (degree/minute/second symbols, spacing,
// hemisphere letter case) rather than requiring an exact match to the
// current display format -- it just extracts up to 3 numbers in order
// (degrees[, minutes[, seconds]]) plus an optional hemisphere letter
// (N/S/E/W) or leading '-', and combines them. Returns false (leaving
// *outDecimalDegrees* untouched) if the text can't be parsed at all.
bool ParseValue(const wxString& text, bool isLatitude,
                double* outDecimalDegrees);

}  // namespace LatLonFormat

#endif  // WHALE_LATLON_FORMAT_H
