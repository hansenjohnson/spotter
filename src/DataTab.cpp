#include "DataTab.h"
#include "CsvUtils.h"
#include "LatLonFormat.h"
#include "DisplaySettings.h"
#include "TimeZoneSetting.h"

#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/colordlg.h>
#include <wx/checklst.h>
#include <algorithm>
#include <set>

#include "ocpn_plugin.h"

// Maps a human-readable color name (as used in species.csv/
// event_types.csv's color column -- see item 6/the README) to an
// actual wxColour for drawing on the chart. Also used to be the set of
// choices for a since-removed per-row Color column on Sightings/Events
// (reverted back to a plain BOOL Map column per direct request); kept
// here since species.csv/event_types.csv now use the same named-color
// vocabulary for their own color column.
wxColour NamedColorToColour(const wxString& name) {
  if (name == "Red") return wxColour(220, 50, 50);
  if (name == "Orange") return wxColour(230, 126, 34);
  if (name == "Yellow") return wxColour(230, 200, 20);
  if (name == "Green") return wxColour(46, 160, 67);
  if (name == "Blue") return wxColour(52, 120, 219);
  if (name == "Navy") return wxColour(30, 60, 120);
  if (name == "Teal") return wxColour(26, 188, 156);
  if (name == "Purple") return wxColour(142, 68, 173);
  if (name == "Pink") return wxColour(230, 110, 170);
  if (name == "Brown") return wxColour(140, 90, 50);
  if (name == "Black") return wxColour(20, 20, 20);
  if (name == "Gray") return wxColour(127, 140, 141);
  if (name == "White") return wxColour(255, 255, 255);
  return wxColour(150, 150, 150);  // unrecognized/blank -- a plain
                                   // gray fallback, matching
                                   // GetChartedPoints()'s own
                                   // no-lookup-available fallback
}

namespace {

// A lat/lon cell is now *edited* in whatever the current LatLonFormat
// display format is (not forced back to plain decimal degrees) -- the
// text typed is parsed via LatLonFormat::ParseValue() and converted to
// decimal degrees before being stored, so the underlying CSV/grid data
// is always plain decimal degrees regardless of what format the user
// was looking at while editing. An earlier version always showed/
// accepted raw decimal degrees during editing specifically to avoid
// storing formatted text verbatim; parsing on save now handles that
// concern instead, while actually addressing the (reasonable) feedback
// that having the display format revert to decimal degrees the moment
// you try to edit a cell was confusing.
class FormattedLatLonGridCellEditor : public wxGridCellTextEditor {
public:
  explicit FormattedLatLonGridCellEditor(bool isLatitude)
      : m_isLatitude(isLatitude) {}

  void BeginEdit(int row, int col, wxGrid* grid) override {
    // GetValue() (not RawGet()) here specifically -- it's the *formatted*
    // display text we want to start editing from now.
    wxString value = grid->GetTable()->GetValue(row, col);
    DoBeginEdit(value);
  }

  bool EndEdit(int row, int col, const wxGrid* grid, const wxString& oldval,
               wxString* newval) override {
    wxUnusedVar(row);
    wxUnusedVar(col);
    wxUnusedVar(oldval);
    wxString typed = Text()->GetValue();
    double decDeg = 0.0;
    if (!LatLonFormat::ParseValue(typed, m_isLatitude, &decDeg)) {
      wxMessageBox(
          "Couldn't understand \"" + typed +
              "\" as a position -- keeping the previous value.\n\nTry "
              "something like 42 21.5 N (or just plain digits -- the "
              "exact degree/minute/second punctuation doesn't matter).",
          "Spotter", wxOK | wxICON_WARNING, const_cast<wxGrid*>(grid));
      *newval = oldval;
      return false;
    }
    m_parsedValue = wxString::FromDouble(decDeg, 6);
    *newval = m_parsedValue;
    return true;
  }

  void ApplyEdit(int row, int col, wxGrid* grid) override {
    // Explicitly write the *parsed* decimal-degrees value here, rather
    // than trusting the base class's ApplyEdit() to source it from
    // *newval -- confirmed via direct testing that it doesn't: it reads
    // straight from the text control instead (Text()->GetValue()),
    // silently discarding whatever EndEdit() computed and putting the
    // user's literal typed text (e.g. "42 21.5 N") into the cell
    // instead of the number that text was supposed to parse to. This
    // was the actual cause of a real reported bug ("I can edit lat/lon
    // but the change doesn't save") -- overriding ApplyEdit() directly
    // removes any dependency on exactly what the base class does.
    grid->GetTable()->SetValue(row, col, m_parsedValue);
  }

private:
  bool m_isLatitude;
  wxString m_parsedValue;
};

// A searchable dropdown for CHOICE columns (Species, Event, etc.) --
// typing filters the dropdown list live to matching options, so a long
// list can be narrowed down and picked without touching the mouse.
//
// Built on wxGridCellChoiceEditor with allowOthers=true (making the
// underlying control a real, editable wxComboBox rather than a plain
// non-editable dropdown), but does *not* rely on
// wxTextEntry::AutoComplete() for the actual filtering -- that API's
// support is inconsistent across platforms and wx versions, including
// known gaps on macOS specifically, which is exactly the platform this
// plugin needs to work reliably on. Instead, this manually re-populates
// the combo's own dropdown list (Clear() + Append() with whatever
// currently matches) on every keystroke, using only portable, always-
// available wxComboBox methods -- slower to implement than a one-line
// AutoComplete() call, but not dependent on that call actually doing
// anything on any given platform.
//
// Typed text that doesn't exactly match one of the valid choices is
// flagged with a confirmation before being accepted (same pattern as
// the lat/lon editor's unparseable-text warning) -- preserves the
// original non-editable dropdown's data-integrity guarantee (only ever
// storing one of the known choices) while still allowing the search-
// to-narrow-down workflow.
// Replaces wxMultiChoiceDialog (a stock, largely-opaque wx dialog) for
// MULTI_CHOICE columns (Behaviors, etc.) -- built from scratch to give
// this the same type-to-find behavior as the regular searchable
// dropdowns, while still allowing more than one selection. A filter
// text box narrows the checklist as you type (typing "sw" for
// "Swimming" and "Traveling" both, say); the list itself is a real
// wxCheckListBox, which natively supports Space to toggle the
// highlighted item and arrow keys to move between items once it has
// focus -- Enter in the filter box also toggles the currently
// highlighted item directly, so a common flow (type enough to narrow
// to the one item you want, hit Enter) doesn't require ever leaving
// the filter box or reaching for the mouse. Checked state is tracked
// independently of what's currently visible (a std::set, not just
// which checkboxes happen to be showing), so filtering the list down
// and back up again never loses a selection that's temporarily
// scrolled out of view.
class MultiSelectSearchDialog : public wxDialog {
public:
  MultiSelectSearchDialog(wxWindow* parent, const wxString& title,
                          const wxArrayString& choices,
                          const wxArrayString& initiallyChecked)
      : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(340, 420),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    m_allChoices = choices;
    m_allChoices.Sort();
    for (const auto& c : initiallyChecked) m_checked.insert(c);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* hint = new wxStaticText(
        this, wxID_ANY,
        "Type to filter or Tab to scroll through the list. Press Enter "
        "to select or deselect desired behavior(s). When finished, "
        "press Cmd+Enter to store the behaviors and close this window.");
    hint->Wrap(320);
    sizer->Add(hint, 0, wxALL, 8);
    m_filterBox = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                                 wxDefaultSize, wxTE_PROCESS_ENTER);
    sizer->Add(m_filterBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    m_listBox = new wxCheckListBox(this, wxID_ANY);
    sizer->Add(m_listBox, 1, wxEXPAND | wxALL, 8);

    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->AddStretchSpacer(1);
    wxButton* okBtn = new wxButton(this, wxID_OK, "OK");
    wxButton* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
    btnRow->Add(cancelBtn, 0, wxALL, 4);
    btnRow->Add(okBtn, 0, wxALL, 4);
    sizer->Add(btnRow, 0, wxALIGN_RIGHT);
    SetSizer(sizer);

    RebuildList();

    m_filterBox->Bind(wxEVT_TEXT, &MultiSelectSearchDialog::OnFilterText, this);
    m_filterBox->Bind(wxEVT_TEXT_ENTER, &MultiSelectSearchDialog::OnFilterEnter,
                      this);
    m_listBox->Bind(wxEVT_CHECKLISTBOX, &MultiSelectSearchDialog::OnItemToggled,
                    this);
    // wxCheckListBox's own native Enter behavior isn't "toggle the
    // highlighted item" (it doesn't do anything in particular for
    // Enter by default) -- bound explicitly here so Enter behaves the
    // same whether focus is in the filter box (handled via
    // OnFilterEnter, above) or, after Tab, in the list itself.
    m_listBox->Bind(wxEVT_KEY_DOWN, &MultiSelectSearchDialog::OnListKeyDown,
                    this);
    // Cmd+Enter saves and closes, regardless of which control (filter
    // box or list) currently has focus -- bound at the dialog level via
    // CHAR_HOOK, the same technique LogWindow uses for its own
    // shortcuts, since it needs to work no matter which child control
    // is focused.
    Bind(wxEVT_CHAR_HOOK, &MultiSelectSearchDialog::OnDialogCharHook, this);
    m_filterBox->SetFocus();
  }

