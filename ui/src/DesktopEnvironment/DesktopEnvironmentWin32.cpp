#include "../../include/DesktopEnvironment/DesktopEnvironmentWin32.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

namespace gsr {
    DesktopEnvironmentWin32::DesktopEnvironmentWin32() {

    }

    bool DesktopEnvironmentWin32::start() {
        return true;
    }

    void DesktopEnvironmentWin32::update() {
        focused_hwnd = GetForegroundWindow();
    }

    std::string DesktopEnvironmentWin32::get_focused_window_title() {
        HWND hwnd = GetForegroundWindow();
        if(!hwnd)
            return "";

        /* Don't report the UI's own windows as the focused game. */
        char class_name[256];
        if(GetClassNameA(hwnd, class_name, sizeof(class_name)) > 0 && strstr(class_name, "gsr") != NULL)
            return "";

        /* Also skip tool/panel windows that shouldn't count as capture targets. */
        const LONG_PTR ex_style = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        if(ex_style & WS_EX_TOOLWINDOW)
            return "";

        wchar_t buffer[1024];
        const int length = GetWindowTextW(hwnd, buffer, sizeof(buffer) / sizeof(wchar_t));
        if(length <= 0)
            return "";

        /* Convert UTF-16 to UTF-8. */
        std::string result;
        const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, buffer, length, NULL, 0, NULL, NULL);
        if(utf8_size > 0) {
            result.resize(utf8_size);
            WideCharToMultiByte(CP_UTF8, 0, buffer, length, &result[0], utf8_size, NULL, NULL);
        }
        return result;
    }

    std::string DesktopEnvironmentWin32::get_focused_window_process_name() {
        HWND hwnd = GetForegroundWindow();
        if(!hwnd)
            return "";

        DWORD process_id = 0;
        GetWindowThreadProcessId(hwnd, &process_id);
        if(process_id == 0)
            return "";

        std::string result;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if(snapshot == INVALID_HANDLE_VALUE)
            return "";

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if(Process32FirstW(snapshot, &entry)) {
            do {
                if(entry.th32ProcessID == process_id) {
                    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, NULL, 0, NULL, NULL);
                    if(utf8_size > 0) {
                        result.resize(utf8_size - 1);
                        WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, &result[0], utf8_size, NULL, NULL);
                    }
                    break;
                }
            } while(Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }
}
