#pragma once

// Minimal declarations for testing ClickEngine.cpp on a non-Windows host.
// These stand-ins do not validate the real Windows ABI or OS input behavior.
#include <cstdint>

using UINT = unsigned int;
using DWORD = std::uint32_t;
using LONG = std::int32_t;
using ULONG_PTR = std::uintptr_t;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using HWND = void*;
using HMONITOR = void*;
using BOOL = int;

struct POINT { LONG x; LONG y; };
struct MOUSEINPUT
{
    LONG dx;
    LONG dy;
    DWORD mouseData;
    DWORD dwFlags;
    DWORD time;
    ULONG_PTR dwExtraInfo;
};
struct INPUT { DWORD type; MOUSEINPUT mi; };

inline constexpr DWORD ERROR_SUCCESS = 0;
inline constexpr DWORD ERROR_ACCESS_DENIED = 5;
inline constexpr DWORD ERROR_NOT_ENOUGH_MEMORY = 8;
inline constexpr DWORD ERROR_INVALID_PARAMETER = 87;
inline constexpr DWORD ERROR_INVALID_MONITOR_HANDLE = 1461;
inline constexpr int VK_LBUTTON = 0x01;
inline constexpr int VK_SHIFT = 0x10;
inline constexpr int VK_CONTROL = 0x11;
inline constexpr int VK_MENU = 0x12;
inline constexpr int VK_LWIN = 0x5B;
inline constexpr int VK_RWIN = 0x5C;
inline constexpr int SM_XVIRTUALSCREEN = 76;
inline constexpr int SM_YVIRTUALSCREEN = 77;
inline constexpr int SM_CXVIRTUALSCREEN = 78;
inline constexpr int SM_CYVIRTUALSCREEN = 79;
inline constexpr DWORD MONITOR_DEFAULTTONULL = 0;
inline constexpr DWORD INPUT_MOUSE = 0;
inline constexpr DWORD MOUSEEVENTF_MOVE = 0x0001;
inline constexpr DWORD MOUSEEVENTF_LEFTDOWN = 0x0002;
inline constexpr DWORD MOUSEEVENTF_LEFTUP = 0x0004;
inline constexpr DWORD MOUSEEVENTF_VIRTUALDESK = 0x4000;
inline constexpr DWORD MOUSEEVENTF_ABSOLUTE = 0x8000;

short GetAsyncKeyState(int key);
int GetSystemMetrics(int metric);
HMONITOR MonitorFromPoint(POINT point, DWORD flags);
void SetLastError(DWORD error);
DWORD GetLastError();
UINT SendInput(UINT count, INPUT* input, int size);
BOOL PostMessageW(HWND owner, UINT message, WPARAM wParam, LPARAM lParam);