  wxArrayString GetCheckedItems() const {
    wxArrayString out;
    for (const auto& c : m_allChoices) {
      if (m_checked.count(c)) out.Add(c);
    }
    return out;
  }

private:
  void RebuildList() {
    wxString filter = m_filterBox->GetValue().Lower();
    int prevSel = m_listBox->GetSelection();
    wxString prevSelStr =
        prevSel != wxNOT_FOUND ? m_listBox->GetString(prevSel) : wxString();

    m_listBox->Clear();
    m_visibleChoices.Clear();
    for (const auto& c : m_allChoices) {
      if (filter.IsEmpty() || c.Lower().Contains(filter)) {
        m_visibleChoices.Add(c);
      }
    }
    m_listBox->InsertItems(m_visibleChoices, 0);
    for (size_t i = 0; i < m_visibleChoices.size(); i++) {
      if (m_checked.count(m_visibleChoices[i])) m_listBox->Check(i, true);
    }

    // Keep the same item highlighted across a filter-text change if
    // it's still visible; otherwise default to the first visible item,
    // so Enter always has something sensible to act on.
    int newSel = m_visibleChoices.Index(prevSelStr);
    if (newSel == wxNOT_FOUND && !m_visibleChoices.IsEmpty()) newSel = 0;
    if (newSel != wxNOT_FOUND) m_listBox->SetSelection(newSel);
  }

  void OnFilterText(wxCommandEvent&) { RebuildList(); }

  void ToggleHighlightedItem() {
    int sel = m_listBox->GetSelection();
    if (sel == wxNOT_FOUND ||
        sel >= static_cast<int>(m_visibleChoices.size())) {
      return;
    }
    bool newState = !m_listBox->IsChecked(sel);
    m_listBox->Check(sel, newState);
    if (newState) {
      m_checked.insert(m_visibleChoices[sel]);
    } else {
      m_checked.erase(m_visibleChoices[sel]);
    }
  }

  void OnFilterEnter(wxCommandEvent&) { ToggleHighlightedItem(); }

  void OnListKeyDown(wxKeyEvent& evt) {
    if ((evt.GetKeyCode() == WXK_RETURN ||
         evt.GetKeyCode() == WXK_NUMPAD_ENTER) &&
        !evt.CmdDown() && !evt.ControlDown()) {
      ToggleHighlightedItem();
      return;  // consumed -- don't also let the list's own default
               // handling (if any) run for Enter
    }
    evt.Skip();
  }

  void OnDialogCharHook(wxKeyEvent& evt) {
    if (evt.CmdDown() && (evt.GetKeyCode() == WXK_RETURN ||
                          evt.GetKeyCode() == WXK_NUMPAD_ENTER)) {
      EndModal(wxID_OK);
      return;  // consumed
    }
    evt.Skip();
  }

  void OnItemToggled(wxCommandEvent& evt) {
    int idx = evt.GetInt();
    if (idx < 0 || idx >= static_cast<int>(m_visibleChoices.size())) return;
    if (m_listBox->IsChecked(idx)) {
      m_checked.insert(m_visibleChoices[idx]);
    } else {
      m_checked.erase(m_visibleChoices[idx]);
    }
  }

  wxArrayString m_allChoices;
  wxArrayString m_visibleChoices;
  std::set<wxString> m_checked;
  wxTextCtrl* m_filterBox;
  wxCheckListBox* m_listBox;
};

class SearchableChoiceGridCellEditor : public wxGridCellChoiceEditor {
public:
  explicit SearchableChoiceGridCellEditor(const wxArrayString &choices,
                                           bool preserveOrder = false)
      : wxGridCellChoiceEditor(SortedCopy(choices, preserveOrder),
                                true /* allowOthers -- makes this an
                                        editable combo box */),
        m_allChoices(SortedCopy(choices, preserveOrder)) {}

  // Alphabetical, so options that start with the same letter (a common
  // case -- several species, several behaviors) sit next to each other
  // in the dropdown, rather than in whatever order they happen to be
  // listed in dropdowns.csv/the column's built-in defaults. Skipped
  // entirely (returns a plain copy, unmodified) when preserveOrder is
  // set -- see ColumnDef::preserveChoiceOrder for why some columns
  // (Glare, confidence scales) need their exact defined order kept
  // instead.
  static wxArrayString SortedCopy(const wxArrayString& choices,
                                  bool preserveOrder) {
    if (preserveOrder) return choices;
    wxArrayString sorted = choices;
    // If every option is a plain number (Beaufort scale: 0-12, etc.),
    // sort numerically -- a plain alphabetical/string sort would put
    // "10" before "2" (comparing "1" < "2" character-by-character),
    // which is wrong for a numeric scale. Falls back to alphabetical
    // for anything that isn't purely numeric (species names, behaviors,
    // etc.), which is everything this was actually designed for.
    bool allNumeric = !sorted.IsEmpty();
    for (const auto& s : sorted) {
      double unused;
      if (!s.ToDouble(&unused)) {
        allNumeric = false;
        break;
      }
    }
    if (allNumeric) {
      std::sort(sorted.begin(), sorted.end(),
                [](const wxString& a, const wxString& b) {
                  double da = 0, db = 0;
                  a.ToDouble(&da);
                  b.ToDouble(&db);
                  return da < db;
                });
    } else {
      sorted.Sort();
    }
    return sorted;
  }

  void Create(wxWindow* parent, wxWindowID id,
              wxEvtHandler* evtHandler) override {
    wxGridCellChoiceEditor::Create(parent, id, evtHandler);
    m_combo = dynamic_cast<wxComboBox*>(GetControl());
    if (m_combo) {
      m_combo->Bind(wxEVT_TEXT, &SearchableChoiceGridCellEditor::OnText, this);
      // The dropdown popup closes immediately on a mouse click, but
      // (reported) not when a choice is picked via the keyboard
      // (Enter) -- wxGrid's own Enter handling commits the cell edit,
      // but doesn't itself tell the combo's popup to physically
      // dismiss if it's still open at that point. Explicitly dismissing
      // it here on Enter/Tab matches the mouse-click behavior.
      //
      // A later attempt at also fixing "Enter needs to be pressed
      // twice to select-and-close" bound wxEVT_COMBOBOX to dismiss
      // immediately once a selection was finalized -- reverted after
      // it broke arrow-key navigation through the popup entirely:
      // wxEVT_COMBOBOX fires on *every* highlight change while
      // arrow-keying through an open list, not just on a final
      // Enter-to-commit, so every arrow press was immediately
      // dismissing the popup. The double-Enter-to-close behavior below
      // is a minor, accepted annoyance -- arrow-key navigation working
      // correctly matters more.
      m_combo->Bind(wxEVT_KEY_DOWN,
                    &SearchableChoiceGridCellEditor::OnComboKeyDown, this);
      m_popupTimer.SetOwner(m_combo);
      m_combo->Bind(wxEVT_TIMER, &SearchableChoiceGridCellEditor::OnPopupTimer,
                    this);
    }
  }

