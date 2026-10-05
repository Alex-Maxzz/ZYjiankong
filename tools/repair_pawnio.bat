@echo off
chcp 65001 >nul2>&1
setlocal enabledelayedexpansion

REM ============================================================
REM  TaskbarStudio - PawnIO 驱动深度修复脚本（需管理员运行）
REM
REM  作用：清掉半残状态（驱动文件/服务键被删但 INF 残留），
REM        让安装器能走「全新安装」分支，而不是卡在「已安装」更新分支。
REM
REM  用法：右键本文件 ->「以管理员身份运行」
REM ============================================================

set "REPORT=%TEMP%\pawnio_repair_log.txt"
echo ============================================== > "%REPORT%"
echo  PawnIO 深度修复报告>> "%REPORT%"
echo  时间: %date% %time%>> "%REPORT%"
echo ==============================================>> "%REPORT%"
echo.>> "%REPORT%"

REM --- 提权自检 ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [X] 需要管理员权限！请右键 ->以管理员身份运行。
    echo     当前权限不足，驱动相关操作会被系统拒绝。
    pause
    exit /b 1
)
echo [OK] 管理员权限已确认>> "%REPORT%"

REM --- 定位安装器 ---
set "SETUP_EXE="
if exist "E:\Aldevelop\ZYjiankong\src\PawnIO_setup.exe" (
    set "SETUP_EXE=E:\Aldevelop\ZYjiankong\src\PawnIO_setup.exe"
) else (
    echo [X] 找不到 PawnIO_setup.exe>> "%REPORT%"
    echo     预期路径: E:\Aldevelop\ZYjiankong\src\PawnIO_setup.exe>> "%REPORT%"
    goto :fail
)
echo [OK] 找到安装器: %SETUP_EXE%>> "%REPORT%"

REM --- 修复前状态快照 ---
echo.>> "%REPORT%"
echo --- 修复前状态 --- >> "%REPORT%"
if exist "C:\Windows\System32\drivers\PawnIO.sys" (
    echo PawnIO.sys: 存在>> "%REPORT%"
) else (
    echo PawnIO.sys: 缺失  ^<-- 半残特征>> "%REPORT%"
)
reg query "HKLM\SYSTEM\CurrentControlSet\Services\PawnIO" >nul 2>&1
if %errorlevel%==0 (
    echo 服务键: 存在>> "%REPORT%"
) else (
    echo 服务键: 缺失  ^<-- 半残特征>> "%REPORT%"
)
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO" >nul 2>&1
if %errorlevel%==0 (
    echo 卸载项: 存在（会骗过安装器，必须删）>> "%REPORT%"
) else (
    echo 卸载项: 不存在>> "%REPORT%"
)

REM --- 第 1 步：删除驱动包（含设备节点 + Class 绑定）---
echo.>> "%REPORT%"
echo --- 第 1 步：删除 PawnIO 驱动包 --- >> "%REPORT%"
set "FOUND_INF="
for %%f in ("C:\Windows\INF\oem*.inf") do (
    findstr /i /c:"Root\PawnIO" "%%f" >nul 2>&1 && set "FOUND_INF=%%~nxf"
)
if defined FOUND_INF (
    echo 找到驱动包: !FOUND_INF!>> "%REPORT%"
    pnputil.exe /delete-driver "!FOUND_INF!" /uninstall /force>> "%REPORT%" 2>&1
    echo [OK] 已执行 pnputil /delete-driver !FOUND_INF!>> "%REPORT%"
) else (
    echo 未找到 oem inf（可能已清理过），跳过>> "%REPORT%"
)

REM --- 第 2 步：删服务键 ---
echo.>> "%REPORT%"
echo --- 第 2 步：清理服务键 --- >> "%REPORT%"
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\PawnIO" /f >> "%REPORT%" 2>&1
echo 服务键已清理>> "%REPORT%"

REM --- 第 3 步：删卸载注册表项（关键！）---
echo.>> "%REPORT%"
echo --- 第 3 步：清理卸载注册表项（关键）--- >> "%REPORT%"
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO" /f >> "%REPORT%" 2>&1
reg delete "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO" /f >> "%REPORT%" 2>&1
echo 卸载项已清理（安装器现在会走全新安装分支）>> "%REPORT%"

timeout /t 2 /nobreak >nul

REM --- 第 4 步：静默安装 ---
echo.>> "%REPORT%"
echo --- 第 4 步：静默安装 PawnIO --- >> "%REPORT%"
echo 执行: !SETUP_EXE! -install -silent>> "%REPORT%"
start /wait "" "!SETUP_EXE!" -install -silent >> "%REPORT%" 2>&1
echo 安装器已退出>> "%REPORT%"

timeout /t 5 /nobreak >nul

REM --- 验证 ---
echo.>> "%REPORT%"
echo --- 修复后验证 --- >> "%REPORT%"
set "RESULT=0"
if exist "C:\Windows\System32\drivers\PawnIO.sys" (
    echo [PASS] PawnIO.sys 已就位>> "%REPORT%"
) else (
    echo [FAIL] PawnIO.sys 仍缺失>> "%REPORT%"
    set "RESULT=1"
)
reg query "HKLM\SYSTEM\CurrentControlSet\Services\PawnIO" >nul 2>&1
if %errorlevel%==0 (
    echo [PASS] 服务键已生成>> "%REPORT%"
    sc query PawnIO | findstr /c:"STATE" >> "%REPORT%" 2>&1
) else (
    echo [FAIL] 服务键仍未生成>> "%REPORT%"
    set "RESULT=1"
)

if %RESULT%==0 (
    echo.>> "%REPORT%"
    echo ===== 修复成功 =====>> "%REPORT%"
    echo 现在可以启动 TaskbarStudio.exe，CPU 温度应可正常读取。>> "%REPORT%"
) else (
    echo.>> "%REPORT%"
    echo ===== 修复未完全成功 =====>> "%REPORT%"
    echo 请把本报告发出来进一步分析。>> "%REPORT%"
)

echo.
echo ==============================================
echo 修复完成！报告已保存到:
echo   %REPORT%
echo ==============================================
echo.
type "%REPORT%"
pause
exit /b %RESULT%

:fail
echo.
echo 修复中断，请查看: %REPORT%
type "%REPORT%"
pause
exit /b 1
