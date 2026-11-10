#include "SettingsDialog.h"
#include "resource.h"
#include "Core/Interval.h"

BEGIN_MESSAGE_MAP(SettingsDialog, CDialogEx)
    ON_CBN_SELCHANGE(IDC_INTERVAL_UNIT, &SettingsDialog::OnUnitChanged)
END_MESSAGE_MAP()

SettingsDialog::SettingsDialog(UINT intervalMs, bool fixedPosition, CWnd* parent)
    : CDialogEx(IDD_SETTINGS, parent),
      m_intervalMs(intervalMs >= autoclicker::kMinIntervalMs &&
          intervalMs <= autoclicker::kMaxIntervalMs
          ? intervalMs : autoclicker::kDefaultIntervalMs),
      m_fixedPosition(fixedPosition)
{
}

BOOL SettingsDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    auto* unit = static_cast<CComboBox*>(GetDlgItem(IDC_INTERVAL_UNIT));
    unit->AddString(L"밀리초 (ms)");
    unit->AddString(L"초 (s)");
    unit->SetCurSel(m_previousUnitIndex);
    static_cast<CEdit*>(GetDlgItem(IDC_INTERVAL_EDIT))->SetLimitText(16);
    SetIntervalText(m_intervalMs, m_previousUnitIndex);
    CheckDlgButton(IDC_FIXED_POSITION, m_fixedPosition ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(IDC_STATUS_TEXT, L"설정창을 열면 정지합니다. 적용 후 시작 단축키로 다시 실행하세요.");
    return TRUE;
}

void SettingsDialog::SetIntervalText(UINT intervalMs, int unitIndex)
{
    CString text;
    if (unitIndex == 0)
        text.Format(L"%u", intervalMs);
    else if (intervalMs % 1000 == 0)
        text.Format(L"%u", intervalMs / 1000);
    else
    {
        text.Format(L"%u.%03u", intervalMs / 1000, intervalMs % 1000);
        text.TrimRight(L"0");
    }
    SetDlgItemText(IDC_INTERVAL_EDIT, text);
}

void SettingsDialog::ShowInputError()
{
    MessageBox(
        L"클릭 간격을 올바르게 입력해주세요.\n\n"
        L"밀리초: 1 ~ 3,600,000 사이의 정수\n"
        L"초: 0.001 ~ 3,600 (소수점 이하 최대 3자리)\n\n"
        L"소수점은 마침표(.)를 사용하고, 쉼표나 단위는 입력하지 마세요.",
        L"클릭 간격 확인", MB_OK | MB_ICONWARNING);
    auto* edit = static_cast<CEdit*>(GetDlgItem(IDC_INTERVAL_EDIT));
    edit->SetFocus();
    edit->SetSel(0, -1);
}

void SettingsDialog::OnUnitChanged()
{
    auto* unit = static_cast<CComboBox*>(GetDlgItem(IDC_INTERVAL_UNIT));
    const int selectedUnitIndex = unit->GetCurSel();
    if (selectedUnitIndex == m_previousUnitIndex || selectedUnitIndex == CB_ERR)
        return;

    CString text;
    GetDlgItemText(IDC_INTERVAL_EDIT, text);
    const auto parsed = autoclicker::ParseInterval(
        std::wstring_view(text.GetString(), static_cast<std::size_t>(text.GetLength())),
        m_previousUnitIndex == 0 ? autoclicker::IntervalUnit::Milliseconds
                                 : autoclicker::IntervalUnit::Seconds);
    if (!parsed)
    {
        unit->SetCurSel(m_previousUnitIndex);
        ShowInputError();
        return;
    }
    SetIntervalText(*parsed, selectedUnitIndex);
    m_previousUnitIndex = selectedUnitIndex;
}

void SettingsDialog::OnOK()
{
    CString text;
    GetDlgItemText(IDC_INTERVAL_EDIT, text);
    const auto parsed = autoclicker::ParseInterval(
        std::wstring_view(text.GetString(), static_cast<std::size_t>(text.GetLength())),
        m_previousUnitIndex == 0 ? autoclicker::IntervalUnit::Milliseconds
                                 : autoclicker::IntervalUnit::Seconds);
    if (!parsed)
    {
        ShowInputError();
        return;
    }
    m_intervalMs = *parsed;
    m_fixedPosition = IsDlgButtonChecked(IDC_FIXED_POSITION) == BST_CHECKED;
    CDialogEx::OnOK();
}