  void BeginEdit(int row, int col, wxGrid* grid) override {
    // Base class sets up the combo's initial text/selection correctly;
    // it just doesn't show the dropdown list itself, so without this
    // override the popup only ever opened via an explicit click on the
    // small dropdown arrow -- reported as a real bug, since it meant a
    // keyboard-only user (arrow keys to scroll through options, Enter
    // to pick one -- both already supported once the popup is open, see
    // OnComboKeyDown below) had no way to actually see or use the list
    // without reaching for the mouse first. Explicitly popping it open
    // here means it's already visible the moment a cell becomes active
    // for editing, matching what "active" should mean for a dropdown
    // cell -- scrollable and selectable immediately, mouse optional.
    wxGridCellChoiceEditor::BeginEdit(row, col, grid);
    // Delayed via a real, short wall-clock timer (60ms) rather than
    // CallAfter() alone -- confirmed necessary for a real, reported
    // regression that persisted even after switching to CallAfter():
    // on Windows specifically, the popup still flashed open and
    // immediately closed again, before a selection could be made.
    // CallAfter() only guarantees "runs on the next idle cycle," which
    // can still race against the *native* Win32 combobox control's own,
    // separate handling of the same triggering Enter keystroke --
    // wxWidgets' own documentation confirms that on Windows, pressing
    // Enter in a combobox is "processed internally by the control"
    // (i.e. at the OS message level, entirely outside wx's C++ event
    // system) unless wxTE_PROCESS_ENTER is set, which this control
    // doesn't have. If that native, OS-level handling runs *after*
    // this call opens the popup, it can toggle it back closed --
    // matching the exact symptom reported (opens then immediately
    // closes). A short, genuine timer delay -- not just "next idle
    // cycle," an actual number of milliseconds -- gives that native
    // processing time to actually finish first: if it also opens the
    // popup as part of its own handling, this call is then a harmless
    // no-op on an already-open popup, rather than racing to open it
    // first and having the native handling close it again afterward.
    // 60ms is short enough to be imperceptible as a delay but should
    // be well clear of same-keystroke residual message processing.
    if (m_combo) m_popupTimer.StartOnce(60);
  }

  void OnPopupTimer(wxTimerEvent&) {
    if (m_combo) m_combo->Popup();
  }

  void OnComboKeyDown(wxKeyEvent& evt) {
    if (evt.GetKeyCode() == WXK_RETURN ||
        evt.GetKeyCode() == WXK_NUMPAD_ENTER || evt.GetKeyCode() == WXK_TAB) {
      // Deferred via CallAfter() rather than called directly here --
      // confirmed as the cause of a reported bug where Enter took two
      // presses to both select a highlighted item and close the
      // popup. Calling Dismiss() synchronously, inside this handler,
      // ran *before* wxComboBox's own native Enter-handling (which
      // copies whatever's highlighted in the popup into the text
      // field) had a chance to happen -- evt.Skip() below only marks
      // the event to continue propagating after this handler returns,
      // it doesn't run that native handling synchronously first. So
      // the popup was closing before the highlighted value had
      // actually been applied, and a second Enter was needed to
      // finish. Deferring the dismiss to run after the current event
      // (and its native follow-up processing) both complete fixes
      // this -- one Enter press both selects and closes.
      wxComboBox* combo = m_combo;
      combo->CallAfter([combo]() { combo->Dismiss(); });
    }
    // Tracked here (from the actual key, not inferred from a text-
    // length comparison) for OnText() below to use. An earlier version
    // compared the new text's length to the previous text's length to
    // guess whether the user was deleting -- but typing a character
    // while an inline suggestion's remainder is selected *also*
    // shortens the text (the keystroke replaces the whole selection),
    // which that heuristic wrongly read as "deleting," which in turn
    // suppressed the next suggestion. Confirmed as the cause of a
    // reported bug: typing "K" suggested "Killer whale" as expected,
    // but "Ki" showed no suggestion, "Kil" did again, alternating with
    // every keystroke -- each odd keystroke followed a suggestion
    // (so it was "typing over a selection," wrongly read as deleting),
    // each even one followed no suggestion (so it was a plain
    // append, correctly read as not deleting).
    m_lastKeyWasDelete =
        evt.GetKeyCode() == WXK_BACK || evt.GetKeyCode() == WXK_DELETE;
    evt.Skip();
  }

  // Inline autocomplete-as-you-type, in the style of a browser address
  // bar: as the user types, the first choice that starts with what
  // they've typed so far is suggested by appending the rest of it to
  // the text field with that appended part shown selected/highlighted
  // -- so the next keystroke either continues narrowing the typed
  // prefix (replacing the highlighted suggestion) or, if it's correct,
  // Enter accepts the whole thing immediately.
  //
  // Deliberately does *not* touch the dropdown's own item list (no
  // Clear()/Append() here) -- an earlier version filtered the list
  // itself down to only the matching entries as you typed, which
  // caused two reported problems: arrow keys stopped working properly
  // for navigating the list (removing/re-adding items resets the
  // list's internal navigation state), and once a cell already
  // contained an exact match, reopening it to edit showed a list
  // filtered down to just that one entry, with no way to see any other
  // option without first deleting the text. Leaving the list alone
  // fixes both: arrow keys always navigate the complete, unmodified
  // list (native wx/OS behavior, nothing extra needed here), and the
  // full list is always there when a cell is reopened, no matter what
  // it currently contains.
  void OnText(wxCommandEvent& evt) {
    evt.Skip();
    if (!m_combo || m_updatingText) return;
    wxString typed = m_combo->GetValue();

    // Only suggest while the user is actively typing forward, not
    // while deleting -- otherwise Backspace would immediately have the
    // just-deleted character re-appended as a suggestion, making it
    // impossible to actually shorten the text. See OnComboKeyDown()
    // for why this is tracked from the actual key pressed, not
    // inferred from comparing text lengths.
    if (m_lastKeyWasDelete || typed.IsEmpty()) return;

    wxString typedLower = typed.Lower();

    // If the text already exactly matches one of the valid choices,
    // there's nothing to complete further -- and critically, this is
    // what was making an option that's itself a prefix of a *longer*
    // option (e.g. Beaufort's "1", also a prefix of "10"/"11"/"12"; an
    // observer "MD", also a prefix of "MDM") unreachable via arrow-key
    // navigation or typing: landing exactly on "1" or "MD" was
    // immediately being "completed" into "10" or "MDM", even though
    // the shorter one was already a complete, correct answer on its
    // own. Confirmed as the cause of a reported bug affecting Beaufort,
    // Observer, and Species dropdowns alike.
    for (const auto& choice : m_allChoices) {
      if (choice.Lower() == typedLower) {
        if (choice != typed) {
          // Case-insensitively correct, but not an exact match -- e.g.
          // typing "MDm" (the "MD" portion still selected/highlighted
          // from an earlier suggestion, the "m" just typed in its own
          // case) for a choice actually spelled "MDM". Left as-is, this
          // exact-match text would fail EndEdit()'s own case-sensitive
          // check against m_allChoices and get flagged as an unknown
          // value. Normalizing the casing here, not just returning,
          // fixes that -- no selection/highlight needed afterward,
          // since this is already a complete answer, not a suggestion
          // to accept or keep typing over.
          m_updatingText = true;
          m_combo->ChangeValue(choice);
          m_combo->SetInsertionPointEnd();
          m_updatingText = false;
        }
        return;
      }
    }

    for (const auto& choice : m_allChoices) {
      if (choice.length() > typed.length() &&
          choice.Left(typed.length()).Lower() == typedLower) {
        m_updatingText = true;  // re-entrancy guard: ChangeValue() below
                                // would otherwise fire another
                                // wxEVT_TEXT
        m_combo->ChangeValue(choice);
        m_combo->SetSelection(static_cast<long>(typed.length()),
                              static_cast<long>(choice.length()));
        m_updatingText = false;
        break;
      }
    }
  }

  bool EndEdit(int row, int col, const wxGrid* grid, const wxString& oldval,
               wxString* newval) override {
    wxUnusedVar(row);
    wxUnusedVar(col);
    if (!m_combo) return false;
    wxString typed = m_combo->GetValue();
    if (typed != oldval && m_allChoices.Index(typed) == wxNOT_FOUND) {
      int answer = wxMessageBox(
          "\"" + typed +
              "\" doesn't match any of the standard options for this "
              "column.\n\nSave it anyway?",
          "Spotter", wxYES_NO | wxICON_WARNING | wxNO_DEFAULT,
          const_cast<wxGrid*>(grid));
      if (answer != wxYES) {
        m_pendingValue = oldval;
        *newval = oldval;
        return false;
      }
    }
    m_pendingValue = typed;
    *newval = typed;
    return typed != oldval;
  }

