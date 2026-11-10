#pragma once

#include "targetver.h"
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Owns one interruptible worker. Start/Stop are called by the UI thread only.
class ClickEngine final
{
public:
    ClickEngine() = default;
    ~ClickEngine();
    ClickEngine(const ClickEngine&) = delete;
    ClickEngine& operator=(const ClickEngine&) = delete;

    bool Start(UINT intervalMs, bool fixedPosition, HWND owner, UINT errorMessage);
    void Stop() noexcept;
    DWORD LastError() const noexcept { return m_lastError.load(); }

private:
    void Run(POINT target, bool fixedPosition, UINT intervalMs, HWND owner, UINT errorMessage) noexcept;
    static bool ModifiersHeld() noexcept;
    static DWORD SendClick(POINT target, bool fixedPosition) noexcept;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_worker;
    bool m_stop = true;
    std::atomic<DWORD> m_lastError{ERROR_SUCCESS};
};
