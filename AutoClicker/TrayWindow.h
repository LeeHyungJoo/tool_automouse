#pragma once

#include "targetver.h"
#include <afxwin.h>
#include "ClickEngine.h"
#include "GlobalHotkeys.h"

class TrayWindow final : public CWnd
{
public:
    TrayWindow() = default;
    ~TrayWindow() override;
    bool CreateHidden();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT createStruct);
    afx_msg void OnDestroy();
    afx_msg void OnClose();
    afx_msg void OnEndSession(BOOL ending);
    afx_msg BOOL OnQueryEndSession();
    afx_msg UINT OnPowerBroadcast(UINT event, LPARAM data);
    afx_msg void OnDisplayChange(UINT depth, int width, int height);
    afx_msg LRESULT OnSessionChange(WPARAM event, LPARAM session);
    afx_msg LRESULT OnTrayEvent(WPARAM, LPARAM event);
    afx_msg LRESULT OnTaskbarCreated(WPARAM, LPARAM);
    afx_msg LRESULT OnStartClicking(WPARAM, LPARAM);
    afx_msg LRESULT OnStopClicking(WPARAM, LPARAM);
    afx_msg LRESULT OnClickError(WPARAM error, LPARAM);
    afx_msg void OnSettings();
    afx_msg void OnStopCommand();
    afx_msg void OnExit();
    DECLARE_MESSAGE_MAP()

private:
    bool AddTrayIcon();
    void UpdateTrayIcon();
    void RemoveTrayIcon() noexcept;
    void ShowTrayMenu();
    void StopClicking();
    bool RefreshHotkeys();
    void DrainHotkeyMessages() noexcept;
    void Cleanup() noexcept;
    static UINT LoadInterval();
    static bool LoadFixedPosition();
    static LONG SaveSettings(UINT intervalMs, bool fixedPosition);
    static void ShowSystemError(LPCWSTR description, DWORD error);

    GlobalHotkeys m_hotkeys;
    ClickEngine m_clicker;
    UINT m_intervalMs = 1000;
    bool m_fixedPosition = false;
    HICON m_idleIcon = nullptr;
    HICON m_runningIcon = nullptr;
    bool m_running = false;
    bool m_trayAdded = false;
    bool m_sessionRegistered = false;
    bool m_hotkeysActive = false;
    bool m_sessionLocked = false;
    bool m_sessionDisconnected = false;
    bool m_powerSuspended = false;
    bool m_closing = false;
    bool m_menuOpen = false;
    CWnd* m_settingsWindow = nullptr;
};
