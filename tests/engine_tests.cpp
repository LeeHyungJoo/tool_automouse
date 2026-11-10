#include "../AutoClicker/ClickEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
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
std::vector<PostedMessage> messages;
std::atomic<UINT> inputLimit{3};
std::atomic<DWORD> inputError{ERROR_SUCCESS};
std::atomic<bool> monitorPresent{true};
std::atomic<int> desktopWidth{3840};
thread_local DWORD lastError = ERROR_SUCCESS;

void Reset()
{
    for (auto& key : keys)
        key = false;
    std::lock_guard<std::mutex> lock(mutex);
    batches.clear();
    messages.clear();
    inputLimit = 3;
    inputError = ERROR_SUCCESS;
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

int GetSystemMetrics(int metric)
{
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
    {
        std::lock_guard<std::mutex> lock(fake::mutex);
        fake::batches.emplace_back(input, input + count);
    }
    fake::changed.notify_all();
    fake::lastError = fake::inputError;
    return count < fake::inputLimit ? count : fake::inputLimit.load();
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
constexpr POINT kTarget{-1000, 100};

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

void TestInterruptibleStop()
{
    fake::Reset();
    ClickEngine engine;
    Require(engine.Start(kTarget, 3'600'000, kOwner, kErrorMessage), "Long interval start failed");
    std::this_thread::sleep_for(10ms);
    const auto start = std::chrono::steady_clock::now();
    engine.Stop();
    Require(std::chrono::steady_clock::now() - start < 500ms, "Stop waited for the interval");
    Require(fake::BatchCount() == 0, "Long interval unexpectedly clicked immediately");
    engine.Stop(); // Idempotent cleanup.
    Require(!engine.Start(kTarget, 0, kOwner, kErrorMessage), "Zero interval accepted");
    Require(engine.LastError() == ERROR_INVALID_PARAMETER, "Wrong interval error");
    Require(!engine.Start(kTarget, 3'600'001, kOwner, kErrorMessage), "Oversized interval accepted");
}

void TestInputAndNormalization()
{
    fake::Reset();
    ClickEngine engine;
    Require(engine.Start(kTarget, 5, kOwner, kErrorMessage), "Start failed");
    Require(fake::WaitForBatches(3), "No repeated clicks");
    engine.Stop();
    const auto count = fake::BatchCount();
    ExpectNoMoreInput(count);
    std::lock_guard<std::mutex> lock(fake::mutex);
    for (const auto& batch : fake::batches)
    {
        Require(batch.size() == 3, "A click was not a complete move/down/up batch");
        Require(batch[0].mi.dx == 15709 && batch[0].mi.dy == 15385,
            "Incorrect negative-desktop pixel normalization");
        Require(batch[0].mi.dwFlags == (MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK),
            "Move is missing virtual desktop absolute flags");
        Require(batch[1].mi.dwFlags == MOUSEEVENTF_LEFTDOWN &&
            batch[2].mi.dwFlags == MOUSEEVENTF_LEFTUP, "Incorrect button sequence");
    }
}

void TestStartReleaseAndRunningGuards()
{
    ClickEngine engine;
    for (int key : std::array<int, 4>{VK_MENU, 'C', 'P', 'S'})
    {
        fake::Reset();
        fake::keys[static_cast<std::size_t>(key)] = true;
        Require(engine.Start(kTarget, 5, kOwner, kErrorMessage), "Guarded start failed");
        ExpectNoMoreInput(0);
        fake::keys[static_cast<std::size_t>(key)] = false;
        Require(fake::WaitForBatches(1), "Did not click after chord release");
        engine.Stop();
    }

    fake::Reset();
    Require(engine.Start(kTarget, 5, kOwner, kErrorMessage), "Running-guard start failed");
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

void TestInputFailure(UINT inputLimit, DWORD osError, DWORD expectedError)
{
    fake::Reset();
    fake::inputLimit = inputLimit;
    fake::inputError = osError;
    ClickEngine engine;
    Require(engine.Start(kTarget, 1, kOwner, kErrorMessage), "Failure-test start failed");
    Require(fake::WaitForError(), "Input failure was not posted");
    engine.Stop();
    Require(engine.LastError() == expectedError, "Wrong stored input error");
    ExpectNoMoreInput(2);
    std::lock_guard<std::mutex> lock(fake::mutex);
    Require(fake::batches.size() == 2, "Failure retried instead of stopping");
    Require(fake::batches[1].size() == 1 &&
        fake::batches[1][0].mi.dwFlags == MOUSEEVENTF_LEFTUP, "Missing recovery button release");
    Require(fake::messages.size() == 1 && fake::messages[0].owner == kOwner &&
        fake::messages[0].message == kErrorMessage && fake::messages[0].error == expectedError,
        "Incorrect failure notification");
}

void TestUnavailableDesktop()
{
    for (bool missingMonitor : {false, true})
    {
        fake::Reset();
        if (missingMonitor)
            fake::monitorPresent = false;
        else
            fake::desktopWidth = 0;
        ClickEngine engine;
        Require(engine.Start(kTarget, 1, kOwner, kErrorMessage), "Desktop-failure start failed");
        Require(fake::WaitForError(), "Unavailable desktop did not notify");
        engine.Stop();
        Require(engine.LastError() == ERROR_INVALID_MONITOR_HANDLE, "Wrong desktop error");
        Require(fake::BatchCount() == 0, "Input sent to an unavailable desktop");
    }
}
}

int main()
{
    try
    {
        TestInterruptibleStop();
        TestInputAndNormalization();
        TestStartReleaseAndRunningGuards();
        TestInputFailure(2, 123, 123);
        TestInputFailure(0, ERROR_SUCCESS, ERROR_ACCESS_DENIED);
        TestUnavailableDesktop();
        std::cout << "Click engine tests passed (" << checks << " checks).\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Click engine test failed: " << error.what() << '\n';
        return 1;
    }
}
