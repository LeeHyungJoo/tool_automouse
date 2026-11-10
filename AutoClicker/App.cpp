#include "App.h"
#include "TrayWindow.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

CAutoClickerApp theApp;

CAutoClickerApp::CAutoClickerApp() = default;
CAutoClickerApp::~CAutoClickerApp() = default;

BOOL CAutoClickerApp::InitInstance()
{
    CWinApp::InitInstance();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    AfxEnableControlContainer();

    m_instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\MfcAutoClicker.Singleton.v1");
    const DWORD mutexError = GetLastError();
    if (!m_instanceMutex)
    {
        AfxMessageBox(L"프로그램의 중복 실행 여부를 확인할 수 없습니다.", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    if (mutexError == ERROR_ALREADY_EXISTS)
    {
        AfxMessageBox(L"오토클릭이 이미 실행 중입니다. 시스템 트레이 아이콘을 확인해주세요.",
            MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    m_window = std::make_unique<TrayWindow>();
    if (!m_window->CreateHidden())
        return FALSE;
    m_pMainWnd = m_window.get();
    // The hidden top-level window owns the tray icon and keeps MFC's message
    // loop alive after each settings dialog closes.
    return TRUE;
}

int CAutoClickerApp::ExitInstance()
{
    if (m_window && IsWindow(m_window->GetSafeHwnd()))
        m_window->DestroyWindow();
    m_pMainWnd = nullptr;
    m_window.reset();
    if (m_instanceMutex)
    {
        CloseHandle(m_instanceMutex);
        m_instanceMutex = nullptr;
    }
    return CWinApp::ExitInstance();
}
