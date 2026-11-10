#include "ClickEngine.h"
#include "Core/Interval.h"
#include <chrono>
#include <system_error>

namespace
{
bool KeyHeld(int key) noexcept
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

LONG NormalizeCoordinate(LONG coordinate, int origin, int extent) noexcept
{
    // Map to the centre of a pixel in the complete virtual desktop, including
    // displays with negative coordinates. Avoid 32-bit multiplication overflow.
    const auto offset = static_cast<long long>(coordinate) - origin;
    const auto normalized = (offset * 65536 + 32768) / extent;
    return static_cast<LONG>(normalized < 0 ? 0 : normalized > 65535 ? 65535 : normalized);
}
}

ClickEngine::~ClickEngine()
{
    Stop();
}

bool ClickEngine::Start(POINT target, UINT intervalMs, HWND owner, UINT errorMessage)
{
    Stop();
    if (intervalMs < autoclicker::kMinIntervalMs || intervalMs > autoclicker::kMaxIntervalMs)
    {
        m_lastError = ERROR_INVALID_PARAMETER;
        return false;
    }
    m_lastError = ERROR_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = false;
    }
    try
    {
        m_worker = std::thread(&ClickEngine::Run, this, target, intervalMs, owner, errorMessage);
    }
    catch (const std::system_error&)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_lastError = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    return true;
}

void ClickEngine::Stop() noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

bool ClickEngine::ModifiersHeld() noexcept
{
    return KeyHeld(VK_MENU) || KeyHeld(VK_CONTROL) || KeyHeld(VK_SHIFT) ||
        KeyHeld(VK_LWIN) || KeyHeld(VK_RWIN);
}

DWORD ClickEngine::SendClick(POINT target) noexcept
{
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0 || !MonitorFromPoint(target, MONITOR_DEFAULTTONULL))
        return ERROR_INVALID_MONITOR_HANDLE;

    INPUT input[3]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dx = NormalizeCoordinate(target.x, GetSystemMetrics(SM_XVIRTUALSCREEN), width);
    input[0].mi.dy = NormalizeCoordinate(target.y, GetSystemMetrics(SM_YVIRTUALSCREEN), height);
    input[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[2].type = INPUT_MOUSE;
    input[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SetLastError(ERROR_SUCCESS);
    if (SendInput(3, input, sizeof(INPUT)) == 3)
        return ERROR_SUCCESS;

    // SendInput may insert only part of a batch. Release a possibly inserted
    // button-down before reporting failure so it cannot remain stuck.
    const DWORD error = GetLastError();
    INPUT release{};
    release.type = INPUT_MOUSE;
    release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &release, sizeof(INPUT));
    return error == ERROR_SUCCESS ? ERROR_ACCESS_DENIED : error;
}

void ClickEngine::Run(POINT target, UINT intervalMs, HWND owner, UINT errorMessage) noexcept
{
    bool awaitingStartRelease = true;
    DWORD failure = ERROR_SUCCESS;
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_stop)
    {
        if (m_wake.wait_for(lock, std::chrono::milliseconds(intervalMs), [this] { return m_stop; }))
            break;
        if (awaitingStartRelease)
        {
            if (ModifiersHeld() || KeyHeld('C') || KeyHeld('P') || KeyHeld('S'))
                continue;
            awaitingStartRelease = false;
        }
        // Do not inject modified clicks or interrupt a physical mouse drag.
        // Timing remains best-effort under the Windows scheduler.
        if (ModifiersHeld() || KeyHeld(VK_LBUTTON))
            continue;

        // Keep the stop lock while inserting the short batch. After Stop()
        // returns, all input insertion and the worker have completed.
        failure = SendClick(target);
        if (failure != ERROR_SUCCESS)
        {
            m_lastError = failure;
            m_stop = true;
        }
    }
    lock.unlock();
    if (failure != ERROR_SUCCESS)
        PostMessageW(owner, errorMessage, static_cast<WPARAM>(failure), 0);
}
