#include "MyHttpServer.h"
#include "WebView2.h"
#include <windows.h>
#include <stdio.h>
#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

// 窗口的初始大小
// PS：如果要让应用的逻辑像素大小受系统缩放影响，可以在 manifest.xml 中关闭dpi感知
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 700

// 窗口类名和窗口标题，可以按自己喜欢乱改
#define WINDOW_CLASSNAME L"com.herta"
#define WINDOW_TITLE L"Herta"

// HTTP服务的静态文件根目录
#define SERVER_BASEPATH "./assets"

// HTTP服务的端口，如果端口被占用，实际使用的端口可能不同
#define SERVER_PORT 3050

// webview2 在运行的时候，会生成用户数据文件夹，没什么用，直接扔到Temp目录的指定文件夹
#define WEBVIEW_DATA_FOLDER_PATH L"Herta.Webview2.Data"


// 窗口的句柄，通过Win32 API可以修改样式和属性
HWND window = NULL;

// 窗口的矩形范围
RECT windowRect = { 200, 200, 200 + WINDOW_WIDTH, 200 + WINDOW_HEIGHT };

// 鼠标位置
POINT mousePoint = { 0 };

// 贴着的窗口
HWND attachedWindow = NULL;

// 鼠标钩子句柄
HHOOK hMouseHook = NULL;

// 窗口的缩放系数
float windowScale = 1.0;

// 鼠标是否在黑塔上
BOOL isHoveringHerta = 0;

// 黑塔的状态：0表示正常状态，1表示坐在窗口上
char* hertaState = 0;

// HTTP服务的句柄
MyHttpServer server = NULL;

// webview 和 webviewController 可以操控Webview2控件
ICoreWebView2* webview = NULL;
ICoreWebView2Controller2* webviewController = NULL;

// envHandler 和 controllerHandler 相当于带引用计数器的回调函数
ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* envHandler = NULL;
ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* controllerHandler = NULL;

// envHandler 和 controllerHandler 的引用计数器
// PS：为了少写几个函数，所以共用一个引用计数器
ULONG handlerRefCount = 0;

ULONG HandlerAddRef(IUnknown* _this) {
    return ++handlerRefCount;
}

ULONG HandlerRelease(IUnknown* _this) {
    --handlerRefCount;
    if (handlerRefCount == 0) {
        if(controllerHandler) {
            free(controllerHandler->lpVtbl);
            free(controllerHandler);
        }
        if(envHandler) {
            free(envHandler->lpVtbl);
            free(envHandler);
        }
    }
    return handlerRefCount;
}

HRESULT HandlerQueryInterface(IUnknown* _this, IID* riid, void** ppvObject) {
    return E_NOINTERFACE;
}



// 在Webview2控制器创建完成时被调用，获取 webview 和 webviewController
HRESULT ControllerHandlerInvoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This, HRESULT errorCode, ICoreWebView2Controller* controller) {
    if (!controller)
         return S_OK;

    webviewController = (void*)controller;
    webviewController->lpVtbl->AddRef(webviewController);
    webviewController->lpVtbl->get_CoreWebView2(webviewController, &webview);

    // 修改Webview的配置项
    ICoreWebView2Settings* Settings;
    webview->lpVtbl->get_Settings(webview, &Settings);

    Settings->lpVtbl->put_IsScriptEnabled(Settings, TRUE);
    Settings->lpVtbl->put_AreDefaultScriptDialogsEnabled(Settings, TRUE);
    Settings->lpVtbl->put_IsWebMessageEnabled(Settings, TRUE);
    Settings->lpVtbl->put_AreDevToolsEnabled(Settings, TRUE);
    Settings->lpVtbl->put_AreDefaultContextMenusEnabled(Settings, TRUE);
    Settings->lpVtbl->put_IsStatusBarEnabled(Settings, TRUE);

    // 设置Webview2的大小为窗口大小
    int wndWidth = windowRect.right - windowRect.left;
    int wndHeight = windowRect.bottom - windowRect.top;
    webviewController->lpVtbl->put_Bounds(webviewController, (RECT){0, 0, wndWidth, wndHeight});

    // 将Webview2的背景设置为透明
    COREWEBVIEW2_COLOR transparentColor = { 0, 0, 0, 0 };
    webviewController->lpVtbl->put_DefaultBackgroundColor(webviewController, transparentColor);
    
    // 打开HTTP服务对应的本地端口
    wchar_t url[MAX_PATH];
    wsprintfW(url, L"http://localhost:%d", server->port);
    webview->lpVtbl->Navigate(webview, url);

    return S_OK;
}



