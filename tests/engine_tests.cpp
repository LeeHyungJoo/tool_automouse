#include "../AutoClicker/ClickEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace fake
{
struct PostedMessage { HWND owner; UINT message; WPARAM error; };
std::array<std::atomic<bool>, 256> keys{};
std::mutex mutex;
std::condition_variable changed;
std::vector<std::vector<INPUT>> batches;
std::vector<POINT> clicks;
std::vector<PostedMessage> messages;
POINT cursor{-1000, 100};
std::size_t moveRequests = 0;
std::atomic<UINT> inputLimit{3};
std::atomic<DWORD> inputError{ERROR_SUCCESS};
std::atomic<bool> captureSucceeds{true};
std::atomic<DWORD> captureError{ERROR_SUCCESS};
std::atomic<unsigned int> captureCalls{0};
std::atomic<unsigned int> metricCalls{0};
std::atomic<unsigned int> monitorCalls{0};
std::atomic<bool> monitorPresent{true};
std::atomic<int> desktopWidth{3840};
thread_local DWORD lastError = ERROR_SUCCESS;

void Reset()
{
    for (auto& key : keys)
        key = false;
    std::lock_guard<std::mutex> lock(mutex);
    batches.clear();
    clicks.clear();
    messages.clear();
    cursor = {-1000, 100};
    moveRequests = 0;
    inputLimit = 3;
    inputError = ERROR_SUCCESS;
    captureSucceeds = true;
    captureError = ERROR_SUCCESS;
    captureCalls = metricCalls = monitorCalls = 0;
    monitorPresent = true;
    desktopWidth = 3840;
}

std::size_t BatchCount()
{
    std::lock_guard<std::mutex> lock(mutex);
    return batches.size();
}

bool WaitForBatches(std::size_t minimum)
{
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 1s, [minimum] { return batches.size() >= minimum; });
}

// Moving the physical cursor and recording the next expected click index are
// atomic relative to SendInput, so coordinate checks need no timing sleeps.
std::size_t MoveCursor(POINT point)
{
    std::lock_guard<std::mutex> lock(mutex);
    cursor = point;
    return clicks.size();
}

bool WaitForClickAt(POINT point, std::size_t since)
{
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 1s, [point, since] {
        return std::any_of(clicks.begin() + static_cast<std::ptrdiff_t>(since), clicks.end(),
            [point](POINT click) { return click.x == point.x && click.y == point.y; });
    });
}

bool WaitForError()
{
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 1s, [] { return !messages.empty(); });
}
}

short GetAsyncKeyState(int key)
{
    return fake::keys.at(static_cast<std::size_t>(key)) ? static_cast<short>(0x8000) : 0;
}

BOOL GetCursorPos(POINT* point)
{
    ++fake::captureCalls;
    if (!fake::captureSucceeds)
    {
        fake::lastError = fake::captureError;
        return 0;
    }
    std::lock_guard<std::mutex> lock(fake::mutex);
    *point = fake::cursor;
    return 1;
}

int GetSystemMetrics(int metric)
{
    ++fake::metricCalls;
    switch (metric)
    {
    case SM_XVIRTUALSCREEN: return -1920;
    case SM_YVIRTUALSCREEN: return -200;
    case SM_CXVIRTUALSCREEN: return fake::desktopWidth;
    case SM_CYVIRTUALSCREEN: return 1280;
    default: throw std::runtime_error("Unexpected metric requested");
    }
}

HMONITOR MonitorFromPoint(POINT point, DWORD)
{
    ++fake::monitorCalls;
    const bool withinDesktop = point.x >= -1920 && point.x < 1920 &&
        point.y >= -200 && point.y < 1080;
    return fake::monitorPresent && withinDesktop ? reinterpret_cast<HMONITOR>(1) : nullptr;
}

void SetLastError(DWORD error) { fake::lastError = error; }
DWORD GetLastError() { return fake::lastError; }

UINT SendInput(UINT count, INPUT* input, int size)
{
    if (size != sizeof(INPUT))
        throw std::runtime_error("Incorrect INPUT size");
    const UINT inserted = std::min(count, fake::inputLimit.load());
    {
        std::lock_guard<std::mutex> lock(fake::mutex);
        fake::batches.emplace_back(input, input + count);
        for (UINT index = 0; index < count; ++index)
        {
            if ((input[index].mi.dwFlags & MOUSEEVENTF_MOVE) != 0)
            {
                ++fake::moveRequests;
                if (index < inserted)
                {
                    // Model Windows' absolute pixel mapping independently of
                    // the engine's pixel-centre normalization formula.
                    fake::cursor.x = -1920 + static_cast<LONG>(
                        static_cast<long long>(input[index].mi.dx) * fake::desktopWidth / 65536);
                    fake::cursor.y = -200 + static_cast<LONG>(
                        static_cast<long long>(input[index].mi.dy) * 1280 / 65536);
                }
            }
            if (index < inserted && (input[index].mi.dwFlags & MOUSEEVENTF_LEFTDOWN) != 0)
                fake::clicks.push_back(fake::cursor);
        }
    }
    fake::changed.notify_all();
    fake::lastError = fake::inputError;
    return inserted;
}

