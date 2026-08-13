// pch.h - precompiled header: Windows API + STL
#ifndef PCH_H
#define PCH_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// Version macros (must be before any Windows header)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Win10/11
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif

// winsock2.h MUST be included before windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>

// DWM taskbar blur
#include <dwmapi.h>

// RPC auth (CoSetProxyBlanket needs RPC_C_AUTHZ_LEVEL_NONE)
#include <rpcdce.h>

// Direct2D / DirectWrite (dwrite_3.h includes dwrite_1.h & dwrite_2.h)
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <d3d11.h>

// DirectComposition
#include <dcomp.h>

// DXGI
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

// Network monitoring (GetIfTable2 needs netioapi.h)
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// WMI
#include <wbemidl.h>
#include <comdef.h>

// WinHTTP (LibreHardwareMonitor local API)
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// STL
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <memory>
#include <algorithm>
#include <functional>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

#endif // PCH_H
