#ifndef WHALE_CSV_UTILS_H
#define WHALE_CSV_UTILS_H

#include <wx/wx.h>
#include <vector>

// Minimal RFC4180-ish CSV read/write: handles quoted fields containing
// commas, quotes (doubled) and embedded newlines. Good enough for the
// data this plugin generates and edits; not a general-purpose parser.
namespace CsvUtils {

// Reads every row (including the header row, at index 0) of a CSV file.
// Returns an empty vector if the file does not exist or can't be read.
std::vector<std::vector<wxString>> ReadAll(const wxString& path);

// Overwrites `path` with `header` followed by `rows`. Returns false if
// the file could not be written.
bool WriteAll(const wxString& path, const std::vector<wxString>& header,
              const std::vector<std::vector<wxString>>& rows);

// Quotes/escapes a single field for CSV output.
wxString EscapeField(const wxString& field);

}  // namespace CsvUtils

#endif  // WHALE_CSV_UTILS_H
