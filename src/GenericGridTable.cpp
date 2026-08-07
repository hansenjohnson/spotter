#include "GenericGridTable.h"
#include "LatLonFormat.h"

GenericGridTable::GenericGridTable(std::vector<ColumnDef> cols)
    : m_cols(std::move(cols)), m_numVisible(0) {
  for (auto& c : m_cols) {
    if (c.type != ColumnDef::HIDDEN) m_numVisible++;
  }
}

int GenericGridTable::GetNumberRows() {
  return static_cast<int>(m_data.size());
}

int GenericGridTable::GetNumberCols() { return m_numVisible; }

bool GenericGridTable::IsEmptyCell(int row, int col) {
  return RawGet(row, col).IsEmpty();
}

wxString GenericGridTable::GetValue(int row, int col) {
  if (col >= 0 && col < static_cast<int>(m_cols.size()) &&
      m_cols[col].type == ColumnDef::BUTTON) {
    return m_cols[col].buttonCaption;
  }
  wxString raw = RawGet(row, col);
  if (col >= 0 && col < static_cast<int>(m_cols.size()) && !raw.IsEmpty()) {
    ColumnDef::GeoRole role = m_cols[col].geoRole;
    if (role != ColumnDef::GeoRole::None) {
      double decDeg = 0.0;
      if (raw.ToDouble(&decDeg)) {
        return LatLonFormat::FormatValue(decDeg,
                                         role == ColumnDef::GeoRole::Latitude);
      }
    }
  }
  return raw;
}

void GenericGridTable::SetValue(int row, int col, const wxString& value) {
  RawSet(row, col, value);
  if (on_cell_edited) on_cell_edited(row, col);
}

wxString GenericGridTable::GetColLabelValue(int col) {
  if (col < 0 || col >= static_cast<int>(m_cols.size())) return wxEmptyString;
  return m_cols[col].name;
}

wxString GenericGridTable::GetTypeName(int row, int col) {
  wxUnusedVar(row);
  if (col < 0 || col >= static_cast<int>(m_cols.size()))
    return wxGRID_VALUE_STRING;
  switch (m_cols[col].type) {
    case ColumnDef::BOOL:
      return wxGRID_VALUE_BOOL;
    case ColumnDef::CHOICE: {
      wxString choiceStr = wxJoin(m_cols[col].choices, ',');
      return wxString::Format("choice:%s", choiceStr);
    }
    default:
      return wxGRID_VALUE_STRING;
  }
}

bool GenericGridTable::CanGetValueAs(int row, int col,
                                     const wxString& typeName) {
  wxUnusedVar(row);
  if (col < 0 || col >= static_cast<int>(m_cols.size())) return false;
  if (typeName == wxGRID_VALUE_BOOL) return m_cols[col].type == ColumnDef::BOOL;
  return typeName == wxGRID_VALUE_STRING;
}

bool GenericGridTable::CanSetValueAs(int row, int col,
                                     const wxString& typeName) {
  return CanGetValueAs(row, col, typeName);
}

bool GenericGridTable::GetValueAsBool(int row, int col) {
  return RawGet(row, col) == "1";
}

void GenericGridTable::SetValueAsBool(int row, int col, bool value) {
  RawSet(row, col, value ? "1" : "0");
  if (on_cell_edited) on_cell_edited(row, col);
}

bool GenericGridTable::AppendRows(size_t numRows) {
  for (size_t i = 0; i < numRows; i++) {
    std::vector<wxString> row(m_cols.size(), wxEmptyString);
    m_data.push_back(row);
    int newRowIdx = static_cast<int>(m_data.size()) - 1;
    if (GetView()) {
      wxGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_APPENDED, 1);
      GetView()->ProcessTableMessage(msg);
    }
    if (on_row_added) on_row_added(newRowIdx);
  }
  return true;
}

bool GenericGridTable::DeleteRows(size_t pos, size_t numRows) {
  if (pos >= m_data.size()) return false;
  size_t end = std::min(pos + numRows, m_data.size());
  for (size_t r = end; r-- > pos;) {
    if (on_row_about_to_delete) on_row_about_to_delete(static_cast<int>(r));
    m_data.erase(m_data.begin() + r);
  }
  if (GetView()) {
    wxGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_DELETED,
                           static_cast<int>(pos), static_cast<int>(end - pos));
    GetView()->ProcessTableMessage(msg);
  }
  return true;
}

wxString GenericGridTable::RawGet(int row, int dataCol) const {
  if (row < 0 || row >= static_cast<int>(m_data.size())) return wxEmptyString;
  if (dataCol < 0 || dataCol >= static_cast<int>(m_data[row].size()))
    return wxEmptyString;
  return m_data[row][dataCol];
}

void GenericGridTable::RawSet(int row, int dataCol, const wxString& value) {
  if (row < 0 || row >= static_cast<int>(m_data.size())) return;
  if (dataCol < 0 || dataCol >= static_cast<int>(m_data[row].size())) return;
  m_data[row][dataCol] = value;
}

int GenericGridTable::AppendDataRow(const std::vector<wxString>& values) {
  std::vector<wxString> row(m_cols.size(), wxEmptyString);
  for (size_t i = 0; i < values.size() && i < row.size(); i++) {
    row[i] = values[i];
  }
  m_data.push_back(row);
  return static_cast<int>(m_data.size()) - 1;
}

void GenericGridTable::NotifyGridRowCountChanged(int oldRowCount) {
  if (!GetView()) return;  // no grid attached yet (e.g. still loading)
  int newRowCount = GetNumberRows();
  if (newRowCount > oldRowCount) {
    wxGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_APPENDED,
                           newRowCount - oldRowCount);
    GetView()->ProcessTableMessage(msg);
  } else if (newRowCount < oldRowCount) {
    wxGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_DELETED, 0,
                           oldRowCount - newRowCount);
    GetView()->ProcessTableMessage(msg);
  }
}

int GenericGridTable::FindColByName(const wxString& name) const {
  for (size_t i = 0; i < m_cols.size(); i++) {
    if (m_cols[i].name == name) return static_cast<int>(i);
  }
  return -1;
}
