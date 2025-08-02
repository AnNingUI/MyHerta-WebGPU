#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESPONSE_BUFFER_SIZE 20 * 1024 * 1024 // Http响应时的文件缓冲区最大大小: 20MB

// 处理Get请求的回调函数
// 返回值：是否已经回复该请求
typedef BOOL(*MyHttpServerGetCallback)(SOCKET clientSocket, const char* parameterStr);

// Get请求路径 + 对应回调函数的结构定义
typedef struct {
    const char* path;
    MyHttpServerGetCallback callback;
} MyHttpServerMapGet;

// HTTP服务句柄结构定义
typedef struct {
    char rootPath[MAX_PATH];
    unsigned short port;
    SOCKET listenSocket;
    volatile BOOL isRunning;
    MyHttpServerMapGet* mapGets;
    int mapGetsNum;
} MyHttpServer_t, *MyHttpServer;


/**
 * @brief 创建并初始化HTTP服务
 * @param rootPath 静态文件根目录
 * @param port 监听端口
 * @return 成功返回HTTP服务句柄，失败返回NULL
 */
MyHttpServer MyHttpServer_Create(const char* rootPath, int port);


/**
 * @brief 启动HTTP服务的监听循环
 * @param server MyHttpServer_Create返回的HTTP服务句柄
 */
DWORD WINAPI MyHttpServer_Start(MyHttpServer server);

/**
 * @brief 关闭HTTP服务，释放资源
 * @param server MyHttpServer_Create返回的HTTP服务句柄
 */
void MyHttpServer_Close(MyHttpServer server);

/**
 * @brief 注册处理对应Get请求的回调函数
 * @param server MyHttpServer_Create返回的HTTP服务句柄
 * @param path Get请求的路径，例如 "/api"
 * @param callback 回调函数
 */
void MyHttpServer_MapGet(MyHttpServer server, const char* path, MyHttpServerGetCallback method);