  void ApplyEdit(int row, int col, wxGrid* grid) override {
    // Explicit override for the same reason FormattedLatLonGridCellEditor
    // has one just above: don't trust the base class to source the
    // final value from *newval correctly.
    grid->GetTable()->SetValue(row, col, m_pendingValue);
  }

private:
  wxArrayString m_allChoices;
  wxComboBox* m_combo = nullptr;
  wxString m_pendingValue;
  bool m_updatingText = false;
  bool m_lastKeyWasDelete = false;
  wxTimer m_popupTimer;
};

}  // namespace

DataTab::DataTab(wxWindow* parent, DataTabConfig cfg, const wxString& dataDir,
                 const wxString& filePrefix)
    : m_cfg(std::move(cfg)),
      m_dataDir(dataDir),
      m_filePrefix(filePrefix),
      m_table(nullptr),
      m_grid(nullptr),
      m_panel(nullptr),
      m_toolbarSizer(nullptr),
      m_haveFix(false),
      m_fixLat(0.0),
      m_fixLon(0.0),
      m_reminderTimer(),
      m_reminderMinutes(m_cfg.reminderMinutes) {
  RebuildCsvPath();

  m_table = new GenericGridTable(m_cfg.columns);
  LoadCsv();  // populate m_table's data directly, before the grid exists

  m_panel = new wxPanel(parent);
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer* toolbar = new wxBoxSizer(wxHORIZONTAL);
  m_toolbarSizer = toolbar;
  wxButton* addBtn = new wxButton(m_panel, wxID_ANY, "+ Add Row");
  wxButton* delBtn = new wxButton(m_panel, wxID_ANY, "- Delete Selected");
  toolbar->Add(addBtn, 0, wxALL, 4);
  toolbar->Add(delBtn, 0, wxALL, 4);

  // The Lat/Lon format button (previously here, for tabs with a
  // geo-role column) moved to the top of the Settings tab per direct
  // request -- the format is a single global setting, not per-tab, so
  // having it repeated on every tab's own toolbar was more clutter than
  // it was worth. See LogWindow::BuildSettingsTab().

  // All buttons left-justified, lined up in one row -- per direct
  // request (an earlier round had right-justified Undo/Redo via a
  // stretch spacer here; reversed).
  wxButton* undoBtn = new wxButton(m_panel, wxID_ANY, "Undo");
  wxButton* redoBtn = new wxButton(m_panel, wxID_ANY, "Redo");
  toolbar->Add(undoBtn, 0, wxALL, 4);
  toolbar->Add(redoBtn, 0, wxALL, 4);

  sizer->Add(toolbar, 0, wxEXPAND);

  m_grid = new wxGrid(m_panel, wxID_ANY);
  m_grid->SetTable(m_table, true /* grid takes ownership */);
  // Lets the user manually fix a row's height by dragging its row-label
  // divider -- previously disabled with no specific reason recorded;
  // enabled per direct request, partly as its own usability feature and
  // partly as a manual workaround for existing rows sometimes staying
  // at a stale, too-short height after reopening the plugin (see
  // SetGridFontSize()'s comment for the actual fix for that).
  m_grid->EnableDragRowSize(true);
  m_grid->SetRowLabelSize(40);
  // Without this, wxGrid's GetBestSize() (based on the sum of all its
  // *natural* column widths) gets enforced by the containing sizers as
  // a hard minimum size, silently preventing the window -- and this
  // grid -- from ever actually being resized smaller than that. A small
  // explicit minimum lets sizers actually shrink the grid; any real
  // overflow is handled by the grid's own horizontal scrollbar.
  m_grid->SetMinSize(wxSize(200, 100));
  // Captured *after* the grid exists, before any reminder-overdue tint is
  // ever applied, so we can restore the platform's real default (which
  // may be dark, per the current theme) rather than hardcoding white.
  m_normalGridBg = m_grid->GetDefaultCellBackgroundColour();

  int visibleIdx = 0;
  for (const auto& c : m_cfg.columns) {
    if (c.type == ColumnDef::HIDDEN) continue;
    m_grid->SetColSize(visibleIdx, std::max(c.width, MinWidthForLabel(c.name)));
    visibleIdx++;
  }
  // Per-cell setup (read-only MULTI_CHOICE/BUTTON cells, custom geo/
  // choice editors, BUTTON styling) for whichever rows LoadCsv() already
  // populated before the grid was created -- see ApplyPerCellSetup(),
  // also used by OnRowAdded() and Undo() for the same purpose on rows
  // that arrive later.
  for (int r = 0; r < m_grid->GetNumberRows(); r++) {
    ApplyPerCellSetup(r);
  }
  if (m_grid->GetNumberRows() > 0) {
    UpdateContentMinWidths();
  }

  sizer->Add(m_grid, 1, wxEXPAND | wxALL, 2);
  m_panel->SetSizer(sizer);

  addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddRow(); });
  delBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteSelectedRows(); });
  // Both fail silently when there's nothing to undo/redo -- no
  // notification popup, per direct request.
  undoBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Undo(); });
  redoBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Redo(); });

  m_table->on_cell_edited = [this](int r, int c) { OnCellEdited(r, c); };
  m_table->on_row_added = [this](int r) { OnRowAdded(r); };
  m_table->on_row_about_to_delete = [this](int r) { OnRowAboutToDelete(r); };

  // Deliberately NOT binding wxEVT_SIZE on m_grid itself here (an
  // earlier version of this code did) -- confirmed via direct testing
  // that a deeply-nested child's own SIZE events don't reliably fire
  // when the top-level frame is resized (the frame's own size updates
  // correctly, but events don't reliably propagate all the way down
  // through panel -> notebook -> tab panel -> grid). LogWindow instead
  // binds wxEVT_SIZE on itself (the frame), which *is* reliable, and
  // calls DataTab::ForceResizeColumnsToFit() on every tab directly.
  m_grid->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &DataTab::OnCellDoubleClick, this);
  m_grid->Bind(wxEVT_GRID_CELL_LEFT_CLICK, &DataTab::OnCellLeftClick, this);
  m_grid->Bind(wxEVT_GRID_SELECT_CELL, &DataTab::OnCellSelected, this);
  // A wxEVT_GRID_RANGE_SELECTED handler used to also be bound here, as
  // an attempt to cover click-and-drag multi-cell selection for the
  // column-definition display. Removed after being confirmed, via
  // direct testing, to be the cause of a real, reported bug: it fires
  // a spurious event reporting column 0 as part of perfectly normal
  // single-cell selection (e.g. right as editing starts), which
  // incorrectly reset the displayed column definition to whatever
  // column 0's was, regardless of which cell was actually selected.
  // wxEVT_GRID_SELECT_CELL alone correctly reports the right column in
  // every case this was tested against, without that problem.
  m_grid->Bind(wxEVT_KEY_DOWN, &DataTab::OnGridKeyDown, this);
  m_grid->Bind(wxEVT_GRID_CELL_CHANGING, &DataTab::OnCellChanging, this);

  if (m_cfg.enableReminderTimer) {
    m_reminderTimer.SetOwner(m_panel);
    m_panel->Bind(
        wxEVT_TIMER, [this](wxTimerEvent&) { OnReminderTick(); },
        m_reminderTimer.GetId());
    m_reminderTimer.Start(1000);
    ResetReminderTimer();
  }
}

void DataTab::RebuildCsvPath() {
  wxString filename = m_filePrefix.IsEmpty()
                          ? m_cfg.csvFilename
                          : m_filePrefix + "_" + m_cfg.csvFilename;
  wxFileName fn(m_dataDir, filename);
  m_csvPath = fn.GetFullPath();
}