BOOL PostMessageW(HWND owner, UINT message, WPARAM error, LPARAM)
{
    {
        std::lock_guard<std::mutex> lock(fake::mutex);
        fake::messages.push_back({owner, message, error});
    }
    fake::changed.notify_all();
    return 1;
}

namespace
{
unsigned int checks = 0;
const HWND kOwner = reinterpret_cast<HWND>(1);
constexpr UINT kErrorMessage = 0x8004;
constexpr POINT kInitial{-1000, 100};

void Require(bool condition, const char* description)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(description);
}

void ExpectNoMoreInput(std::size_t count)
{
    std::this_thread::sleep_for(25ms);
    Require(fake::BatchCount() == count, "Unexpected input while stopped or guarded");
}

void TestInterruptibleStop(bool fixedPosition)
{
    fake::Reset();
    ClickEngine engine;
    Require(engine.Start(3'600'000, fixedPosition, kOwner, kErrorMessage), "Long interval start failed");
    std::this_thread::sleep_for(10ms);
    const auto start = std::chrono::steady_clock::now();
    engine.Stop();
    Require(std::chrono::steady_clock::now() - start < 500ms, "Stop waited for the interval");
    Require(fake::BatchCount() == 0, "Long interval unexpectedly clicked immediately");
    engine.Stop(); // Idempotent cleanup.
    Require(!engine.Start(0, fixedPosition, kOwner, kErrorMessage), "Zero interval accepted");
    Require(engine.LastError() == ERROR_INVALID_PARAMETER, "Wrong interval error");
    Require(!engine.Start(3'600'001, fixedPosition, kOwner, kErrorMessage), "Oversized interval accepted");
}

void TestFollowMovingCursor()
{
    fake::Reset();
    ClickEngine engine;
    Require(engine.Start(5, false, kOwner, kErrorMessage), "Follow start failed");
    Require(fake::WaitForClickAt(kInitial, 0), "Initial follow click missing");
    // Include a monitor to the left/above the origin and cross to the right.
    for (POINT point : {POINT{250, 300}, POINT{-1700, -120}, POINT{1500, 900}})
    {
        const auto since = fake::MoveCursor(point);
        Require(fake::WaitForClickAt(point, since), "Follow click did not use the new cursor position");
        std::lock_guard<std::mutex> lock(fake::mutex);
        Require(fake::cursor.x == point.x && fake::cursor.y == point.y, "Follow mode moved the cursor");
    }
    engine.Stop();
    ExpectNoMoreInput(fake::BatchCount());
    Require(fake::captureCalls == 0, "Follow mode captured a fixed cursor position");
    Require(fake::metricCalls == 0 && fake::monitorCalls == 0, "Follow mode queried desktop geometry");
    std::lock_guard<std::mutex> lock(fake::mutex);
    Require(fake::moveRequests == 0, "Follow mode requested cursor movement");
    for (const auto& batch : fake::batches)
    {
        Require(batch.size() == 2 && batch[0].type == INPUT_MOUSE && batch[1].type == INPUT_MOUSE,
            "Follow click must contain only two mouse events");
        Require(batch[0].mi.dwFlags == MOUSEEVENTF_LEFTDOWN && batch[1].mi.dwFlags == MOUSEEVENTF_LEFTUP,
            "Follow click must contain only button-down/button-up flags");
        Require(batch[0].mi.dx == 0 && batch[0].mi.dy == 0 && batch[1].mi.dx == 0 && batch[1].mi.dy == 0,
            "Follow click contains synthetic coordinates");
    }
}

void TestFixedCaptureAndNormalization()
{
    fake::Reset();
    ClickEngine engine;
    Require(engine.Start(5, true, kOwner, kErrorMessage), "Fixed start failed");
    Require(fake::WaitForClickAt(kInitial, 0), "Initial fixed click missing");
    const auto since = fake::MoveCursor({1500, 900});
    Require(fake::WaitForClickAt(kInitial, since), "Fixed mode did not return to the captured start position");
    engine.Stop();
    ExpectNoMoreInput(fake::BatchCount());
    Require(fake::captureCalls == 1, "Fixed position was not captured exactly once");
    std::lock_guard<std::mutex> lock(fake::mutex);
    for (const auto& batch : fake::batches)
    {
        Require(batch.size() == 3, "Fixed click is missing the move/down/up batch");
        Require(batch[0].mi.dx == 15709 && batch[0].mi.dy == 15385,
            "Incorrect negative-desktop pixel normalization");
        Require(batch[0].mi.dwFlags == (MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK),
            "Fixed move is missing virtual desktop absolute flags");
        Require(batch[1].mi.dwFlags == MOUSEEVENTF_LEFTDOWN && batch[2].mi.dwFlags == MOUSEEVENTF_LEFTUP,
            "Incorrect fixed-mode button sequence");
    }
}

