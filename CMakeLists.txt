# TaskbarStudio - 任务栏资源监控悬浮条
# 仅资源监控，不含任务栏美化
cmake_minimum_required(VERSION 3.20)
project(TaskbarStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 静态链接 CRT，免安装单文件
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Unicode
add_compile_definitions(UNICODE _UNICODE)
# UTF-8 源码（项目路径含全角括号）
add_compile_options(/utf-8 /diagnostics:classic)

add_executable(${PROJECT_NAME} WIN32
    src/pch.cpp
    src/main.cpp
    src/Monitor.cpp
    src/NvApi.cpp
    src/PawnIo.cpp
    src/OverlayWindow.cpp
    src/AppConfig.cpp
    src/FullscreenDetect.cpp
    src/SettingsDialog.cpp
    src/app.rc
)

# Windows 库
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11 d2d1 dwrite dcomp dxgi
    dwmapi iphlpapi winhttp psapi
    ole32 oleaut32 wbemuuid
    user32 gdi32 shell32 comctl32
)

# 嵌入自定义 manifest（requireAdministrator + Per-Monitor DPI）
# 禁用 CMake 默认 manifest，由 app.rc 中的 manifest 资源替代
target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO)
