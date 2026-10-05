// CrashLog.h - 未处理异常捕获
//
// 为什么需要：本机 Windows Error Reporting 处于关闭状态（HKLM\...\Windows Error
// Reporting\Disabled=1），进程崩溃后既没有 Application Error 事件、也没有 WER 报告，
// 导致「点设置面板就崩」类问题只能靠猜。故进程内自建最小可用的崩溃取证：
//   %TEMP%\ts_crash.log   异常码 / 故障模块+RVA / 调用栈 / 模块基址
//   %TEMP%\ts_crash_*.dmp minidump（可用 WinDbg/VS 打开）
#pragma once

namespace CrashLog {
// 在主线程入口尽早调用（越早越好，最好在创建任何窗口前）
void Install();
}
