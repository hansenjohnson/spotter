#include "ShortcutsFile.h"
#include "CsvUtils.h"

#include <wx/filename.h>

namespace {
struct DefaultShortcut {
  const char* combo;
  const char* action;
};

// Recognized modifier tokens: Ctrl, Alt, Shift, Cmd (Cmd maps to the Mac
// Command key, recommended for Mac users, and to Ctrl on other
// platforms). The base actions are AddSighting, AddEnvironmental,
// AddEvent, AddEffort (each adds a row to that tab), Undo/Redo (apply
// to whichever tab currently has focus), NextTab/PrevTab (cycle
// between Sightings/Effort/Events/Surfacing), and GoToSightings/
// GoToEffort/GoToEvents/GoToSummary/GoToSettings (jump straight to that
// tab).
//
// A base action can optionally be followed by ":Field=Value" (or
// several, separated by ";") to also pre-fill specific columns on the
// new row -- handy for a species you see often. For example:
//   Cmd+Shift+R,AddSighting:Species=North Atlantic right whale;Img=Photos
// adds a Sightings row with Species already set to "North Atlantic
// right whale" and Img set to "Photos". Field names must exactly match
// a column header on that tab (see the Sightings/Environmental/Events/
// Effort tabs' headers). Not valid for any of the other actions above,
// none of which take fields.
//
// Defaults use Cmd+Shift+<mnemonic letter> rather than Cmd+<number> --
// Cmd+1 through Cmd+9 are frequently reserved by macOS itself or by the
// containing app's own menu (commonly used for window/tab switching),
// which can silently swallow the keystroke before it ever reaches this
// plugin. Shift+letter combinations are much less likely to collide.
// (The tab-navigation defaults below do use Cmd+1-5/PageUp/PageDown
// anyway, since they're specifically *for* switching tabs and so a
// collision with some other app's own tab-switching shortcut is a
// less-bad failure mode than for the add-a-row actions above; if one
// of these turns out to collide with something on a given machine,
// remap it here, the same as any other entry in this file.)
const DefaultShortcut kDefaults[] = {
    {"Cmd+Shift+S", "AddSighting"},
    {"Cmd+Shift+E", "AddEnvironmental"},
    {"Cmd+Shift+V", "AddEvent"},
    {"Cmd+Shift+F", "AddEffort"},
    // Two examples of the Field=Value extended syntax, so it's
    // discoverable just by opening this file -- add more for whichever
    // species you see most often on your own survey.
    {"Cmd+Shift+R",
     "AddSighting:Species=North Atlantic right whale;Img=Photos"},
    {"Cmd+Shift+H", "AddSighting:Species=Humpback whale"},
    // Undo/Redo moved here from being hardcoded in the plugin's own
    // source, so they can be remapped the same way as any other
    // shortcut if a default combination doesn't work well on a given
    // machine. Both are unlimited (repeatable back through a whole
    // editing session, not just the last change) -- see the "Undo"/
    // "Redo" buttons on every tab's toolbar, which do the same thing
    // and don't depend on any keyboard shortcut working at all.
    {"Cmd+Shift+U", "Undo"},
    {"Cmd+Shift+Y", "Redo"},
    // Tab navigation -- also moved here from being hardcoded, per
    // direct request, since other users may want to edit these too.
    // PageUp/PageDown cycle only among the data tabs (not Summary/
    // Settings, neither of which is a data table); 1-5 jump straight to
    // any of the five tabs, Summary/Settings included.
    {"Cmd+PageUp", "PrevTab"},
    {"Cmd+PageDown", "NextTab"},
    {"Cmd+1", "GoToSightings"},
    {"Cmd+2", "GoToEffort"},
    {"Cmd+3", "GoToEvents"},
    {"Cmd+4", "GoToSummary"},
    {"Cmd+5", "GoToSettings"},
};
}  // namespace

ShortcutsFile::ShortcutsFile(const wxString& dataDir) {
  wxFileName fn(dataDir, "shortcuts.csv");
  m_path = fn.GetFullPath();

  if (!wxFileExists(m_path)) {
    CreateDefaults();
  }
  Load();
}

void ShortcutsFile::CreateDefaults() {
  std::vector<wxString> header = {"key_combo", "action"};
  std::vector<std::vector<wxString>> rows;
  for (const auto& d : kDefaults) {
    rows.push_back({wxString(d.combo), wxString(d.action)});
  }
  CsvUtils::WriteAll(m_path, header, rows);
}

void ShortcutsFile::Load() {
  auto rows = CsvUtils::ReadAll(m_path);
  for (size_t i = 1; i < rows.size(); i++) {  // skip header row
    if (rows[i].size() < 2) continue;
    wxString combo = rows[i][0];
    wxString action = rows[i][1];
    combo.Trim(true).Trim(false);
    action.Trim(true).Trim(false);
    if (combo.IsEmpty() || action.IsEmpty()) continue;
    m_shortcuts[combo] = action;
  }
}
