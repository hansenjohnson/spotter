#include "CsvUtils.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/dir.h>

namespace CsvUtils {

wxString EscapeField(const wxString& field) {
  if (field.Find(',') == wxNOT_FOUND && field.Find('"') == wxNOT_FOUND &&
      field.Find('\n') == wxNOT_FOUND && field.Find('\r') == wxNOT_FOUND) {
    return field;
  }
  wxString out = field;
  out.Replace("\"", "\"\"");
  return "\"" + out + "\"";
}

std::vector<std::vector<wxString>> ReadAll(const wxString& path) {
  std::vector<std::vector<wxString>> result;
  if (!wxFileExists(path)) return result;

  wxFile file(path, wxFile::read);
  if (!file.IsOpened()) return result;

  wxString contents;
  file.ReadAll(&contents);
  file.Close();

  std::vector<wxString> row;
  wxString field;
  bool inQuotes = false;
  size_t i = 0;
  size_t len = contents.length();

  auto endField = [&]() {
    row.push_back(field);
    field.Clear();
  };
  auto endRow = [&]() {
    endField();
    result.push_back(row);
    row.clear();
  };

  while (i < len) {
    wxChar c = contents[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < len && contents[i + 1] == '"') {
          field += '"';
          i += 2;
          continue;
        }
        inQuotes = false;
        i++;
        continue;
      }
      field += c;
      i++;
      continue;
    }
    if (c == '"') {
      inQuotes = true;
      i++;
      continue;
    }
    if (c == ',') {
      endField();
      i++;
      continue;
    }
    if (c == '\r') {
      i++;
      continue;
    }
    if (c == '\n') {
      endRow();
      i++;
      continue;
    }
    field += c;
    i++;
  }
  // Last row (file may or may not end with a newline).
  if (!field.IsEmpty() || !row.empty()) {
    endRow();
  }

  return result;
}

bool WriteAll(const wxString& path, const std::vector<wxString>& header,
              const std::vector<std::vector<wxString>>& rows) {
  wxFileName fn(path);
  if (!wxDir::Exists(fn.GetPath())) {
    wxFileName::Mkdir(fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }

  // Write to a temp file then rename over the original, so a crash mid-write
  // never leaves a half-written / corrupt CSV behind.
  wxString tmpPath = path + ".tmp";
  wxFile file;
  if (!file.Create(tmpPath, true)) return false;

  wxString line;
  for (size_t c = 0; c < header.size(); c++) {
    if (c) line << ",";
    line << EscapeField(header[c]);
  }
  line << "\n";
  file.Write(line);

  for (const auto& row : rows) {
    line.Clear();
    for (size_t c = 0; c < row.size(); c++) {
      if (c) line << ",";
      line << EscapeField(row[c]);
    }
    line << "\n";
    file.Write(line);
  }
  file.Close();

  return wxRenameFile(tmpPath, path, true);
}

bool AppendLine(const wxString& path, const wxString& line) {
  wxFileName fn(path);
  if (!wxDir::Exists(fn.GetPath())) {
    wxFileName::Mkdir(fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  wxFile file;
  bool ok;
  if (wxFileExists(path)) {
    ok = file.Open(path, wxFile::write_append);
  } else {
    ok = file.Create(path, false);
  }
  if (!ok) return false;
  file.Write(line + "\n");
  file.Close();
  return true;
}

}  // namespace CsvUtils