int DataTab::MinWidthForLabel(const wxString& label) const {
  if (!m_grid) return 50;
  // wxWindow::GetTextExtent() measures using the window's own device
  // context safely -- unlike constructing a standalone wxMemoryDC
  // without a bitmap selected into it first, which is invalid and was
  // the cause of a real crash elsewhere in this codebase (see
  // spotter_pi.cpp's DrawOverlayGL comments).
  int w = 0, h = 0;
  wxFont labelFont = m_grid->GetLabelFont();
  m_grid->GetTextExtent(label, &w, &h, nullptr, nullptr, &labelFont);
  return std::max(50, w + 20);  // padding for header borders/sort glyph
}

void DataTab::UpdateContentMinWidths() {
  if (!m_grid) return;
  m_grid->AutoSizeColumns(false);
  m_contentMinWidths.clear();
  for (int i = 0; i < m_grid->GetNumberCols(); i++) {
    m_contentMinWidths.push_back(m_grid->GetColSize(i));
  }
}

void DataTab::ResizeColumnsToFit() {
  if (!m_grid) return;
  int availableWidth =
      m_grid->GetClientSize().GetWidth() - m_grid->GetRowLabelSize();
  if (availableWidth <= 0) return;

  long totalBaseWidth = 0;
  for (const auto& c : m_cfg.columns) {
    if (c.type != ColumnDef::HIDDEN) totalBaseWidth += c.width;
  }
  if (totalBaseWidth <= 0) return;

  double scale = static_cast<double>(availableWidth) / totalBaseWidth;

  int visibleIdx = 0;
  for (const auto& c : m_cfg.columns) {
    if (c.type == ColumnDef::HIDDEN) continue;
    // Never shrink a column below what's needed to show its own header
    // title, or its widest cell's actual content -- both act as a floor
    // that takes priority over the generic proportional-scaling width
    // when either is larger.
    int contentFloor = visibleIdx < static_cast<int>(m_contentMinWidths.size())
                           ? m_contentMinWidths[visibleIdx]
                           : 0;
    int newWidth = std::max({MinWidthForLabel(c.name), contentFloor,
                             static_cast<int>(c.width * scale)});
    m_grid->SetColSize(visibleIdx, newWidth);
    visibleIdx++;
  }
}

void DataTab::OpenMultiSelectEditor(int row, int col) {
  if (col < 0 || col >= static_cast<int>(m_cfg.columns.size()) || row < 0) {
    return;
  }
  const ColumnDef& cd = m_cfg.columns[col];
  if (cd.type != ColumnDef::MULTI_CHOICE) return;

  wxString current = m_table->RawGet(row, col);
  wxArrayString currentValues = wxStringTokenize(current, ",", wxTOKEN_STRTOK);
  for (auto& s : currentValues) s.Trim(true).Trim(false);

  MultiSelectSearchDialog dlg(m_grid, cd.name, cd.choices, currentValues);
  if (dlg.ShowModal() != wxID_OK) return;

  wxArrayString selections = dlg.GetCheckedItems();
  wxString joined;
  for (size_t i = 0; i < selections.size(); i++) {
    if (i) joined << ", ";
    joined << selections[i];
  }
  m_grid->SetCellValue(row, col, joined);  // goes through the normal
                                           // SetValue -> on_cell_edited
                                           // path, so it's saved
}

void DataTab::OnCellDoubleClick(wxGridEvent& evt) {
  int col = evt.GetCol();
  if (col < 0 || col >= static_cast<int>(m_cfg.columns.size()) ||
      m_cfg.columns[col].type != ColumnDef::MULTI_CHOICE) {
    evt.Skip();
    return;
  }
  OpenMultiSelectEditor(evt.GetRow(), col);
}

void DataTab::OnCellLeftClick(wxGridEvent& evt) {
  int col = evt.GetCol();
  if (col < 0 || col >= static_cast<int>(m_cfg.columns.size()) ||
      m_cfg.columns[col].type != ColumnDef::BUTTON) {
    evt.Skip();
    return;
  }
  if (on_button_column_clicked) on_button_column_clicked(evt.GetRow());
  // Deliberately not calling evt.Skip() -- a single click on a BUTTON
  // cell is fully handled here; letting it propagate further would just
  // try to select/focus a read-only cell for no benefit.
}

void DataTab::OnCellSelected(wxGridEvent& evt) {
  evt.Skip();
  int col = evt.GetCol();
  if (on_cell_selected && col >= 0 &&
      col < static_cast<int>(m_cfg.columns.size())) {
    on_cell_selected(m_cfg.columns[col].name);
  }
}

void DataTab::OnCellChanging(wxGridEvent& evt) {
  int col = evt.GetCol();
  if (col < 0 || col >= static_cast<int>(m_cfg.columns.size())) {
    evt.Skip();
    return;
  }
  // SightNo is used by the Surfacings tab to cross-reference a specific
  // Sightings row -- changing it after the fact could silently break
  // that link, so confirm before allowing the edit (auto-filling it
  // when a row is first added doesn't go through this path at all,
  // since that's a direct RawSet() rather than a grid-driven edit, so
  // this only ever fires for an actual manual edit).
  if (m_cfg.columns[col].name == "SightNo") {
    int answer = wxMessageBox(
        "SightNo is used to cross-reference this sighting from the "
        "Surfacings tab. Changing it won't update any Surfacings rows "
        "that already reference the old number.\n\nChange it anyway?",
        "Spotter -- Edit SightNo", wxYES_NO | wxICON_WARNING | wxNO_DEFAULT,
        m_grid);
    if (answer != wxYES) {
      evt.Veto();
      return;
    }
  }
  SaveUndoSnapshot();
  evt.Skip();
}

void DataTab::OnGridKeyDown(wxKeyEvent& evt) {
  if (evt.GetKeyCode() == WXK_RETURN || evt.GetKeyCode() == WXK_NUMPAD_ENTER ||
      evt.GetKeyCode() == WXK_F2 || evt.GetKeyCode() == WXK_SPACE) {
    if (TryOpenMultiSelectForCurrentCell()) {
      return;  // consumed -- don't let the default editor try to open too
    }
  }
  evt.Skip();
}

bool DataTab::TryOpenMultiSelectForCurrentCell() {
  if (!m_grid) return false;
  int row = m_grid->GetGridCursorRow();
  int col = m_grid->GetGridCursorCol();
  if (col < 0 || col >= static_cast<int>(m_cfg.columns.size()) ||
      m_cfg.columns[col].type != ColumnDef::MULTI_CHOICE) {
    return false;
  }
  OpenMultiSelectEditor(row, col);
  return true;
}

bool DataTab::TryStartEditingCurrentCell() {
  if (!m_grid || m_grid->IsCellEditControlEnabled()) return false;
  int row = m_grid->GetGridCursorRow();
  int col = m_grid->GetGridCursorCol();
  if (col < 0 || col >= static_cast<int>(m_cfg.columns.size())) return false;
  if (m_cfg.columns[col].type == ColumnDef::MULTI_CHOICE ||
      m_cfg.columns[col].type == ColumnDef::BUTTON) {
    return false;  // handled separately (MULTI_CHOICE) or not editable
                   // via this path at all (BUTTON)
  }
  if (m_grid->IsReadOnly(row, col)) return false;
  m_grid->SetFocus();
  m_grid->SetGridCursor(row, col);
  m_grid->EnableCellEditControl(true);
  return true;
}

void DataTab::SetReminderIntervalMinutes(int minutes) {
  if (minutes <= 0) return;
  m_reminderMinutes = minutes;
  ResetReminderTimer();
}

void DataTab::ResetReminderTimer() {
  if (!m_cfg.enableReminderTimer) return;
  m_reminderDeadline =
      wxDateTime::Now() + wxTimeSpan::Minutes(m_reminderMinutes);
  OnReminderTick();
}

bool DataTab::IsReminderOverdue() const {
  if (!m_cfg.enableReminderTimer || !m_reminderDeadline.IsValid()) {
    return false;
  }
  return (m_reminderDeadline - wxDateTime::Now()).GetSeconds() <= 0;
}

wxString DataTab::GetReminderCountdownText() const {
  if (!m_cfg.enableReminderTimer || !m_reminderDeadline.IsValid()) {
    return wxEmptyString;
  }
  if (IsReminderOverdue()) return "OVERDUE";
  wxTimeSpan remaining = m_reminderDeadline - wxDateTime::Now();
  long secs = remaining.GetSeconds().ToLong();
  return wxString::Format("%02ld:%02ld", secs / 60, secs % 60);
}