// 在Webview2环境创建完成时被调用，通过Webview2环境创建Webview2控制器
HRESULT EnvHandlerInvoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This, HRESULT errorCode, ICoreWebView2Environment* env) {
    if(!env) return S_OK;
    
    controllerHandler = malloc(sizeof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler));
    controllerHandler->lpVtbl = malloc(sizeof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl));
    controllerHandler->lpVtbl->AddRef = (void*)HandlerAddRef;
    controllerHandler->lpVtbl->Release = (void*)HandlerRelease;
    controllerHandler->lpVtbl->QueryInterface = (void*)HandlerQueryInterface;
    controllerHandler->lpVtbl->Invoke = ControllerHandlerInvoke;

    env->lpVtbl->CreateCoreWebView2Controller(env, window, controllerHandler);

    return S_OK;
}


extern BOOL CALLBACK FindTargetWindowToAttachProc(HWND hwnd, LPARAM lParam);
extern void DetachFromTargetWindow();

// 低级鼠标钩子过程
// 因为Webview2会直接吃掉鼠标事件，为了实现任意位置拖拽窗口，只能使用低级鼠标钩子，不知道有没有更好的方法
// 顺便用来实现自动看向鼠标位置
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    static BOOL isDragging = FALSE;
    static POINT startMousePos; 
    static POINT startWndPos;

    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

        switch(wParam) {
        case WM_LBUTTONDOWN:
            if (isHoveringHerta) {
                startMousePos = pMouseStruct->pt;
                startWndPos.x = windowRect.left;
                startWndPos.y = windowRect.top;
                isDragging = TRUE;
            }
            break;

        case WM_LBUTTONUP:
            if(isDragging) {
                EnumWindows(FindTargetWindowToAttachProc, 0);
                isDragging = FALSE;
            }
            break;

        case WM_MOUSEMOVE:
            mousePoint = pMouseStruct->pt;

            if (isDragging) {
                if(attachedWindow)
                    DetachFromTargetWindow();

                int deltaX = pMouseStruct->pt.x - startMousePos.x;
                int deltaY = pMouseStruct->pt.y - startMousePos.y;

                SetWindowPos(window, NULL, startWndPos.x + deltaX, startWndPos.y + deltaY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            break;
        
        // 鼠标滚轮放大缩小窗口
        case WM_MOUSEWHEEL:
            if (isHoveringHerta) {
                short wheelDelta = HIWORD(pMouseStruct->mouseData);
                float scale = wheelDelta > 0 ? 1.05: 0.95;
                windowScale *= scale;

                int centerX = (windowRect.left + windowRect.right) / 2;
                int centerY = (windowRect.top + windowRect.bottom) / 2;

                int newWidth = (int)(WINDOW_WIDTH * windowScale);
                int newHeight = (int)(WINDOW_HEIGHT * windowScale);

                int newLeft = centerX - newWidth / 2;
                int newTop = centerY - newHeight / 2;

                SetWindowPos(window, NULL, newLeft, newTop, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        }

    }

    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}



// 窗口过程函数
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        // Webview的大小自动调整为窗口大小
        GetWindowRect(window, &windowRect);
        int wndWidth = windowRect.right - windowRect.left;
        int wndHeight = windowRect.bottom - windowRect.top;
        
        if (webviewController)
            webviewController->lpVtbl->put_Bounds(webviewController, (RECT){0, 0, wndWidth, wndHeight});
        break;

    case WM_MOVE:
        GetWindowRect(window, &windowRect);
        break;

    case WM_KEYDOWN:
        // F12键打开DevTools
        if(wParam == VK_F12 && webview) 
            webview->lpVtbl->OpenDevToolsWindow(webview);
        break;

    case WM_CLOSE:
        // 程序关闭时清理资源
        MyHttpServer_Close(server);
        UnhookWindowsHookEx(hMouseHook);
        PostQuitMessage(0);
        exit(0);
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// 自定义的对话框函数
int MyMessageBoxW(HWND hWnd, UINT uType, LPCWSTR lpCaption, LPCWSTR formatText, ...) {
    wchar_t text[1024];
    va_list ap;
    va_start(ap, formatText);
    vswprintf(text, sizeof(text)/sizeof(wchar_t), formatText, ap);
    va_end(ap);
    
    return MessageBoxW(hWnd, text, lpCaption, uType);
}


// 处理从Webview2接收的移动窗口请求
BOOL HandleMoveWindowRequest(SOCKET clientSocket, const char* parameterStr) {
    if(!parameterStr) return FALSE;

    int dx = 0, dy = 0;
    if(sscanf(parameterStr, "dx=%d&dy=%d", &dx, &dy) > 0)
        SetWindowPos(window, NULL, windowRect.left + dx, windowRect.top + dy, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);

    return FALSE;
}

// 处理从Webview2接收的数据同步请求
BOOL HandleSynchronizeDataRequest(SOCKET clientSocket, const char* parameterStr) {
    sscanf(parameterStr, "isHoveringHerta=%d", &isHoveringHerta);
    char data[100];
    char response[200];

    sprintf(data, "{"
            "\"hertaState\": %d,"
            "\"wndRect\": [%d, %d, %d, %d],"
            "\"mousePos\": [%d, %d]"
        "}", 
        attachedWindow ? 1 : 0,
        windowRect.left, windowRect.top, windowRect.right, windowRect.bottom,
        mousePoint.x, mousePoint.y
    );
    sprintf(response, 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n%s", strlen(data), data);
    send(clientSocket, response, strlen(response), 0);

    return TRUE;
}


int main() {
    // 获得当前程序实例
    HINSTANCE hInstance = GetModuleHandleW(NULL);

    // 初始化 Common Controls，从而获得更现代的窗口UI风格
    InitCommonControlsEx(&(INITCOMMONCONTROLSEX){ sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES });

    // 注册并创建主窗口
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(32512));
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = WINDOW_CLASSNAME;

    if (!RegisterClassExW(&wc)) {
        MyMessageBoxW(NULL, MB_OK | MB_ICONERROR | MB_TOPMOST, L"ERROR", L"注册窗口类时出错，错误代码：0x%08X", GetLastError());
        return -1;
    }

    window = CreateWindowExW(WS_EX_TOPMOST, WINDOW_CLASSNAME, WINDOW_TITLE, WS_POPUP, windowRect.left, windowRect.top, (int)(WINDOW_WIDTH * windowScale), (int)(WINDOW_HEIGHT * windowScale), NULL, NULL, hInstance, NULL);

    if(!window) {
        MyMessageBoxW(NULL, MB_OK | MB_ICONERROR | MB_TOPMOST, L"ERROR", L"创建窗口时出错，错误代码：0x%08X", GetLastError());
        return -1;
    }
    
    // 使用 DWM 实现窗口背景透明
    DwmExtendFrameIntoClientArea(window, &(MARGINS){-1});

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    // 在新的线程创建HTTP服务
    server = MyHttpServer_Create(SERVER_BASEPATH, SERVER_PORT);
    if(!server) {
        MyMessageBoxW(NULL, MB_OK | MB_ICONERROR | MB_TOPMOST, L"ERROR", L"创建HTTP服务时出错，错误代码: 0x%08X", WSAGetLastError());
        return -1;
    }

    MyHttpServer_MapGet(server, "/api/move_window", HandleMoveWindowRequest);
    MyHttpServer_MapGet(server, "/api/synchronize_data", HandleSynchronizeDataRequest);
    CreateThread(NULL, 0, (void*)MyHttpServer_Start, server, 0, NULL);

    // 异步创建Webview2环境，当 Webview2环境创建完成后，EnvHandlerInvoke会被调用
    envHandler = malloc(sizeof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler));
    envHandler->lpVtbl = malloc(sizeof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl));
    envHandler->lpVtbl->AddRef = (void*)HandlerAddRef;
    envHandler->lpVtbl->Release = (void*)HandlerRelease;
    envHandler->lpVtbl->QueryInterface = (void*)HandlerQueryInterface;
    envHandler->lpVtbl->Invoke = EnvHandlerInvoke;

    // 用户数据文件夹指定到Temp目录下
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wsprintfW(tempPath + wcslen(tempPath), WEBVIEW_DATA_FOLDER_PATH);

    // 创建 Webview2 环境
    HRESULT result = CreateCoreWebView2EnvironmentWithOptions(NULL, tempPath, NULL, envHandler);
        
    if(result != S_OK) {
        // Microsoft Edge WebView2 下载网站：
        // https://developer.microsoft.com/zh-cn/microsoft-edge/webview2
        MyMessageBoxW(NULL, MB_OK | MB_ICONERROR | MB_TOPMOST, L"ERROR", L"创建Webview2时出错，错误代码: 0x%08X，请确认已安装 Microsoft Edge WebView2", result);
        return -1;
    }

    // 创建低级鼠标钩子
    hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    if(!hMouseHook) {
        MyMessageBoxW(NULL, MB_OK | MB_ICONERROR | MB_TOPMOST, L"ERROR", L"无法创建MOUSE_LL HOOK");
        return -1;
    }

    // 处理消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}