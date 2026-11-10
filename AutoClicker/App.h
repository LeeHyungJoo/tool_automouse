#pragma once

#include "targetver.h"
#include <afxwin.h>
#include <memory>

class TrayWindow;

class CAutoClickerApp final : public CWinApp
{
public:
    CAutoClickerApp();
    ~CAutoClickerApp() override;
    BOOL InitInstance() override;
    int ExitInstance() override;

private:
    HANDLE m_instanceMutex = nullptr;
    std::unique_ptr<TrayWindow> m_window;
};
