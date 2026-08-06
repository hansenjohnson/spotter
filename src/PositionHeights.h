#ifndef WHALE_POSITION_HEIGHTS_H
#define WHALE_POSITION_HEIGHTS_H

#include <wx/wx.h>
#include <vector>
#include <utility>

// Loads/creates positions.csv: the Effort tab's "Position" dropdown
// options (Wheelhouse/Topdeck/Bow by default) paired with an observer
// eye height in feet for each -- used both to populate the Position
// dropdown and to look up the current observer height for the
// "reticles" DistUnit's horizon-based range calculation (see
// LogWindow's polling of this into DataTab::SetObserverHeightFt()).
// Editable the same way dropdowns.csv is -- add, remove, or rename
// positions and adjust their heights directly in the file.
class PositionHeights {
public:
  explicit PositionHeights(const wxString& dataDir);

  // Position names, in file order -- for populating the dropdown.
  wxArrayString GetPositionNames() const;

  // Height in feet for a given position name, or `fallbackFt` if the
  // position isn't found (e.g. blank, or a name that's been removed
  // from the file since a row was logged).
  double GetHeightFt(const wxString& position, double fallbackFt) const;

  wxString GetPath() const { return m_path; }

private:
  void CreateDefaults();
  void Load();

  wxString m_path;
  std::vector<std::pair<wxString, double>> m_positions;  // name -> height ft
};

#endif  // WHALE_POSITION_HEIGHTS_H