void TestStartReleaseAndRunningGuards(bool fixedPosition)
{
    ClickEngine engine;
    for (int key : std::array<int, 4>{VK_MENU, 'C', 'P', 'S'})
    {
        fake::Reset();
        fake::keys[static_cast<std::size_t>(key)] = true;
        Require(engine.Start(5, fixedPosition, kOwner, kErrorMessage), "Guarded start failed");
        ExpectNoMoreInput(0);
        fake::keys[static_cast<std::size_t>(key)] = false;
        Require(fake::WaitForBatches(1), "Did not click after chord release");
        engine.Stop();
    }

    fake::Reset();
    Require(engine.Start(5, fixedPosition, kOwner, kErrorMessage), "Running-guard start failed");
    Require(fake::WaitForBatches(1), "Running-guard initial click missing");
    for (int key : {VK_MENU, VK_CONTROL, VK_SHIFT, VK_LWIN, VK_RWIN, VK_LBUTTON})
    {
        fake::keys[static_cast<std::size_t>(key)] = true;
        // Let any sample from just before key-down finish before counting.
        std::this_thread::sleep_for(15ms);
        const auto count = fake::BatchCount();
        ExpectNoMoreInput(count);
        fake::keys[static_cast<std::size_t>(key)] = false;
        Require(fake::WaitForBatches(count + 1), "Did not resume after modifier/button release");
    }
    engine.Stop();
    ExpectNoMoreInput(fake::BatchCount());
}

void TestInputFailure(bool fixedPosition, UINT inputLimit, DWORD osError, DWORD expectedError)
{
    fake::Reset();
    fake::inputLimit = inputLimit;
    fake::inputError = osError;
    ClickEngine engine;
    Require(engine.Start(1, fixedPosition, kOwner, kErrorMessage), "Failure-test start failed");
    Require(fake::WaitForError(), "Input failure was not posted");
    engine.Stop();
    Require(engine.LastError() == expectedError, "Wrong stored input error");
    ExpectNoMoreInput(2);
    std::lock_guard<std::mutex> lock(fake::mutex);
    Require(fake::batches.size() == 2 && fake::batches[0].size() == (fixedPosition ? 3u : 2u),
        "Failure retried or used the wrong mode's input count");
    Require(fake::batches[1].size() == 1 && fake::batches[1][0].mi.dwFlags == MOUSEEVENTF_LEFTUP,
        "Missing recovery button release");
    Require(fake::messages.size() == 1 && fake::messages[0].owner == kOwner &&
        fake::messages[0].message == kErrorMessage && fake::messages[0].error == expectedError,
        "Incorrect failure notification");
}

void TestCaptureFailureOnlyAffectsFixedMode()
{
    for (DWORD osError : {DWORD{1234}, ERROR_SUCCESS})
    {
        fake::Reset();
        fake::captureSucceeds = false;
        fake::captureError = osError;
        ClickEngine engine;
        Require(!engine.Start(1, true, kOwner, kErrorMessage), "Fixed start ignored cursor capture failure");
        Require(engine.LastError() == (osError == ERROR_SUCCESS ? ERROR_ACCESS_DENIED : osError),
            "Wrong cursor capture error");
        Require(fake::captureCalls == 1 && fake::BatchCount() == 0, "Failed capture started sending input");
        fake::monitorPresent = false;
        fake::desktopWidth = 0;
        Require(engine.Start(1, false, kOwner, kErrorMessage), "Follow mode incorrectly depends on cursor capture");
        Require(fake::WaitForClickAt(kInitial, 0), "Follow mode incorrectly depends on monitor metadata");
        engine.Stop();
        Require(fake::captureCalls == 1 && fake::metricCalls == 0 && fake::monitorCalls == 0,
            "Follow mode called fixed-position APIs");
        Require(engine.LastError() == ERROR_SUCCESS, "Follow restart retained the old capture error");
    }
}

void TestFixedUnavailableDesktop()
{
    for (bool missingMonitor : {false, true})
    {
        fake::Reset();
        if (missingMonitor)
            fake::monitorPresent = false;
        else
            fake::desktopWidth = 0;
        ClickEngine engine;
        Require(engine.Start(1, true, kOwner, kErrorMessage), "Desktop-failure start failed");
        Require(fake::WaitForError(), "Unavailable desktop did not notify");
        engine.Stop();
        Require(engine.LastError() == ERROR_INVALID_MONITOR_HANDLE, "Wrong desktop error");
        Require(fake::BatchCount() == 0, "Fixed input sent to an unavailable desktop");
    }
}
}

int main()
{
    try
    {
        TestFollowMovingCursor();
        TestFixedCaptureAndNormalization();
        for (bool fixedPosition : {false, true})
        {
            TestInterruptibleStop(fixedPosition);
            TestStartReleaseAndRunningGuards(fixedPosition);
            TestInputFailure(fixedPosition, fixedPosition ? 2u : 1u, 123, 123);
            TestInputFailure(fixedPosition, 0, ERROR_SUCCESS, ERROR_ACCESS_DENIED);
        }
        TestCaptureFailureOnlyAffectsFixedMode();
        TestFixedUnavailableDesktop();
        std::cout << "Click engine tests passed (" << checks << " checks).\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Click engine test failed: " << error.what() << '\n';
        return 1;
    }
}
