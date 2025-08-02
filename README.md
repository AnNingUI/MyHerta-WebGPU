# MyHerta
基于 **C语言** + **Three.js** 的黑塔桌宠程序

## 使用方法
1. 从 [Release](https://github.com/SyrieYume/MyHerta/releases/latest) 下载 `MyHerta.zip`, 解压到任意位置
2. 运行其中的 `MyHerta.exe`
3. 鼠标**左键**可拖动黑塔的位置
4. 鼠标悬浮在黑塔上时，可以通过鼠标**滚轮**调整黑塔的大小
5. 将黑塔拖到窗口上边缘时（确保黑塔下半部分与窗口上边缘有重合），可以让黑塔坐在窗口上

## 如何手动编译本项目
### 后端部分（C语言）
使用的编译器为 **MinGW** (gcc version 14.2.0)  

在项目根目录下依次执行以下命令：
```powershell
windres res/res.rc res/res.o
gcc src/*.c res/res.o -o publish/MyHerta.exe -lws2_32 -lpublish/Webview2Loader -lcomctl32 -lgdi32 -ldwmapi -L. -w -mwindows
```
生成的程序在 `/publish` 目录中

### 前端部分（JavaScript）
需要安装 **Node.js** (node version 22.15.1)

在项目根目录下依次执行以下命令：
```powershell
cd MyHerta.Web
npm install
npm run build
cd ..
```
生成的文件在 `/MyHerta.Web/dist` 目录中

### 程序打包
将 `MyHerta.Web` 目录下生成的 `dist` 文件夹改名为 `assets`，然后移动到项目根目录的 `publish` 文件夹中

最终 `publish` 目录下的文件结构应该是这样的：

- assets/
- MyHerta.exe
- WebView2Loader.dll

程序运行的时候，需要保证程序和 `assets` 文件夹，以及 `WebView2Loader.dll` 处于同一个目录下