#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <time.h>

namespace WindowHider {

// Forward declaration for callback
class WindowManager;

// Helper function to convert UTF-16 to UTF-8
std::string utf16_to_utf8(const std::wstring& utf16);

// Struct to track window state
struct WindowState {
    HWND hwnd;
    DWORD processId;
    std::wstring title;
    bool isInjected;
    time_t lastUpdated;

    WindowState() : hwnd(NULL), processId(0), isInjected(false), lastUpdated(0) {}
    
    WindowState(HWND h, DWORD pid, const std::wstring& t)
        : hwnd(h), processId(pid), title(t), isInjected(false), lastUpdated(time(nullptr)) {}
};

// Callback for scanning hidden windows
BOOL CALLBACK ScanHiddenWindowsProc(HWND hwnd, LPARAM lParam);

class WindowManager {
public:
    static WindowManager& GetInstance() {
        static WindowManager instance;
        return instance;
    }

    ~WindowManager();

    // Delete copy constructor and assignment operator
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    bool HideWindow(HWND hwnd);
    bool ShowWindow(HWND hwnd);
    bool IsWindowHidden(HWND hwnd) const;
    void ShowAllHiddenWindows();

    std::unordered_map<HWND, std::wstring> GetAllVisibleWindows() const;
    std::unordered_map<HWND, std::wstring> GetHiddenWindows() const;

    std::wstring GetWindowTitle(HWND hwnd) const;
    std::wstring GetProcessName(HWND hwnd) const;
    bool HasDisplayAffinity(HWND hwnd) const;

private:
    WindowManager();
    void LoadState();
    void SaveState();
    void RestoreHiddenWindows();
    void CleanupInvalidEntries();
    bool VerifyWindowState(WindowState& state);
    void ScanForHiddenWindows();

    std::unordered_map<HWND, WindowState> hiddenWindows;
    std::wstring stateFilePath;

    friend BOOL CALLBACK ScanHiddenWindowsProc(HWND hwnd, LPARAM lParam);
};
} 