#include "MyHttpServer.h"

#pragma comment(lib, "ws2_32.lib")

// HTTP请求结构定义
typedef struct {
    char method[16];
    char* path;
    char version[16];
    char* headers;
    char* body;
} MyHttpRequest;


// 用于传递给客户端处理线程的参数
typedef struct {
    MyHttpServer server;
    SOCKET clientSocket;
} ClientHandlerArgs;



// 根据文件名后缀获取MIME类型
// 注意：这里写的MIME类型并不全，如果需要处理其它类型的文件，需要自行添加
// 参考网站：https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Guides/MIME_types/Common_types
const char* GetMimeType(const char* filename) {
    static struct { char *extension, *mimeType; } mimeTypeDict[] = {
        { ".html", "text/html" },
        { ".css",  "text/css" },
        { ".js",   "application/javascript" },
        { ".json", "application/json" },
        { ".ttf",  "font/ttf" },
        { ".jpg",  "image/jpeg" },
        { ".png",  "image/png" },
        { ".gif",  "image/gif" },
        { ".svg",  "image/svg+xml" },
        { ".ico",  "image/x-icon" },
        { ".mp3",  "audio/mpeg" }
    };

    const char* dot = strrchr(filename, '.');
    if(!dot || dot == filename) 
        return "application/octet-stream";

    for(int i = 0; i < sizeof(mimeTypeDict) / sizeof(mimeTypeDict[0]); i++)
        if(strcmp(dot, mimeTypeDict[i].extension) == 0) 
            return mimeTypeDict[i].mimeType;

    return "application/octet-stream";
}



// 对HTTP请求中的Url进行解码
// 例如 "%E4%BD%A0%E5%A5%BD%20%E4%B8%96%E7%95%8C" -> "你好 世界"
char* DecodeUrl(const char* encodedStr) {
    if(!encodedStr)
        return NULL;

    int len = strlen(encodedStr);
    char* decodedStr = (char*)malloc(len + 1);

    int i = 0, j = 0;
    while(i < len) {
        if((encodedStr[i] == '%') && (i + 2 < len)) {
            char hex[3] = { encodedStr[i+1], encodedStr[i+2], '\0' };
            decodedStr[j++] = strtol(hex, NULL, 16);
            i += 3;
        }
        else if(encodedStr[i] == '+') {
            decodedStr[j++] = ' ';
            i++;
        } 
        else decodedStr[j++] = encodedStr[i++];
    }

    decodedStr[j] = '\0';
    decodedStr = (char*)realloc(decodedStr, j + 1);
    return decodedStr;
}



// 释放HTTP请求的数据
void FreeHttpRequest(MyHttpRequest* request) {
    if(request) {
        free(request->path);
        free(request->headers);
        free(request->body);
        free(request);
    }
}



// 接收并解析HTTP请求
MyHttpRequest* ReceiveHttpRequest(SOCKET clientSocket) {
    MyHttpRequest* request = NULL;
    char buffer[1024];
    int bytesReceived;
    char* requestStr = (char*)calloc(10, sizeof(char));
    int totalBytesReceived = 0;

    // 接收HTTP请求中body以外的部分
    while((bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        requestStr = (char*)realloc(requestStr, totalBytesReceived + bytesReceived + 1);
        memcpy(requestStr + totalBytesReceived, buffer, bytesReceived);
        totalBytesReceived += bytesReceived;
        requestStr[totalBytesReceived] = '\0';

        if(strstr(requestStr, "\r\n\r\n"))
            break;
    }


    // 从HTTP请求的第一行中，解析出 method path version
    char* line = strtok(requestStr, "\r\n");
    if(line == NULL) {
        free(requestStr);
        return NULL;
    }

    request = (MyHttpRequest*)calloc(1, sizeof(MyHttpRequest));

    char* encodedPath = calloc(strlen(line), sizeof(char));
    sscanf(line, "%s %s %s", request->method, encodedPath, request->version);
    request->path = DecodeUrl(encodedPath);
    free(encodedPath);

    if(!request->method[0] || !request->path) {
        free(requestStr);
        FreeHttpRequest(request);
        return NULL;
    }

    // 从HTTP请求中解析 headers 和 body(未完成)

    free(requestStr);
    return request;
}



/**
 * @brief 客户端请求处理线程函数
 * @param args 指向ClientHandlerArgs的指针
 */
DWORD WINAPI HandleClient(LPVOID args) {
    ClientHandlerArgs* handlerArgs = (ClientHandlerArgs*)args;
    MyHttpServer server = handlerArgs->server;
    SOCKET clientSocket = handlerArgs->clientSocket;
    
    MyHttpRequest* request = ReceiveHttpRequest(clientSocket);

    if(!request) {
        const char* response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
        closesocket(clientSocket);
        free(handlerArgs);
        return 0;
    }


    // 只处理GET请求
    if(strcmp(request->method, "GET") == 0) {
        char* requestPath = request->path;

        // 处理已注册回调函数的Get请求
        for(int i = 0; i < server->mapGetsNum; i++) {
            const char* mapGetPath = server->mapGets[i].path;
            if(strncmp(requestPath, mapGetPath, strlen(mapGetPath)) == 0) {
                char* parameterStr = strchr(request->path, '?');
                BOOL haveResponse = server->mapGets[i].callback(clientSocket, parameterStr ? (parameterStr + 1) : parameterStr);

                if(!haveResponse) {
                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    send(clientSocket, response, strlen(response), 0);
                }

                goto end;
            }
        }

        printf("[INFO] Received request: %s %s\n", request->method, request->path);

        // 当路径是'/'时, 访问index.html
        if(strcmp(requestPath, "/") == 0)
            requestPath = "/index.html";
        
        char fullPath[MAX_PATH];
        sprintf(fullPath, "%s%s", server->rootPath, requestPath);
    
        // 将UTF-8编码的路径转为UTF-16编码，因为fopen打开带中文的路径会出问题
        int wlen = MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, NULL, 0);
        wchar_t *wpath = (wchar_t*)malloc(wlen * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, wpath, wlen);

        FILE* file = _wfopen(wpath, L"rb");
        free(wpath);

        if(file) {
            fseek(file, 0, SEEK_END);
            int fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);
            
            char header[256];
            sprintf(header, 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n\r\n", GetMimeType(requestPath), fileSize);
            
            send(clientSocket, header, strlen(header), 0);
            
            // 发送文件内容
            int fileBufferSize = min(fileSize, RESPONSE_BUFFER_SIZE);
            char* fileBuffer = (char*)malloc(fileBufferSize);
            size_t bytesRead;
            while ((bytesRead = fread(fileBuffer, 1, fileBufferSize, file)) > 0)
                send(clientSocket, fileBuffer, bytesRead, 0);

            free(fileBuffer);
            fclose(file);
            printf("[INFO] Responded 200 OK for %s\n", fullPath);

        } else {
            // 文件不存在，返回404
            const char* response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(clientSocket, response, strlen(response), 0);
            printf("[WARN] File not found: %s. Responded 404\n", fullPath);
        }
    } else {
        // 不支持的请求方法
        const char* response = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
        printf("[WARN] Unsupported method: %s. Responded 501\n", request->method);
    }

end:
    FreeHttpRequest(request);
    closesocket(clientSocket);
    free(handlerArgs);
    return 0;
}



