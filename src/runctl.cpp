#include "runctl.h"

#include <windows.h>

#include <atomic>
#include <cstdlib>

namespace runctl {

namespace {

std::atomic<bool> g_cancel{ false };
std::atomic<int> g_busy{ 0 };
std::atomic<bool> g_installed{ false };
HANDLE g_idle = nullptr;                                      // 手上没有文件操作时是有信号的

HANDLE idleEvent() {
    static HANDLE h = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    return h;
}

BOOL WINAPI onCtrl(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        g_cancel = true;                                      // 不结束进程:干活的代码自己停到安全的地方
        return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        // 窗口要关了:这个函数一返回进程就被结束(系统最多等 5 秒)。立起标志,等手上这个文件收尾
        g_cancel = true;
        if (g_idle) WaitForSingleObject(g_idle, 4500);
        return TRUE;
    default:
        return FALSE;
    }
}

} // namespace

void install() {
    if (g_installed.exchange(true)) return;
    g_idle = idleEvent();
    SetConsoleCtrlHandler(nullptr, FALSE);                    // 启动我们的程序要是关掉了 Ctrl+C,这里恢复
    SetConsoleCtrlHandler(onCtrl, TRUE);
}

bool cancelRequested() { return g_cancel.load(); }
void requestCancel() { g_cancel = true; }
void resetCancel() { g_cancel = false; }

Busy::Busy() {
    if (g_busy.fetch_add(1) == 0) ResetEvent(idleEvent());
}

Busy::~Busy() {
    if (g_busy.fetch_sub(1) == 1) SetEvent(idleEvent());
}

void testPause() {
    static const int ms = [] {
        wchar_t b[32];
        const DWORD n = GetEnvironmentVariableW(L"TAGREPAIR_TEST_DELAY_MS", b, 32);
        const int v = n > 0 && n < 32 ? _wtoi(b) : 0;
        return v < 0 ? 0 : v > 5000 ? 5000 : v;
    }();
    for (int waited = 0; waited < ms && !cancelRequested(); waited += 20) Sleep(20);      // 等的时候来了取消:马上往下走
}

} // namespace runctl