void DataTab::OnReminderTick() {
  if (!m_grid) return;
  // The countdown text itself is read by LogWindow's status bar
  // (GetReminderCountdownText()) rather than displayed here -- this tab
  // only needs to handle the overdue grid tint.
  if (IsReminderOverdue()) {
    m_grid->SetDefaultCellBackgroundColour(wxColour(180, 40, 40));
  } else {
    m_grid->SetDefaultCellBackgroundColour(m_normalGridBg);
  }
  m_grid->ForceRefresh();
}

int DataTab::RowCount() const { return m_table->GetNumberRows(); }

void DataTab::SaveUndoSnapshot() {
  m_undoStack.push_back(m_table->Data());  // deep copy
  // A genuinely new edit is happening -- any previously available redo
  // history no longer makes sense (the same convention as a
  // spreadsheet or text editor: redo is only available immediately
  // after an undo, not after doing something else).
  m_redoStack.clear();
}

void DataTab::RestoreSnapshot(
    const std::vector<std::vector<wxString>>& snapshot) {
  int n = m_table->GetNumberRows();
  if (n > 0) m_table->DeleteRows(0, static_cast<size_t>(n));
  for (auto& row : snapshot) {
    m_table->AppendDataRow(row);
  }
  // AppendDataRow() (used above instead of AppendRows()/AddRow() so the
  // exact snapshotted values are restored verbatim, without also
  // re-running OnRowAdded's auto-fill/auto-increment/inheritance logic
  // on top of them) doesn't itself notify the grid of the new rows --
  // confirmed via direct testing that skipping this causes a wxGrid
  // assertion failure the next time the grid does almost anything.
  m_table->NotifyGridRowCountChanged(0);
  // Nor does it apply the per-cell read-only/editor/styling setup that
  // OnRowAdded() and the constructor both do for their rows -- without
  // this, a restored MULTI_CHOICE cell would (for example) show the
  // default text editor instead of staying read-only.
  for (int r = 0; r < m_table->GetNumberRows(); r++) {
    ApplyPerCellSetup(r);
  }
  SaveCsv();
  if (m_grid) {
    UpdateContentMinWidths();
    ReapplyRowHeights();
    m_grid->ForceRefresh();
  }
  if (on_chart_changed) on_chart_changed();
}

bool DataTab::Undo() {
  if (m_undoStack.empty()) return false;
  // Capture what's about to be overwritten, so Redo() can bring it back.
  m_redoStack.push_back(m_table->Data());
  RestoreSnapshot(m_undoStack.back());
  m_undoStack.pop_back();
  return true;
}

bool DataTab::Redo() {
  if (m_redoStack.empty()) return false;
  // Symmetric with Undo(): capture the current state onto the undo
  // stack before restoring, so undoing again after a redo correctly
  // steps back to where the redo started from, not further back than
  // that.
  m_undoStack.push_back(m_table->Data());
  RestoreSnapshot(m_redoStack.back());
  m_redoStack.pop_back();
  return true;
}

void DataTab::ClearAllData() {
  int n = m_table->GetNumberRows();
  if (n > 0) m_table->DeleteRows(0, static_cast<size_t>(n));
  SaveCsv();  // rewrites the *same* file, now just the header
  if (m_grid) m_grid->ForceRefresh();
  if (on_chart_changed) on_chart_changed();
}

void DataTab::RefreshDisplay() {
  if (m_grid) m_grid->ForceRefresh();
}

void DataTab::SetGridFontSize(int pointSize) {
  if (!m_grid || pointSize <= 0) return;
  wxFont cellFont = m_grid->GetDefaultCellFont();
  cellFont.SetPointSize(pointSize);
  m_grid->SetDefaultCellFont(cellFont);

  wxFont labelFont = m_grid->GetLabelFont();
  labelFont.SetPointSize(pointSize);
  m_grid->SetLabelFont(labelFont);

  // Row sizes were computed against the old font metrics -- recompute
  // so nothing looks clipped after a size change.
  m_grid->AutoSizeRows();
  // Column widths need the same treatment, but re-measured against the
  // *actual current cell content* at the new font size
  // (UpdateContentMinWidths(), via wxGrid's own AutoSizeColumns()) --
  // not just each column's static, hardcoded base width from its
  // ColumnDef, which assumes the font size those base widths were
  // originally tuned for. Confirmed as a real, reported bug: at a
  // larger grid font size (14pt), Time and Lat/Lon cells were getting
  // clipped, since the previous code here reset every column back to
  // max(base width, header-label width) regardless of whether the
  // actual cell content at the new, larger font size needed more room
  // than that. ResizeColumnsToFit() uses the freshly-recomputed content
  // floor from UpdateContentMinWidths() as one of the inputs to its own
  // proportional-scaling width, so it settles on a width that's at
  // least as wide as the actual content needs, not just the column's
  // designed-for-default-font-size base width.
  UpdateContentMinWidths();
  ResizeColumnsToFit();
  m_grid->ForceRefresh();
}

void DataTab::ReapplyRowHeights() {
  if (!m_grid) return;
  // Reset every row to a minimal height *before* AutoSizeRows() below,
  // so it's forced to measure fresh against actual current content --
  // confirmed as necessary to fix a real, reported regression: without
  // this reset, AutoSizeRows() treats a row's *current* height as a
  // floor and won't shrink it, only grow it if needed, so repeated
  // calls (e.g. on every tab switch) kept compounding whatever padding
  // was added below on top of an already-padded height. 1px specifically
  // (not e.g. GetDefaultRowSize()) -- wx's own generic default row
  // height isn't guaranteed to match this app's own custom cell font
  // (see SetGridFontSize()), and using it as the reset floor could
  // itself end up larger than a given row's real content needs, for
  // the same reason AutoSizeRows() can't be trusted to shrink below
  // whatever floor it's given.
  for (int row = 0; row < m_grid->GetNumberRows(); row++) {
    m_grid->SetRowSize(row, 1);
  }
  m_grid->AutoSizeRows();
#ifdef __WXMSW__
  // A small safety margin beyond whatever AutoSizeRows() itself
  // calculated -- a real, reported bug, but specifically on Windows:
  // rows came out too short to comfortably fit their text there
  // (fixable by hand via drag-resize, confirming it's a sizing-
  // calculation gap, not a font/rendering problem). AutoSizeRows()'s
  // calculation ultimately depends on the platform's own font-metric
  // APIs (GDI on Windows vs. Core Text/Pango elsewhere), which are
  // known to differ in how tightly they report a line's height.
  // Scoped to Windows specifically -- a real, reported regression from
  // an earlier version of this fix: applied unconditionally on every
  // platform, this same padding made macOS's rows larger than
  // necessary, even though AutoSizeRows() alone was already correct
  // there before this fix existed at all.
  for (int row = 0; row < m_grid->GetNumberRows(); row++) {
    m_grid->SetRowSize(row, m_grid->GetRowSize(row) + 6);
  }
#endif
  m_grid->ForceRefresh();
}

int DataTab::GetRowHeight(int row) const {
  if (!m_grid || row < 0 || row >= m_grid->GetNumberRows()) return -1;
  return m_grid->GetRowSize(row);
}

std::vector<int> DataTab::GetColumnWidths() const {
  std::vector<int> widths;
  if (!m_grid) return widths;
  for (int i = 0; i < m_grid->GetNumberCols(); i++) {
    widths.push_back(m_grid->GetColSize(i));
  }
  return widths;
}

void DataTab::SetVesselFix(double lat, double lon, const wxDateTime& utc) {
  m_haveFix = true;
  m_fixLat = lat;
  m_fixLon = lon;
  m_fixTime = utc;
}

void DataTab::LoadCsv() {
  auto rows = CsvUtils::ReadAll(m_csvPath);
  if (rows.empty()) return;

  std::vector<wxString> expectedHeader;
  for (const auto& c : m_cfg.columns) expectedHeader.push_back(c.name);

  bool headerMatches = (rows[0].size() == expectedHeader.size());
  if (headerMatches) {
    for (size_t i = 0; i < expectedHeader.size(); i++) {
      if (rows[0][i] != expectedHeader[i]) {
        headerMatches = false;
        break;
      }
    }
  }

  if (!headerMatches) {
    // The file's columns don't match this version of the plugin (e.g. an
    // older release wrote it). Don't silently drop or corrupt it -- keep
    // it under a backup name and start this tab fresh.
    wxString backupPath =
        m_csvPath + ".bak-" + wxDateTime::Now().Format("%Y%m%dT%H%M%S");
    wxRenameFile(m_csvPath, backupPath, false);
    return;
  }

  for (size_t r = 1; r < rows.size(); r++) {
    m_table->AppendDataRow(rows[r]);
  }
}