/**
 * @brief 创建并初始化HTTP服务
 * @param rootPath 静态文件根目录
 * @param port 监听端口
 * @return 成功返回HTTP服务句柄，失败返回NULL
 */
MyHttpServer MyHttpServer_Create(const char* rootPath, int port) {
    WSADATA wsaData;

    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return NULL;

    MyHttpServer server = (MyHttpServer)calloc(1, sizeof(MyHttpServer_t));

    strncpy(server->rootPath, rootPath, MAX_PATH - 1);
    server->rootPath[MAX_PATH - 1] = '\0';
    server->port = port;
    server->isRunning = FALSE;

    // 创建监听Socket
    server->listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(server->listenSocket == INVALID_SOCKET) {
        free(server);
        WSACleanup();
        return NULL;
    }

    // 绑定Socket到指定端口，如果端口被占用，尝试寻找可用端口
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(server->port);

    // 尝试找到一个未被占用的端口
    while(bind(server->listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR && WSAGetLastError() == WSAEADDRINUSE)
        serverAddr.sin_port = htons(++(server->port));

    if(WSAGetLastError() == SOCKET_ERROR) {
        closesocket(server->listenSocket);
        free(server);
        WSACleanup();
        return NULL;
    }

    // 开始监听
    if(listen(server->listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(server->listenSocket);
        free(server);
        WSACleanup();
        return NULL;
    }

    return server;
}



/**
 * @brief 启动HTTP服务的监听循环
 * @param server MyHttpServer_Create返回的HTTP服务句柄
 */
DWORD WINAPI MyHttpServer_Start(MyHttpServer server) {
    if(!server) return 0;

    server->isRunning = TRUE;
    while (server->isRunning) {
        // 接受客户端连接
        SOCKET clientSocket = accept(server->listenSocket, NULL, NULL);
        if(clientSocket == INVALID_SOCKET) {
            if(!server->isRunning)
                break;
            else continue;
        }

        // 为每个客户端连接创建独立的线程进行处理
        ClientHandlerArgs* args = (ClientHandlerArgs*)malloc(sizeof(ClientHandlerArgs));
        args->server = server;
        args->clientSocket = clientSocket;
        HANDLE hThread = CreateThread(NULL, 0, HandleClient, args, 0, NULL);
        CloseHandle(hThread); // 分离线程，让其自行运行和销毁
    }

    return 0;
}



/**
 * @brief 关闭HTTP服务，释放资源
 * @param server MyHttpServer_Create返回的HTTP服务句柄
 */
void MyHttpServer_Close(MyHttpServer server) {
    if(!server) return;
    
    server->isRunning = FALSE;
    
    // 关闭监听socket以中断accept()调用
    if(server->listenSocket != INVALID_SOCKET) {
        closesocket(server->listenSocket);
        server->listenSocket = INVALID_SOCKET;
    }

    WSACleanup();
    free(server->mapGets);
    free(server);
}



/**
 * @brief 注册处理对应Get请求的回调函数
 * @param server MyHttpServer_Create返回的HTTP服务句柄
 * @param path Get请求的路径，例如 "/api"
 * @param method callback
 */
void MyHttpServer_MapGet(MyHttpServer server, const char* path, MyHttpServerGetCallback callback) {
    if(!server || !path || !callback) 
        return;

    server->mapGetsNum += 1;
    server->mapGets = (MyHttpServerMapGet*)realloc(server->mapGets, sizeof(MyHttpServerMapGet) * server->mapGetsNum);
    server->mapGets[server->mapGetsNum - 1].path = path;
    server->mapGets[server->mapGetsNum - 1].callback = callback;
}


// --- 使用示例 ---
// int main() {
//     MyHttpServer server = MyHttpServer_Create("./assets", 3050);

//     if(server) {
//         printf("[INFO] Server created successfully, Listening on http://localhost:%d \n", server->port);
//         MyHttpServer_Start(server);
//     }
//     else {
//         printf("[ERROR] Failed to create server\n %s", strerror(errno));
//         return -1;
//     }

//     return 0;
// }