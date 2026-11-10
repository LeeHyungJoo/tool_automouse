#include "GlobalHotkeys.h"

#include <utility>

thread_local GlobalHotkeys* GlobalHotkeys::current_ = nullptr;

GlobalHotkeys::~GlobalHotkeys()
{
    Stop();
}

bool GlobalHotkeys::Start(HWND owner, UINT startMessage, UINT stopMessage)
{
    if (thread_.joinable()) {
        RecordError(ERROR_ALREADY_INITIALIZED);
        return false;
    }
    if (!::IsWindow(owner) || startMessage < WM_USER || stopMessage < WM_USER
        || startMessage > 0xFFFF || stopMessage > 0xFFFF
        || startMessage == stopMessage) {
        RecordError(ERROR_INVALID_PARAMETER);
        return false;
    }

    lastError_.store(ERROR_SUCCESS, std::memory_order_relaxed);
    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) {
        RecordError(::GetLastError());
        return false;
    }

    owner_ = owner;
    startMessage_ = startMessage;
    stopMessage_ = stopMessage;
    state_.Reset();

    std::future<DWORD> installed;
    try {
        std::promise<DWORD> ready;
        installed = ready.get_future();
        thread_ = std::thread(&GlobalHotkeys::ThreadMain, this, std::move(ready));
    } catch (...) {
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        owner_ = nullptr;
        RecordError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    // The worker creates its message queue and installs the hook before this
    // returns. Stop can therefore immediately post WM_QUIT after success.
    const DWORD error = installed.get();
    if (error != ERROR_SUCCESS) {
        thread_.join();
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        owner_ = nullptr;
        threadId_ = 0;
        RecordError(error);
        return false;
    }
    return true;
}

void GlobalHotkeys::Stop() noexcept
{
    if (thread_.joinable()) {
        if (!::PostThreadMessageW(threadId_, WM_QUIT, 0, 0)) {
            RecordError(::GetLastError());
        }
        // The event also makes shutdown reliable if posting fails, for example
        // because the thread already exited or its message queue is full.
        ::SetEvent(stopEvent_);
        thread_.join();
    }
    if (stopEvent_ != nullptr) {
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    owner_ = nullptr;
    threadId_ = 0;
    state_.Reset();
}

DWORD GlobalHotkeys::LastError() const noexcept
{
    return lastError_.load(std::memory_order_relaxed);
}

void GlobalHotkeys::RecordError(DWORD error) noexcept
{
    lastError_.store(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE,
        std::memory_order_relaxed);
}

void GlobalHotkeys::ThreadMain(std::promise<DWORD> ready) noexcept
{
    threadId_ = ::GetCurrentThreadId();
    MSG message{};
    ::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    current_ = this;

    const HHOOK hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardProc,
        ::GetModuleHandleW(nullptr), 0);
    if (hook == nullptr) {
        const DWORD error = ::GetLastError();
        current_ = nullptr;
        ready.set_value(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
        return;
    }

    // Read physical modifier keys only. Seeding generic VK_MENU as well would
    // leave it stuck when a VK_LMENU/VK_RMENU release arrives later.
    constexpr int seedKeys[] = { VK_LMENU, VK_RMENU, VK_LCONTROL, VK_RCONTROL,
        VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN, 'C', 'P', 'S' };
    for (const int key : seedKeys) {
        if ((::GetAsyncKeyState(key) & 0x8000) != 0) {
            state_.SeedPressed(static_cast<DWORD>(key));
        }
    }
    ready.set_value(ERROR_SUCCESS);

    bool running = true;
    while (running) {
        const DWORD wait = ::MsgWaitForMultipleObjectsEx(1, &stopEvent_, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_FAILED) {
            RecordError(::GetLastError());
            break;
        }
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }

    if (!::UnhookWindowsHookEx(hook)) {
        RecordError(::GetLastError());
    }
    state_.Reset();
    current_ = nullptr;
}

DWORD GlobalHotkeys::NormalizeKey(const KBDLLHOOKSTRUCT& event) noexcept
{
    // Hooks normally provide physical modifier VKs. Normalize generic forms
    // as well so they match the physical keys seeded above.
    if (event.vkCode == VK_MENU) {
        return (event.flags & LLKHF_EXTENDED) != 0 ? VK_RMENU : VK_LMENU;
    }
    if (event.vkCode == VK_CONTROL) {
        return (event.flags & LLKHF_EXTENDED) != 0 ? VK_RCONTROL : VK_LCONTROL;
    }
    if (event.vkCode == VK_SHIFT) {
        return event.scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    }
    return event.vkCode;
}

LRESULT CALLBACK GlobalHotkeys::KeyboardProc(int code, WPARAM wParam, LPARAM lParam) noexcept
{
    auto* self = current_;
    if (code != HC_ACTION || self == nullptr || lParam == 0) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if ((event.flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
    if (!down && !up) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const DWORD key = NormalizeKey(event);
    const auto result = self->state_.Process(key, down);
    bool delivered = true;
    if (result.action == autoclicker::ChordState::Action::Start) {
        POINT target{};
        if (!::GetCursorPos(&target)) {
            self->RecordError(::GetLastError());
            delivered = false;
        } else if (!::PostMessageW(self->owner_, self->startMessage_,
            static_cast<WPARAM>(static_cast<INT_PTR>(target.x)),
            static_cast<LPARAM>(target.y))) {
            self->RecordError(::GetLastError());
            delivered = false;
        }
    } else if (result.action == autoclicker::ChordState::Action::Stop) {
        if (!::PostMessageW(self->owner_, self->stopMessage_, 0, 0)) {
            self->RecordError(::GetLastError());
            delivered = false;
        }
    }
    if (!delivered) {
        self->state_.CancelSuppression(key);
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }
    return result.suppress ? 1 : ::CallNextHookEx(nullptr, code, wParam, lParam);
}
