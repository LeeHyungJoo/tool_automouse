#pragma once

#include "targetver.h"
#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>

class SettingsDialog final : public CDialogEx
{
public:
    explicit SettingsDialog(UINT intervalMs, CWnd* parent = nullptr);
    UINT IntervalMs() const noexcept { return m_intervalMs; }

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    afx_msg void OnUnitChanged();
    DECLARE_MESSAGE_MAP()

private:
    void SetIntervalText(UINT intervalMs, int unitIndex);
    void ShowInputError();

    UINT m_intervalMs;
    int m_previousUnitIndex = 0;
};
