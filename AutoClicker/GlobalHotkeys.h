#pragma once

#include "targetver.h"
#include <Windows.h>

#include <atomic>
#include <future>
#include <thread>

#include "Core/ChordState.h"

// Owns a low-level keyboard hook on a dedicated message-loop thread. Call
// Start/Stop/destruction serially from the application's owner/UI thread;
// LastError may be read from any thread. The owner HWND must remain alive until
// Stop returns. Stop joins the hook thread, so no callback can outlive this.
//
// Hold Alt+C, then press P to start or S to stop. Ctrl/Shift/Win combinations
// (including AltGr) are excluded. Only recognized P/S down/repeat/up events are
// consumed; the Alt+C prefix still reaches the foreground application.
//
// startMessage receives the captured signed desktop x in WPARAM and signed y
// in LPARAM (decode x via static_cast<INT_PTR>). stopMessage has zero arguments.
class GlobalHotkeys final {
public:
    GlobalHotkeys() = default;
    ~GlobalHotkeys();

    GlobalHotkeys(const GlobalHotkeys&) = delete;
    GlobalHotkeys& operator=(const GlobalHotkeys&) = delete;

    bool Start(HWND owner, UINT startMessage, UINT stopMessage);
    void Stop() noexcept;
    DWORD LastError() const noexcept;

private:
    void ThreadMain(std::promise<DWORD> ready) noexcept;
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) noexcept;
    static DWORD NormalizeKey(const KBDLLHOOKSTRUCT& event) noexcept;
    void RecordError(DWORD error) noexcept;

    static thread_local GlobalHotkeys* current_;

    HWND owner_ = nullptr;
    UINT startMessage_ = 0;
    UINT stopMessage_ = 0;
    DWORD threadId_ = 0;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    std::atomic<DWORD> lastError_{ ERROR_SUCCESS };
    autoclicker::ChordState state_;
};
