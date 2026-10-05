// CrashLog.cpp - 未处理异常捕获（见 CrashLog.h 说明）
#include "pch.h"
#include "CrashLog.h"

#include <strsafe.h>
#include <dbghelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")

namespace {

constexpr size_t kLine = 768;

// 把异常码翻译成人话，避免每次都要查表
const wchar_t* CodeName(DWORD c) {
    switch (c) {
        case 0xC0000005: return L"(ACCESS_VIOLATION 访问违规)";
        case 0xC000041D: return L"(FATAL_USER_CALLBACK_EXCEPTION 窗口过程/回调内抛出了未处理异常)";
        case 0xE06D7363: return L"(C++ 异常被抛出且无人捕获)";
        case 0x80000003: return L"(BREAKPOINT 断言/调试断点)";
        case 0xC00000FD: return L"(STACK_OVERFLOW 栈溢出，通常是无限递归)";
        case 0xC0000374: return L"(HEAP_CORRUPTION 堆已损坏，多为越界写/重复释放)";
        case 0xC000001D: return L"(ILLEGAL_INSTRUCTION)";
        case 0xC0000094: return L"(INT_DIVIDE_BY_ZERO 整除零)";
        case 0xC0000006: return L"(IN_PAGE_ERROR 读取分页文件失败)";
        default: return L"";
    }
}

// 崩溃期可用的极简日志（不用 CRT 缓冲，直接 WriteFile，避免缓冲丢失）
void BuildPath(wchar_t* out, size_t cap, const wchar_t* leaf) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    StringCchCopyW(out, cap, tmp);
    StringCchCatW(out, cap, leaf);
}

class Log {
public:
    Log() {
        wchar_t path[MAX_PATH * 2] = {};
        BuildPath(path, _countof(path), L"ts_crash.log");
        m_h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~Log() { if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h); }

    void W(const wchar_t* s) const {
        if (m_h == INVALID_HANDLE_VALUE || !s) return;
        char buf[8192];
        const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), nullptr, nullptr);
        if (n > 1) { DWORD wr = 0; WriteFile(m_h, buf, (DWORD)(n - 1), &wr, nullptr); }
    }
    void F(const wchar_t* fmt, ...) const {
        wchar_t b[kLine];
        va_list ap; va_start(ap, fmt);
        _vsnwprintf_s(b, kLine, _TRUNCATE, fmt, ap);
        va_end(ap);
        W(b);
    }
    bool ok() const { return m_h != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_h = INVALID_HANDLE_VALUE;
};

// 一行：地址 -> 模块名 + 模块内偏移（偏移可用于 dumpbin/PDB 反查函数）
void FrameLine(wchar_t* out, size_t cap, void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) || !mod) {
        StringCchPrintfW(out, cap, L"0x%p  <未知模块>\r\n", addr);
        return;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(mod, path, MAX_PATH);
    const wchar_t* leaf = wcsrchr(path, L'\\');
    leaf = leaf ? leaf + 1 : path;
    const unsigned long long rva =
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(addr)) -
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mod));
    StringCchPrintfW(out, cap, L"0x%p  %s + 0x%llX\r\n", addr, leaf, rva);
}

void DumpStack(const Log& log) {
    void* frames[48] = {};
    const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
    log.F(L"调用栈 (%hu 层):\r\n", n);
    for (USHORT i = 0; i < n; ++i) {
        wchar_t line[kLine];
        FrameLine(line, kLine, frames[i]);
        log.W(L"  "); log.W(line);
    }
}

void WriteDump(const Log& log, EXCEPTION_POINTERS* ep) {
    wchar_t path[MAX_PATH * 2] = {};
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    SYSTEMTIME st{}; GetLocalTime(&st);
    StringCchPrintfW(path, _countof(path), L"%sts_crash_%04d%02d%02d_%02d%02d%02d.dmp",
                     tmp, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { log.W(L"minidump: 创建文件失败\r\n"); return; }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo);

    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                                      type, ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(h);
    if (ok) log.F(L"minidump: %s\r\n", path);
    else    log.F(L"minidump: 写入失败 (err=%lu)\r\n", GetLastError());
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* ep) {
    if (IsDebuggerPresent()) return EXCEPTION_CONTINUE_SEARCH;

    Log log;
    SYSTEMTIME st{}; GetLocalTime(&st);
    EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : nullptr;
    const DWORD code = er ? er->ExceptionCode : 0;

    log.W(L"\r\n");
    log.F(L"===================== 崩溃 %04d-%02d-%02d %02d:%02d:%02d =====================\r\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    log.F(L"异常码   : 0x%08lX %s\r\n", code, CodeName(code));
    log.F(L"线程/PID : %lu / %lu\r\n", GetCurrentThreadId(), GetCurrentProcessId());

    if (code == 0xC0000005 && er && er->NumberParameters >= 2) {
        const ULONG_PTR what = er->ExceptionInformation[0];
        const wchar_t* kind = what == 0 ? L"读取" : what == 1 ? L"写入" : L"执行";
        log.F(L"违规详情 : 尝试%s地址 0x%llX\r\n", kind,
              static_cast<unsigned long long>(er->ExceptionInformation[1]));
    }

    log.W(L"故障位置 : ");
    { wchar_t line[kLine]; FrameLine(line, kLine, er ? er->ExceptionAddress : nullptr); log.W(line); }

    DumpStack(log);

    // 如果故障地址本身是个已加载模块，顺带把主程序基址记下来，方便换算
    log.F(L"主模块基址: 0x%p\r\n",
          reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))));

    WriteDump(log, ep);
    log.W(L"===============================================================\r\n");
    return EXCEPTION_EXECUTE_HANDLER;
}

// 未捕获的 C++ 异常会走 std::terminate → abort，OS 层过滤器拿不到类型信息，
// 这里补一层，顺便把 what() 打出来
[[noreturn]] void OnTerminate() {
    Log log;
    log.W(L"\r\n===================== std::terminate =====================\r\n");
    try {
        auto ep = std::current_exception();
        if (ep) std::rethrow_exception(ep);
        log.W(L"C++ 异常: <无活动异常>\r\n");
    } catch (const std::exception& e) {
        log.F(L"C++ 异常: %hs\r\n", e.what());
    } catch (...) {
        log.W(L"C++ 异常: 非 std::exception 派生类型\r\n");
    }
    DumpStack(log);
    log.W(L"=========================================================\r\n");
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) {}  // 理论上到不了
}

// 日志无上限增长会拖慢启动，超过 256KB 时在会话开始时清空
void TrimLogIfHuge() {
    wchar_t path[MAX_PATH * 2] = {};
    BuildPath(path, _countof(path), L"ts_crash.log");
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        const unsigned long long sz = (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32)
                                    | fad.nFileSizeLow;
        if (sz > 256ull * 1024ull) DeleteFileW(path);
    }
}

}  // namespace

void CrashLog::Install() {
    TrimLogIfHuge();
    std::set_terminate(OnTerminate);
    SetUnhandledExceptionFilter(OnUnhandled);

    // 每次启动留一条会话标记，便于确认「哪个进程、哪个版本崩的」
    Log log;
    if (!log.ok()) return;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SYSTEMTIME st{}; GetLocalTime(&st);
    log.F(L"--- 启动 pid=%lu %04d-%02d-%02d %02d:%02d:%02d  %s\r\n",
          GetCurrentProcessId(), st.wYear, st.wMonth, st.wDay,
          st.wHour, st.wMinute, st.wSecond, exe);
}