void DataTab::SaveCsv() {
  std::vector<wxString> header;
  for (const auto& c : m_cfg.columns) header.push_back(c.name);
  CsvUtils::WriteAll(m_csvPath, header, m_table->Data());
}

void DataTab::ExportCopyTo(const wxString& destDir) {
  std::vector<wxString> header;
  for (const auto& c : m_cfg.columns) header.push_back(c.name);
  wxFileName fn(destDir, wxFileName(m_csvPath).GetFullName());
  CsvUtils::WriteAll(fn.GetFullPath(), header, m_table->Data());
}

void DataTab::StartNewFile(const wxString& newPrefix) {
  int n = m_table->GetNumberRows();
  if (n > 0) m_table->DeleteRows(0, n);

  m_filePrefix = newPrefix;
  RebuildCsvPath();
  SaveCsv();  // writes just the header to the new file

  if (m_grid) m_grid->ForceRefresh();
  if (on_chart_changed) on_chart_changed();
}

void DataTab::LoadSurvey(const wxString& newPrefix) {
  int n = m_table->GetNumberRows();
  if (n > 0) m_table->DeleteRows(0, static_cast<size_t>(n));

  m_filePrefix = newPrefix;
  RebuildCsvPath();
  LoadCsv();  // populates from the new path if a file already exists
              // there (e.g. this survey already has some Sightings
              // rows); leaves the table empty otherwise, the same as
              // a brand new survey would
  // Rewrites the file in this plugin's own current column order/header
  // either way -- matters most when loading from an externally-copied
  // folder, whose files might have come from a slightly different
  // plugin version.
  SaveCsv();

  if (m_grid) {
    m_table->NotifyGridRowCountChanged(0);
    for (int r = 0; r < m_table->GetNumberRows(); r++) {
      ApplyPerCellSetup(r);
    }
    UpdateContentMinWidths();
    ReapplyRowHeights();
    m_grid->ForceRefresh();
  }
  if (on_chart_changed) on_chart_changed();
}

void DataTab::SetCellValueByName(int row, const wxString& colName,
                                 const wxString& value) {
  int col = m_table->FindColByName(colName);
  if (col < 0 || col >= m_table->NumVisibleCols()) return;
  // wxGrid::SetCellValue() (unlike the interactive in-place editing
  // flow) doesn't itself fire wxEVT_GRID_CELL_CHANGING -- confirmed via
  // direct testing -- so OnCellChanging()'s own SaveUndoSnapshot() call
  // never runs for a programmatic edit like this one, silently making
  // it un-undoable (including shortcut-triggered field population, e.g.
  // "AddSighting:Species=Right whale"). Saved explicitly here instead.
  SaveUndoSnapshot();
  // Goes through wxGrid so the table's normal SetValue() path runs (fires
  // on_cell_edited -> recompute / overlay refresh / SaveCsv), same as if
  // the user had typed it in.
  m_grid->SetCellValue(row, col, value);
}

wxString DataTab::GetCellValueByName(int row, const wxString& colName) const {
  int col = m_table->FindColByName(colName);
  if (col < 0) return wxEmptyString;
  return m_table->RawGet(row, col);
}

std::vector<ChartPoint> DataTab::GetChartedPoints() const {
  std::vector<ChartPoint> points;
  if (m_cfg.chartCol < 0 || m_cfg.latCol < 0 || m_cfg.lonCol < 0) {
    return points;
  }
  int latCol = m_cfg.overlayLatCol >= 0 ? m_cfg.overlayLatCol : m_cfg.latCol;
  int lonCol = m_cfg.overlayLonCol >= 0 ? m_cfg.overlayLonCol : m_cfg.lonCol;

  for (int row = 0; row < m_table->GetNumberRows(); row++) {
    // Reverted back to a plain BOOL "Map" column (show/hide only) per
    // direct request -- an intermediate version of this plugin had a
    // CHOICE "Color" column here instead, letting each row's color be
    // overridden individually; that's gone now, but the underlying
    // species/event-based color lookup it introduced stays (below),
    // just without the per-row override option.
    if (m_table->RawGet(row, m_cfg.chartCol) != "1") continue;

    double lat = 0.0, lon = 0.0;
    wxString latStr = m_table->RawGet(row, latCol);
    wxString lonStr = m_table->RawGet(row, lonCol);
    if (latStr.IsEmpty() || lonStr.IsEmpty()) continue;
    if (!latStr.ToDouble(&lat) || !lonStr.ToDouble(&lon)) continue;

    ChartPoint pt;
    pt.lat = lat;
    pt.lon = lon;
    if (m_cfg.labelTextFn) {
      pt.labelText = m_cfg.labelTextFn(m_table, row);
    } else {
      pt.labelText = m_cfg.overlayTextCol >= 0
                         ? m_table->RawGet(row, m_cfg.overlayTextCol)
                         : "";
    }
    // Resolved via this row's Species/Event, looked up in
    // species.csv/event_types.csv (chart_default_color_lookup, wired by
    // LogWindow for Sightings/Events specifically). Left unset for any
    // tab without a lookup wired (Surfacing, currently disabled) or
    // without a key value, so drawMarkers()/drawMarkersGL() fall back
    // to that tab's own configured marker color instead of a flat gray.
    wxString key = m_cfg.chartColorKeyCol >= 0
                       ? m_table->RawGet(row, m_cfg.chartColorKeyCol)
                       : wxString();
    if (chart_default_color_lookup && !key.IsEmpty()) {
      pt.color = chart_default_color_lookup(key);
    }
    points.push_back(pt);
  }
  return points;
}

void DataTab::AddRow() {
  SaveUndoSnapshot();
  m_table->AppendRows(1);  // fires OnRowAdded via the table's callback
  if (m_cfg.enableReminderTimer) ResetReminderTimer();
}

void DataTab::ApplyPerCellSetup(int row) {
  int visibleIdx = 0;
  for (const auto& c : m_cfg.columns) {
    if (c.type == ColumnDef::HIDDEN) continue;
    if (c.type == ColumnDef::MULTI_CHOICE) {
      m_grid->SetReadOnly(row, visibleIdx, true);
    }
    if (c.geoRole != ColumnDef::GeoRole::None) {
      bool isLat = c.geoRole == ColumnDef::GeoRole::Latitude;
      m_grid->SetCellEditor(row, visibleIdx,
                            new FormattedLatLonGridCellEditor(isLat));
    }
    if (c.type == ColumnDef::CHOICE) {
      m_grid->SetCellEditor(
          row, visibleIdx,
          new SearchableChoiceGridCellEditor(c.choices, c.preserveChoiceOrder));
    }
    if (c.type == ColumnDef::BUTTON) {
      m_grid->SetReadOnly(row, visibleIdx, true);
      m_grid->SetCellBackgroundColour(row, visibleIdx, wxColour(215, 230, 250));
      m_grid->SetCellTextColour(row, visibleIdx, wxColour(20, 60, 140));
      m_grid->SetCellAlignment(row, visibleIdx, wxALIGN_CENTRE, wxALIGN_CENTRE);
    }
    visibleIdx++;
  }
}

