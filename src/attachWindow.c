#include <windows.h>
#include <stdio.h>

#ifndef EVENT_OBJECT_CLOAKED
#define EVENT_OBJECT_CLOAKED 0x8017
#endif

extern HWND window;
extern RECT windowRect;
extern HWND attachedWindow;
int attachOffsetX = 0;
const float attachOffsetY = 518.0f / 720;
HWINEVENTHOOK hEventHook = NULL;


void FollowTargetWindow() {
    RECT targetRect;
    GetWindowRect(attachedWindow, &targetRect);

    long wndWidth = windowRect.right - windowRect.left;
    long wndHeight = windowRect.bottom - windowRect.top;

    long newX = targetRect.right - wndWidth + attachOffsetX;
    long newY = targetRect.top - (int)(wndHeight * attachOffsetY);

    SetWindowPos(window, NULL, newX, newY, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}


void DetachFromTargetWindow() {
    attachedWindow = NULL;
    UnhookWinEvent(hEventHook);
    hEventHook = NULL;
}


// 监听目标窗口的事件处理函数
VOID CALLBACK TargetWindowEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (hwnd != attachedWindow)
        return;

    switch (event) {
        case EVENT_OBJECT_LOCATIONCHANGE:
            if (idObject == OBJID_WINDOW && !IsIconic(hwnd))
                FollowTargetWindow();
            break;
        
        case EVENT_OBJECT_CLOAKED:
        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
        case EVENT_SYSTEM_MINIMIZESTART:
            DetachFromTargetWindow();
            break;
    }
}


void StickToTargetWindow(HWND target) {
    attachedWindow = target;

    RECT targetRect;
    GetWindowRect(target, &targetRect);

    attachOffsetX = windowRect.right - targetRect.right;

    // 设置事件钩子，监听目标窗口的移动、隐藏、关闭、最小化
    DWORD dwProcessId;
    GetWindowThreadProcessId(target, &dwProcessId);
    hEventHook = SetWinEventHook(EVENT_MIN, EVENT_MAX, NULL, TargetWindowEventProc, dwProcessId, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    
    FollowTargetWindow();
}


BOOL CALLBACK FindTargetWindowToAttachProc(HWND hwnd, LPARAM lParam)  {
    // 跳过自己、不可见窗口、无标题窗口
    if (!IsWindowEnabled(hwnd) || IsIconic(hwnd) || !IsWindowVisible(hwnd) || hwnd == window || GetWindowTextLengthW(hwnd) == 0) 
        return TRUE;

    RECT targetRect;
    GetWindowRect(hwnd, &targetRect);

    // 碰撞检测逻辑：
    // 1. 水平方向有重叠
    // 2. 垂直方向上：我们的窗口下半部分与窗口顶部重叠
    if ((windowRect.left < targetRect.right && windowRect.right > targetRect.left) &&
        (windowRect.bottom - targetRect.top < (windowRect.bottom - windowRect.top) * 2 / 3)) {
        StickToTargetWindow(hwnd);
        return FALSE;
    }

    return TRUE;
}

// 使用示例
// LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
//     switch(msg) {
//     case WM_MOVING:
//         if(attachedWindow) 
//             DetachTargetWindow();
//         break;
    
//     case WM_MOVE:
//         if(!attachedWindow)
//             EnumWindows(FindTargetEnumWindowsProc, 0);
//         break;

//     case WM_DESTROY:
//         PostQuitMessage(0);
//         exit(0);
//         break;
//     }

//     return DefWindowProcW(hwnd, msg, wParam, lParam);
// }

// int main() {
//     HINSTANCE hInstance = GetModuleHandleW(NULL);

//     WNDCLASSEXW wc = { 0 };
//     wc.cbSize = sizeof(WNDCLASSEXW);
//     wc.style = CS_HREDRAW | CS_VREDRAW;
//     wc.lpfnWndProc = WindowProc;
//     wc.hInstance = hInstance;
//     wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(32512));
//     wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
//     wc.hbrBackground = CreateSolidBrush(RGB(250, 250, 250));
//     wc.lpszClassName = L"TestWindow";
    
//     RegisterClassExW(&wc);

//     window = CreateWindowExW(WS_EX_TOPMOST, L"TestWindow", L"TestWindow", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 500, 400, NULL, NULL, hInstance, NULL);

//     ShowWindow(window, SW_SHOWDEFAULT);
//     UpdateWindow(window);

//     MSG msg;
//     while (GetMessageW(&msg, NULL, 0, 0)) {
//         TranslateMessage(&msg);
//         DispatchMessageW(&msg);
//     }

//     return (int)msg.wParam;
// }