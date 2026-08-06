#include "LatLonFormat.h"
#include "CsvUtils.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/tokenzr.h>
#include <cmath>
#include <vector>

namespace LatLonFormat {

namespace {
Format g_current = Format::DegreesDecimalMinutes;  // default, per spec
}

Format Get() { return g_current; }
void Set(Format format) { g_current = format; }

Format CycleToNext() {
  switch (g_current) {
    case Format::DegreesDecimalMinutes:
      g_current = Format::DecimalDegrees;
      break;
    case Format::DecimalDegrees:
      g_current = Format::DegreesMinutesSeconds;
      break;
    case Format::DegreesMinutesSeconds:
    default:
      g_current = Format::DegreesDecimalMinutes;
      break;
  }
  return g_current;
}

wxString CurrentLabel() {
  switch (g_current) {
    case Format::DecimalDegrees:
      return "Lat/Lon: DD";
    case Format::DegreesMinutesSeconds:
      return "Lat/Lon: DMS";
    case Format::DegreesDecimalMinutes:
    default:
      return "Lat/Lon: DDM";
  }
}

void LoadFromFile(const wxString& path) {
  if (!wxFileExists(path)) return;
  wxFile f(path);
  if (!f.IsOpened()) return;
  wxString contents;
  f.ReadAll(&contents);
  contents.Trim(true).Trim(false);
  if (contents == "DD") {
    g_current = Format::DecimalDegrees;
  } else if (contents == "DMS") {
    g_current = Format::DegreesMinutesSeconds;
  } else if (contents == "DDM") {
    g_current = Format::DegreesDecimalMinutes;
  }
  // Any other/garbled content is left as whatever the current in-memory
  // default already is, rather than treated as an error.
}

void SaveToFile(const wxString& path) {
  wxString code;
  switch (g_current) {
    case Format::DecimalDegrees:
      code = "DD";
      break;
    case Format::DegreesMinutesSeconds:
      code = "DMS";
      break;
    case Format::DegreesDecimalMinutes:
    default:
      code = "DDM";
      break;
  }
  wxFile f;
  if (f.Create(path, true)) {
    f.Write(code);
    f.Close();
  }
}

wxString FormatValue(double decimalDegrees, bool isLatitude) {
  bool negative = decimalDegrees < 0;
  double absVal = std::fabs(decimalDegrees);
  wxString hemi = isLatitude ? (negative ? "S" : "N") : (negative ? "W" : "E");
  int degWidth = isLatitude ? 2 : 3;

  // Built via wxUniChar(0x00B0) rather than a "\u00B0" escape embedded in
  // a plain narrow string literal -- confirmed via direct testing (byte-
  // level inspection of the resulting wxString) that the latter can be
  // silently corrupted or dropped entirely by wxString's implicit
  // `const char*` conversion, depending on the build's default string
  // encoding assumptions. wxUniChar constructs the character from its
  // actual Unicode code point, sidestepping any byte-encoding ambiguity.
  static const wxString kDegreeSign(wxUniChar(0x00B0));

  // Zero-padded degrees as a plain string, built manually rather than
  // via a "%0*d"-style dynamic-width format specifier.
  auto padDegrees = [degWidth](int deg) {
    wxString s = wxString::Format("%d", deg);
    while (static_cast<int>(s.length()) < degWidth) s = "0" + s;
    return s;
  };

  // Also: every wxString::Format() call below substitutes exactly one
  // argument, with pieces concatenated via operator+ afterward, rather
  // than passing several differently-typed arguments to one Format()
  // call -- confirmed via direct testing (full backtrace) that this
  // specific wx 3.2 build reproducibly segfaults inside
  // wxFormatString::AsWChar() when a numeric conversion and a wxString
  // conversion are combined in the same Format() call.
  switch (g_current) {
    case Format::DecimalDegrees: {
      wxString numPart = wxString::Format("%.4f", absVal);
      return numPart + kDegreeSign + " " + hemi;
    }

    case Format::DegreesMinutesSeconds: {
      int deg = static_cast<int>(absVal);
      double remMin = (absVal - deg) * 60.0;
      int min = static_cast<int>(remMin);
      double sec = (remMin - min) * 60.0;
      wxString minPart = wxString::Format("%02d", min);
      wxString secPart = wxString::Format("%04.1f", sec);
      return padDegrees(deg) + kDegreeSign + " " + minPart + "' " + secPart +
             "\" " + hemi;
    }

    case Format::DegreesDecimalMinutes:
    default: {
      int deg = static_cast<int>(absVal);
      double minVal = (absVal - deg) * 60.0;
      wxString minPart = wxString::Format("%06.3f", minVal);
      return padDegrees(deg) + kDegreeSign + " " + minPart + "' " + hemi;
    }
  }
}

bool ParseValue(const wxString& textIn, bool isLatitude,
                double* outDecimalDegrees) {
  wxString text = textIn;
  text.Trim(true).Trim(false);
  if (text.IsEmpty()) return false;

  wxString upper = text.Upper();
  bool negative = false;
  for (size_t i = 0; i < upper.length(); i++) {
    wxChar c = upper[i];
    if (c == 'S' || c == 'W') {
      negative = true;
      break;
    }
    if (c == 'N' || c == 'E') break;
  }
  if (!upper.IsEmpty() && upper[0] == '-') negative = true;

  // Extract up to 3 numeric tokens in order (degrees, minutes,
  // seconds), tolerant of whatever symbols/spacing separate them --
  // degree/minute/second marks, commas, or nothing at all are all
  // treated the same as whitespace, so this doesn't require an exact
  // match to the current display format's punctuation.
  wxString cleaned;
  for (size_t i = 0; i < text.length(); i++) {
    wxChar c = text[i];
    if (wxIsdigit(c) || c == '.') {
      cleaned += c;
    } else {
      cleaned += ' ';
    }
  }
  wxStringTokenizer tok(cleaned);
  std::vector<double> nums;
  while (tok.HasMoreTokens() && nums.size() < 3) {
    wxString t = tok.GetNextToken();
    double v = 0.0;
    if (t.ToDouble(&v)) nums.push_back(v);
  }
  if (nums.empty()) return false;

  double result = 0.0;
  if (nums.size() == 1) {
    result = nums[0];
  } else if (nums.size() == 2) {
    result = nums[0] + nums[1] / 60.0;
  } else {
    result = nums[0] + nums[1] / 60.0 + nums[2] / 3600.0;
  }

  if (isLatitude && result > 90.0) return false;
  if (!isLatitude && result > 180.0) return false;

  *outDecimalDegrees = negative ? -result : result;
  return true;
}

}  // namespace LatLonFormat