void DataTab::OnRowAdded(int row) {
  // A freshly-added row needs the same per-cell setup (read-only
  // MULTI_CHOICE/BUTTON cells, custom geo/choice editors, BUTTON
  // styling) as whichever rows existed at construction time.
  ApplyPerCellSetup(row);

  if (m_cfg.chartCol >= 0) m_table->RawSet(row, m_cfg.chartCol, "1");

  // Timestamp formatted per the Settings tab's General > Timezone
  // choice (TimeZoneSetting) -- "System Default" preserves this
  // plugin's original behavior (the computer's own configured
  // timezone); a specific zone formats the same underlying instant in
  // that zone's wall-clock time instead, regardless of what the
  // computer's own timezone is set to. Added per direct request,
  // specifically to avoid a survey's timestamps becoming inconsistent
  // if the computer's own timezone changes mid-survey (some systems
  // auto-adjust it based on location).
  wxDateTime t = m_haveFix ? m_fixTime : wxDateTime::Now();
  if (!t.IsValid()) t = wxDateTime::Now();
  if (m_cfg.timeCol >= 0) {
    m_table->RawSet(row, m_cfg.timeCol,
                    TimeZoneSetting::FormatInSelectedZone(t));
  }
  if (m_haveFix) {
    if (m_cfg.latCol >= 0)
      m_table->RawSet(row, m_cfg.latCol, wxString::FromDouble(m_fixLat, 6));
    if (m_cfg.lonCol >= 0)
      m_table->RawSet(row, m_cfg.lonCol, wxString::FromDouble(m_fixLon, 6));
  }

  // Captured once, here, from whatever the observer height is *right
  // now* -- not read live later when the "reticles" DistUnit is
  // actually computed, which could be well after the observer has
  // since moved to a different position (see
  // RecomputeBearingDistancePosition's comment for why that distinction
  // matters).
  {
    int obsHeightCol = m_table->FindColByName("ObsHeightFt");
    if (obsHeightCol >= 0) {
      m_table->RawSet(row, obsHeightCol,
                      wxString::FromDouble(m_observerHeightFt, 2));
    }
  }

  if (m_cfg.autoIncrementCol >= 0) {
    long maxVal = 0;
    for (int r = 0; r < m_table->GetNumberRows(); r++) {
      if (r == row) continue;
      long v = 0;
      if (m_table->RawGet(r, m_cfg.autoIncrementCol).ToLong(&v)) {
        maxVal = std::max(maxVal, v);
      }
    }
    m_table->RawSet(row, m_cfg.autoIncrementCol,
                    wxString::Format("%ld", maxVal + 1));
  }

  // Inherit the previous row's values (Environmental: conditions usually
  // haven't changed since the last check) before falling back to
  // choice-defaulting below -- takes priority when there *is* a
  // previous row, since a copied real value is a better default than
  // the first item in a dropdown.
  if (m_cfg.inheritPreviousRowValues && row > 0) {
    for (size_t i = 0; i < m_cfg.columns.size(); i++) {
      int ci = static_cast<int>(i);
      if (ci == m_cfg.chartCol || ci == m_cfg.timeCol || ci == m_cfg.latCol ||
          ci == m_cfg.lonCol || ci == m_cfg.autoIncrementCol) {
        continue;
      }
      if (m_cfg.columns[i].skipInherit) continue;
      m_table->RawSet(row, ci, m_table->RawGet(row - 1, ci));
    }
  }

  // Explicit per-column defaults (ColumnDef::defaultValue) apply
  // regardless of defaultChoicesToFirstOption -- e.g. DistUnit
  // defaulting to "nm" even on Sightings, which otherwise deliberately
  // leaves CHOICE columns blank (see the comment just below) so a
  // not-yet-identified Species doesn't end up looking identified.
  for (size_t i = 0; i < m_cfg.columns.size(); i++) {
    if (m_cfg.columns[i].type == ColumnDef::CHOICE &&
        !m_cfg.columns[i].defaultValue.IsEmpty() &&
        m_table->RawGet(row, static_cast<int>(i)).IsEmpty()) {
      m_table->RawSet(row, static_cast<int>(i), m_cfg.columns[i].defaultValue);
    }
  }

  // Give any still-empty choice columns a real default instead of
  // showing blank -- skipped entirely for tabs (like Sightings) that
  // set defaultChoicesToFirstOption=false, since auto-filling e.g.
  // Species with its first option could leave a not-yet-identified
  // sighting looking like it was actually identified.
  if (m_cfg.defaultChoicesToFirstOption) {
    for (size_t i = 0; i < m_cfg.columns.size(); i++) {
      if (m_cfg.columns[i].type == ColumnDef::CHOICE &&
          m_table->RawGet(row, static_cast<int>(i)).IsEmpty() &&
          !m_cfg.columns[i].choices.IsEmpty()) {
        m_table->RawSet(row, static_cast<int>(i), m_cfg.columns[i].choices[0]);
      }
    }
  }

  if (m_cfg.recompute) m_cfg.recompute(this, m_table, row, m_cfg.latCol);

  SaveCsv();
  UpdateContentMinWidths();  // widen to fit new content, and remember
                             // this as a floor so a later window
                             // resize can't shrink columns back below
                             // it
  m_grid->ForceRefresh();
  m_grid->MakeCellVisible(row, 0);
  m_grid->SetGridCursor(row, 0);
  // A freshly-added row never had its height calculated against its
  // own actual content until now -- confirmed as a real, reported bug:
  // with a large grid font specifically (where the gap between a
  // generic/previous row height and what a new row's own content
  // actually needs is large enough to notice), a new row showed only
  // part of its text until something else happened to trigger
  // ReapplyRowHeights() later (a tab switch, in particular -- which is
  // why refreshing appeared to "fix" it). Calling it here means a new
  // row is sized correctly from the moment it appears, not just
  // eventually. Same platform-independent underlying cause on both
  // Windows and macOS, per direct report -- this was never actually a
  // platform-specific rendering issue at all, just a step that was
  // never being run for a brand new row in the first place.
  ReapplyRowHeights();

  if (on_chart_changed) on_chart_changed();

  for (size_t i = 0; i < m_cfg.columns.size(); i++) {
    wxString value = m_table->RawGet(row, static_cast<int>(i));
    for (auto& watch : m_watchedColumnValues) {
      if (std::get<0>(watch) == m_cfg.columns[i].name &&
          std::get<1>(watch) == value && std::get<2>(watch)) {
        std::get<2>(watch)();
      }
    }
  }

  if (on_row_added_external) on_row_added_external(row);
}

void DataTab::WatchColumnValue(const wxString& colName, const wxString& value,
                               std::function<void()> callback) {
  m_watchedColumnValues.emplace_back(colName, value, std::move(callback));
}

void DataTab::OnCellEdited(int row, int col) {
  if (m_cfg.recompute) m_cfg.recompute(this, m_table, row, col);
  SaveCsv();
  UpdateContentMinWidths();
  m_grid->ForceRefresh();
  if (on_chart_changed) on_chart_changed();

  if (col >= 0 && col < static_cast<int>(m_cfg.columns.size())) {
    const wxString& editedColName = m_cfg.columns[col].name;
    wxString newValue = m_table->RawGet(row, col);
    for (auto& watch : m_watchedColumnValues) {
      if (std::get<0>(watch) == editedColName &&
          std::get<1>(watch) == newValue && std::get<2>(watch)) {
        std::get<2>(watch)();
      }
    }
  }
}

void DataTab::OnRowAboutToDelete(int row) {
  wxUnusedVar(row);
  // Nothing persistent to clean up -- the chart overlay is redrawn fresh
  // from GetChartedPoints() whenever on_chart_changed fires (see
  // DeleteSelectedRows()).
}

void DataTab::DeleteSelectedRows() {
  wxArrayInt rows = m_grid->GetSelectedRows();
  std::vector<int> toDelete;
  if (!rows.IsEmpty()) {
    for (size_t i = 0; i < rows.size(); i++) toDelete.push_back(rows[i]);
  } else if (m_grid->GetNumberRows() > 0) {
    toDelete.push_back(m_grid->GetGridCursorRow());
  }
  if (toDelete.empty()) return;

  int answer = wxMessageBox(
      wxString::Format("Delete %zu row(s) from ", toDelete.size()) +
          m_cfg.title +
          "? You can undo this with Cmd+Z if you change "
          "your mind, as long as nothing else is edited first.",
      "Confirm Delete", wxYES_NO | wxICON_WARNING, m_panel);
  if (answer != wxYES) return;

  SaveUndoSnapshot();
  std::sort(toDelete.rbegin(), toDelete.rend());  // descending
  for (int r : toDelete) {
    if (r < 0 || r >= m_table->GetNumberRows()) continue;
    m_table->DeleteRows(static_cast<size_t>(r), 1);
  }
  SaveCsv();
  m_grid->ForceRefresh();
  if (on_chart_changed) on_chart_changed();
}
