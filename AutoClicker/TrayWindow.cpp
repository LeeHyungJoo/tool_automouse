#include "TrayWindow.h"
#include "SettingsDialog.h"
#include "Core/Interval.h"
#include "resource.h"
#include <shellapi.h>
#include <wtsapi32.h>

namespace
{
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kStartMessage = WM_APP + 2;
constexpr UINT kStopMessage = WM_APP + 3;
constexpr UINT kClickErrorMessage = WM_APP + 4;
constexpr UINT kTrayId = 1;
constexpr LPCWSTR kRegistryPath = L"Software\\MfcAutoClicker\\Settings";
UINT g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}

BEGIN_MESSAGE_MAP(TrayWindow, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_CLOSE()
    ON_WM_ENDSESSION()
    ON_WM_QUERYENDSESSION()
    ON_WM_POWERBROADCAST()
    ON_WM_DISPLAYCHANGE()
    ON_MESSAGE(WM_WTSSESSION_CHANGE, &TrayWindow::OnSessionChange)
    ON_MESSAGE(kTrayMessage, &TrayWindow::OnTrayEvent)
    ON_MESSAGE(kStartMessage, &TrayWindow::OnStartClicking)
    ON_MESSAGE(kStopMessage, &TrayWindow::OnStopClicking)
    ON_MESSAGE(kClickErrorMessage, &TrayWindow::OnClickError)
    ON_REGISTERED_MESSAGE(g_taskbarCreated, &TrayWindow::OnTaskbarCreated)
    ON_COMMAND(ID_TRAY_SETTINGS, &TrayWindow::OnSettings)
    ON_COMMAND(ID_TRAY_STOP, &TrayWindow::OnStopCommand)
    ON_COMMAND(ID_TRAY_EXIT, &TrayWindow::OnExit)
END_MESSAGE_MAP()

TrayWindow::~TrayWindow()
{
    Cleanup();
}

bool TrayWindow::CreateHidden()
{
    const auto windowClass = AfxRegisterWndClass(0);
    if (!CreateEx(WS_EX_TOOLWINDOW, windowClass, L"MFC AutoClicker", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr))
    {
        AfxMessageBox(L"오토클릭 프로그램을 초기화할 수 없습니다.", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

int TrayWindow::OnCreate(LPCREATESTRUCT createStruct)
{
    if (CWnd::OnCreate(createStruct) == -1)
        return -1;
    m_intervalMs = LoadInterval();
    m_idleIcon = LoadIconW(AfxGetInstanceHandle(), MAKEINTRESOURCEW(IDI_AUTOCLICKER_IDLE));
    m_runningIcon = LoadIconW(AfxGetInstanceHandle(), MAKEINTRESOURCEW(IDI_AUTOCLICKER_RUNNING));
    if (!m_idleIcon || !m_runningIcon || !AddTrayIcon())
    {
        AfxMessageBox(L"시스템 트레이 아이콘을 만들 수 없습니다.", MB_OK | MB_ICONERROR);
        return -1;
    }
    m_sessionRegistered = WTSRegisterSessionNotification(GetSafeHwnd(), NOTIFY_FOR_THIS_SESSION) != FALSE;
    if (!m_sessionRegistered)
    {
        ShowSystemError(L"세션 잠금 감지를 등록할 수 없습니다.", GetLastError());
        return -1;
    }
    if (!RefreshHotkeys())
    {
        ShowSystemError(L"전역 단축키를 등록할 수 없습니다.", m_hotkeys.LastError());
        return -1;
    }
    return 0;
}

UINT TrayWindow::LoadInterval()
{
    DWORD value = autoclicker::kDefaultIntervalMs;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegistryPath, L"IntervalMs", RRF_RT_REG_DWORD,
        nullptr, &value, &size) != ERROR_SUCCESS ||
        value < autoclicker::kMinIntervalMs || value > autoclicker::kMaxIntervalMs)
        return autoclicker::kDefaultIntervalMs;
    return value;
}

LONG TrayWindow::SaveInterval(UINT value)
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        return result;
    const DWORD data = value;
    result = RegSetValueExW(key, L"IntervalMs", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(key);
    return result;
}

bool TrayWindow::AddTrayIcon()
{
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = GetSafeHwnd();
    icon.uID = kTrayId;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon.uCallbackMessage = kTrayMessage;
    icon.hIcon = m_running ? m_runningIcon : m_idleIcon;
    wcscpy_s(icon.szTip, L"오토클릭 · 정지");
    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    if (m_trayAdded)
    {
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
        UpdateTrayIcon();
    }
    return m_trayAdded;
}

void TrayWindow::UpdateTrayIcon()
{
    if (!m_trayAdded)
        return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = GetSafeHwnd();
    icon.uID = kTrayId;
    icon.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon.hIcon = m_running ? m_runningIcon : m_idleIcon;
    CString tip;
    if (m_running)
        tip.Format(L"오토클릭 · 실행 중 (%ld, %ld) · %u ms", m_target.x, m_target.y, m_intervalMs);
    else
        tip.Format(L"오토클릭 · 정지 · %u ms", m_intervalMs);
    wcsncpy_s(icon.szTip, tip.GetString(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &icon);
}

void TrayWindow::RemoveTrayIcon() noexcept
{
    if (!m_trayAdded)
        return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = GetSafeHwnd();
    icon.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    m_trayAdded = false;
}

LRESULT TrayWindow::OnTaskbarCreated(WPARAM, LPARAM)
{
    m_trayAdded = false;
    if (!AddTrayIcon())
    {
        StopClicking();
        AfxMessageBox(L"시스템 트레이를 복원할 수 없어 프로그램을 종료합니다.", MB_OK | MB_ICONERROR);
        PostMessage(WM_CLOSE);
    }
    return 0;
}

LRESULT TrayWindow::OnTrayEvent(WPARAM, LPARAM event)
{
    switch (LOWORD(event))
    {
    case WM_CONTEXTMENU:
    case WM_RBUTTONUP:
        ShowTrayMenu();
        break;
    case WM_LBUTTONDBLCLK:
    case NIN_KEYSELECT:
        OnSettings();
        break;
    }
    return 0;
}

void TrayWindow::ShowTrayMenu()
{
    if (m_menuOpen)
        return;
    StopClicking();
    m_menuOpen = true;
    CMenu menu;
    if (menu.CreatePopupMenu())
    {
        CString status;
        status.Format(L"정지됨 · 클릭 간격 %u ms", m_intervalMs);
        menu.AppendMenu(MF_STRING | MF_GRAYED, 0, status);
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, ID_TRAY_SETTINGS, L"설정 (Settings)...");
        menu.AppendMenu(MF_STRING, ID_TRAY_STOP, L"정지 (Alt+C+S)");
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, ID_TRAY_EXIT, L"종료 (Exit)");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow();
        // Return a command so it is handled after the menu loop has closed.
        const UINT command = static_cast<UINT>(menu.TrackPopupMenu(
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, this));
        PostMessage(WM_NULL);
        m_menuOpen = false;
        if (command != 0)
            SendMessage(WM_COMMAND, command);
    }
    m_menuOpen = false;
}

LRESULT TrayWindow::OnStartClicking(WPARAM x, LPARAM y)
{
    if (m_closing || m_sessionLocked || m_sessionDisconnected || m_powerSuspended || !m_hotkeysActive ||
        m_running || m_menuOpen || m_settingsWindow)
        return 0;
    m_target.x = static_cast<LONG>(static_cast<INT_PTR>(x));
    m_target.y = static_cast<LONG>(y);
    if (!m_clicker.Start(m_target, m_intervalMs, GetSafeHwnd(), kClickErrorMessage))
        ShowSystemError(L"오토클릭을 시작할 수 없습니다.", m_clicker.LastError());
    else
    {
        m_running = true;
        UpdateTrayIcon();
    }
    return 0;
}

void TrayWindow::StopClicking()
{
    m_clicker.Stop();
    m_running = false;
    UpdateTrayIcon();
    // A completed worker can have posted an error just before it was stopped.
    // Drain it before a new worker starts so it cannot stop that new session.
    MSG stale{};
    while (PeekMessageW(&stale, GetSafeHwnd(), kClickErrorMessage, kClickErrorMessage, PM_REMOVE))
    {
    }
}

bool TrayWindow::RefreshHotkeys()
{
    if (m_closing || m_sessionLocked || m_sessionDisconnected || m_powerSuspended)
    {
        // A hook cannot observe key releases on another/locked desktop. Join
        // it before clearing messages, then seed fresh physical key state only
        // when this session is available and the machine is awake again.
        m_hotkeys.Stop();
        m_hotkeysActive = false;
        StopClicking();
        DrainHotkeyMessages();
        return true;
    }
    if (m_hotkeysActive)
        return true;
    m_hotkeysActive = m_hotkeys.Start(GetSafeHwnd(), kStartMessage, kStopMessage);
    return m_hotkeysActive;
}

void TrayWindow::DrainHotkeyMessages() noexcept
{
    const HWND owner = GetSafeHwnd();
    if (!owner)
        return;
    MSG stale{};
    // These two private message IDs are contiguous. No old start or stop can
    // cross the session/power boundary and affect a newly installed hook.
    while (PeekMessageW(&stale, owner, kStartMessage, kStopMessage, PM_REMOVE))
    {
    }
}

LRESULT TrayWindow::OnStopClicking(WPARAM, LPARAM)
{
    StopClicking();
    return 0;
}

void TrayWindow::OnStopCommand()
{
    StopClicking();
}

LRESULT TrayWindow::OnClickError(WPARAM error, LPARAM)
{
    StopClicking();
    ShowSystemError(
        L"클릭 입력을 보낼 수 없어 정지했습니다.\n"
        L"대상 프로그램의 관리자 권한 또는 디스플레이 연결 상태를 확인해주세요.",
        static_cast<DWORD>(error));
    return 0;
}

void TrayWindow::OnSettings()
{
    StopClicking();
    if (m_settingsWindow)
    {
        if (IsWindow(m_settingsWindow->GetSafeHwnd()))
            m_settingsWindow->SetForegroundWindow();
        return;
    }
    SettingsDialog settings(m_intervalMs, this);
    m_settingsWindow = &settings;
    const INT_PTR result = settings.DoModal();
    m_settingsWindow = nullptr;
    if (result == IDOK)
    {
        m_intervalMs = settings.IntervalMs();
        const LONG saved = SaveInterval(m_intervalMs);
        UpdateTrayIcon();
        if (saved != ERROR_SUCCESS)
            ShowSystemError(L"설정은 적용했지만 저장하지 못했습니다. 다음 실행 시 이전 값이 사용됩니다.", saved);
    }
}

void TrayWindow::ShowSystemError(LPCWSTR description, DWORD error)
{
    CString message;
    message.Format(L"%s\n\nWindows 오류 코드: %lu", description, error);
    AfxMessageBox(message, MB_OK | MB_ICONERROR);
}

LRESULT TrayWindow::OnSessionChange(WPARAM event, LPARAM)
{
    switch (event)
    {
    case WTS_SESSION_LOCK:
    case WTS_SESSION_LOGOFF:
        m_sessionLocked = true;
        break;
    case WTS_CONSOLE_DISCONNECT:
    case WTS_REMOTE_DISCONNECT:
        m_sessionDisconnected = true;
        break;
    case WTS_SESSION_UNLOCK:
        m_sessionLocked = false;
        break;
    case WTS_CONSOLE_CONNECT:
    case WTS_REMOTE_CONNECT:
        m_sessionDisconnected = false;
        break;
    default:
        return 0;
    }
    if (!RefreshHotkeys())
    {
        m_closing = true;
        StopClicking();
        ShowSystemError(L"전역 단축키를 다시 등록할 수 없어 프로그램을 종료합니다.", m_hotkeys.LastError());
        PostMessage(WM_CLOSE);
    }
    return 0;
}

UINT TrayWindow::OnPowerBroadcast(UINT event, LPARAM)
{
    if (event == PBT_APMSUSPEND)
        m_powerSuspended = true;
    else if (event == PBT_APMRESUMEAUTOMATIC || event == PBT_APMRESUMESUSPEND)
        m_powerSuspended = false;
    else
        return TRUE;

    // Duplicate resume notifications must not recreate an active hook, but
    // neither a duplicate nor a missing suspend notification may resume clicks.
    StopClicking();
    if (!RefreshHotkeys())
    {
        m_closing = true;
        ShowSystemError(L"전역 단축키를 다시 등록할 수 없어 프로그램을 종료합니다.", m_hotkeys.LastError());
        PostMessage(WM_CLOSE);
    }
    return TRUE;
}

void TrayWindow::OnDisplayChange(UINT depth, int width, int height)
{
    StopClicking();
    CWnd::OnDisplayChange(depth, width, height);
}

BOOL TrayWindow::OnQueryEndSession()
{
    StopClicking();
    return TRUE;
}

void TrayWindow::OnEndSession(BOOL ending)
{
    if (ending)
        Cleanup();
}

void TrayWindow::Cleanup() noexcept
{
    m_closing = true;
    m_hotkeys.Stop();
    m_hotkeysActive = false;
    m_clicker.Stop();
    m_running = false;
    DrainHotkeyMessages();
    if (m_sessionRegistered)
    {
        WTSUnRegisterSessionNotification(GetSafeHwnd());
        m_sessionRegistered = false;
    }
    RemoveTrayIcon();
}

void TrayWindow::OnClose()
{
    m_closing = true;
    RefreshHotkeys();
    if (m_settingsWindow && IsWindow(m_settingsWindow->GetSafeHwnd()))
    {
        m_settingsWindow->SendMessage(WM_CLOSE);
        PostMessage(WM_CLOSE);
        return;
    }
    DestroyWindow();
}

void TrayWindow::OnDestroy()
{
    Cleanup();
    CWnd::OnDestroy();
    PostQuitMessage(0);
}

void TrayWindow::OnExit()
{
    PostMessage(WM_CLOSE);
}
