#include "../include/window_manager.h"
#include "../include/injector.h"
#include <windows.h>
#include <shlobj.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <Shlwapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace WindowHider {

// Define the utf16_to_utf8 function in the namespace scope
std::string utf16_to_utf8(const std::wstring& utf16) {
    if (utf16.empty()) return std::string();
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &utf16[0], (int)utf16.size(), NULL, 0, NULL, NULL);
    std::string utf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &utf16[0], (int)utf16.size(), &utf8[0], size_needed, NULL, NULL);
    return utf8;
}

// Helper function to trim whitespace from a string
std::wstring trim(const std::wstring& str) {
    auto start = std::find_if_not(str.begin(), str.end(), [](wchar_t c) {
        return iswspace(c);
    });
    
    auto end = std::find_if_not(str.rbegin(), str.rend(), [](wchar_t c) {
        return iswspace(c);
    }).base();
    
    return (start < end) ? std::wstring(start, end) : std::wstring();
}

// Helper struct for EnumWindows callback
struct EnumWindowsData {
    std::unordered_map<HWND, std::wstring> windows;
    WindowManager* manager;
};

// EnumWindows callback function
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumWindowsData* data = reinterpret_cast<EnumWindowsData*>(lParam);
    
    // Skip if window is not visible
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    
    // Skip child windows
    if (GetWindow(hwnd, GW_OWNER) != NULL) {
        return TRUE;
    }
    
    // Skip windows with certain styles
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW)) {
        return TRUE;
    }
    
    // Get process name
    std::wstring processName = data->manager->GetProcessName(hwnd);
    if (processName.empty()) {
        return TRUE;
    }
    
    // Skip windows that are too small
    RECT rect;
    GetWindowRect(hwnd, &rect);
    if ((rect.right - rect.left) < 100 || (rect.bottom - rect.top) < 100) {
        return TRUE;
    }
    
    // Add to list - use process name only
    data->windows[hwnd] = processName;
    
    return TRUE;
}

WindowManager::WindowManager() {
    // Get the path to the state file
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        stateFilePath = std::wstring(appDataPath) + L"\\WindowHider\\state.dat";
        std::filesystem::create_directories(std::filesystem::path(stateFilePath).parent_path());
        LoadState();
        ScanForHiddenWindows(); // Scan for hidden windows first
        RestoreHiddenWindows(); // Then restore any that need DLL injection
    }
}

WindowManager::~WindowManager() {
    SaveState();
    // Don't show windows on exit - let them stay hidden
}

void WindowManager::LoadState() {
    std::wifstream file(stateFilePath);
    if (!file.is_open()) return;

    std::wstring line;
    while (std::getline(file, line)) {
        std::wstringstream ss(line);
        DWORD processId;
        std::wstring title;
        ss >> processId;
        std::getline(ss, title);
        title = trim(title);
        
        // Find the window handle for this process
        HWND hwnd = NULL;
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            DWORD* targetProcessId = reinterpret_cast<DWORD*>(lParam);
            DWORD processId;
            GetWindowThreadProcessId(hwnd, &processId);
            if (processId == *targetProcessId) {
                *targetProcessId = reinterpret_cast<DWORD_PTR>(hwnd);
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&processId));

        if (processId != 0) {
            hwnd = reinterpret_cast<HWND>(static_cast<DWORD_PTR>(processId));
            hiddenWindows[hwnd] = WindowState(hwnd, GetWindowThreadProcessId(hwnd, &processId), title);
        }
    }
}

void WindowManager::SaveState() {
    std::wofstream file(stateFilePath);
    if (!file.is_open()) return;

    for (const auto& pair : hiddenWindows) {
        const WindowState& state = pair.second;
        file << state.processId << L" " << state.title << std::endl;
    }
}

bool WindowManager::VerifyWindowState(WindowState& state) {
    // Check if process still exists
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, state.processId);
    if (!hProcess) return false;
    CloseHandle(hProcess);

    // Check if window still exists
    if (!IsWindow(state.hwnd)) return false;

    // Check if window title matches
    std::wstring currentTitle = GetWindowTitle(state.hwnd);
    if (currentTitle != state.title) {
        state.title = currentTitle;
        state.lastUpdated = time(nullptr);
    }

    return true;
}

void WindowManager::CleanupInvalidEntries() {
    for (auto it = hiddenWindows.begin(); it != hiddenWindows.end();) {
        if (!VerifyWindowState(it->second)) {
            it = hiddenWindows.erase(it);
        } else {
            ++it;
        }
    }
}

void WindowManager::RestoreHiddenWindows() {
    CleanupInvalidEntries();
    
    for (auto& pair : hiddenWindows) {
        WindowState& state = pair.second;
        if (VerifyWindowState(state) && !state.isInjected) {
            Injector injector;
            if (injector.InjectDLL(state.processId, injector.GetDLLPath())) {
                state.isInjected = true;
                state.lastUpdated = time(nullptr);
            }
        }
    }
}

bool WindowManager::HideWindow(HWND hwnd) {
    std::cout << "[DEBUG] Starting hide process for window handle: " << hwnd << std::endl;
    
    if (!hwnd || !IsWindow(hwnd)) {
        std::cout << "[ERROR] Invalid window handle" << std::endl;
        return false;
    }
    
    // Check if already hidden
    if (IsWindowHidden(hwnd)) {
        std::cout << "[DEBUG] Window is already hidden" << std::endl;
        return true;
    }
    
    // Get window information
    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, sizeof(title)/sizeof(wchar_t));
    std::cout << "[DEBUG] Window title: " << utf16_to_utf8(title) << std::endl;
    
    // Get process ID
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    std::cout << "[DEBUG] Process ID: " << processId << std::endl;
    
    if (processId == 0) {
        std::cout << "[ERROR] Failed to get process ID" << std::endl;
        return false;
    }
    
    // Try to check window display affinity before injection
    DWORD affinity = 0;
    BOOL success = GetWindowDisplayAffinity(hwnd, &affinity);
    std::cout << "[DEBUG] Current display affinity: " << (success ? std::to_string(affinity) : "unknown") << std::endl;
    
    // Track window state
    WindowState state(hwnd, processId, title);
    state.lastUpdated = time(nullptr);
    
    // Store the window handle for later reference
    HWND hwndToHide = hwnd;
    
    // First try direct method (may only work if we have sufficient permissions)
    std::cout << "[DEBUG] Attempting direct SetWindowDisplayAffinity call..." << std::endl;
    BOOL directResult = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    std::cout << "[DEBUG] Direct SetWindowDisplayAffinity result: " << (directResult ? "SUCCESS" : "FAILED") << std::endl;
    
    if (directResult) {
        // Verify the direct call worked
        DWORD checkAffinity = 0;
        if (GetWindowDisplayAffinity(hwnd, &checkAffinity) && checkAffinity == WDA_EXCLUDEFROMCAPTURE) {
            std::cout << "[DEBUG] Direct method worked! Window is now hidden" << std::endl;
            state.isInjected = false;  // We didn't inject, but window is hidden
            hiddenWindows[hwnd] = state;
            return true;
        } else {
            std::cout << "[WARNING] SetWindowDisplayAffinity returned success but affinity is incorrect: " 
                      << checkAffinity << std::endl;
        }
    }
    
    // If direct method failed or wasn't reliable, try DLL injection
    std::cout << "[DEBUG] Trying DLL injection method..." << std::endl;
    
    // Get DLL path
    std::wstring dllPath = Injector::GetDLLPath();
    std::cout << "[DEBUG] Using DLL: " << utf16_to_utf8(dllPath) << std::endl;
    
    // Check if DLL exists
    if (!PathFileExistsW(dllPath.c_str())) {
        std::cout << "[ERROR] DLL file does not exist: " << utf16_to_utf8(dllPath) << std::endl;
        return false;
    }
    
    // Store the window handle in a table or global that the injector can access
    std::cout << "[DEBUG] Target window handle for hiding: 0x" << std::hex << (DWORD_PTR)hwndToHide << std::dec << std::endl;
    
    // Inject DLL
    std::cout << "[DEBUG] Attempting to inject DLL into process..." << std::endl;
    bool injected = Injector::InjectDLL(processId, dllPath);
    std::cout << "[DEBUG] DLL injection result: " << (injected ? "success" : "failed") << std::endl;
    
    if (!injected) {
        std::cout << "[ERROR] Failed to inject DLL" << std::endl;
        // Check if the direct method might have worked anyway
        DWORD finalAffinity = 0;
        if (GetWindowDisplayAffinity(hwnd, &finalAffinity) && finalAffinity == WDA_EXCLUDEFROMCAPTURE) {
            std::cout << "[DEBUG] DLL injection failed, but window appears to be hidden anyway" << std::endl;
            state.isInjected = false;
            hiddenWindows[hwnd] = state;
            return true;
        }
        return false;
    }
    
    state.isInjected = true;
    hiddenWindows[hwnd] = state;
    
    // Verify window is hidden by checking display affinity again
    affinity = 0;
    success = GetWindowDisplayAffinity(hwnd, &affinity);
    std::cout << "[DEBUG] Display affinity after injection: " << (success ? std::to_string(affinity) : "unknown") << std::endl;
    
    if (!success || affinity != WDA_EXCLUDEFROMCAPTURE) {
        std::cout << "[WARNING] Window may not be properly hidden. Affinity = " << affinity << std::endl;
    }
    
    return true;
}

bool WindowManager::ShowWindow(HWND hwnd) {
    std::cout << "[DEBUG] Starting show process for window handle: " << hwnd << std::endl;
    
    // Find the window state
    auto it = hiddenWindows.find(hwnd);
    if (it == hiddenWindows.end()) {
        std::cout << "[ERROR] Window not found in hidden windows list" << std::endl;
        return false;
    }
    
    WindowState& state = it->second;
    std::cout << "[DEBUG] Found window in hidden list, process ID: " << state.processId << std::endl;
    
    // Check display affinity before showing
    DWORD affinity = 0;
    BOOL success = GetWindowDisplayAffinity(hwnd, &affinity);
    std::cout << "[DEBUG] Current display affinity: " << (success ? std::to_string(affinity) : "unknown") << std::endl;
    
    // If this window wasn't hidden with DLL injection, just use direct method
    if (!state.isInjected) {
        std::cout << "[DEBUG] Window was hidden using direct method, trying direct unhide..." << std::endl;
        BOOL result = SetWindowDisplayAffinity(hwnd, WDA_NONE);
        std::cout << "[DEBUG] Direct SetWindowDisplayAffinity result: " << (result ? "success" : "failed") << std::endl;
        
        // Check if it worked
        DWORD checkAffinity = 0;
        if (GetWindowDisplayAffinity(hwnd, &checkAffinity)) {
            std::cout << "[DEBUG] New display affinity: " << checkAffinity << std::endl;
            if (checkAffinity == WDA_NONE) {
                hiddenWindows.erase(it);
                SaveState();
                return true;
            }
        }
        
        // If direct method failed, nothing more we can do
        if (!result) {
            std::cout << "[ERROR] Failed to unhide window with direct method" << std::endl;
            return false;
        }
    }
    
    // Otherwise use DLL ejection method
    // Get DLL path
    std::wstring dllPath = Injector::GetDLLPath();
    std::cout << "[DEBUG] Using DLL: " << utf16_to_utf8(dllPath) << std::endl;
    
    // Eject DLL
    std::cout << "[DEBUG] Attempting to eject DLL from process..." << std::endl;
    bool ejected = Injector::EjectDLL(state.processId, dllPath);
    std::cout << "[DEBUG] DLL ejection result: " << (ejected ? "success" : "failed") << std::endl;
    
    if (!ejected) {
        std::cout << "[ERROR] Failed to eject DLL" << std::endl;
        
        // Try to force the display affinity back to normal anyway
        std::cout << "[DEBUG] Attempting to force display affinity to WDA_NONE" << std::endl;
        BOOL forcedResult = SetWindowDisplayAffinity(hwnd, WDA_NONE);
        std::cout << "[DEBUG] Force display affinity result: ";
        if (forcedResult) {
            std::cout << "success" << std::endl;
        } else {
            std::cout << "failed (Error: " << GetLastError() << ")" << std::endl;
        }
        
        if (forcedResult) {
            // Even though DLL ejection failed, we successfully reset the display affinity
            hiddenWindows.erase(it);
            SaveState();
            return true;
        }
        
        return false;
    }
    
    // Check display affinity after showing
    affinity = 0;
    success = GetWindowDisplayAffinity(hwnd, &affinity);
    std::cout << "[DEBUG] Display affinity after DLL ejection: " << (success ? std::to_string(affinity) : "unknown") << std::endl;
    
    // Remove from hidden windows
    hiddenWindows.erase(it);
    SaveState();
    
    std::cout << "[DEBUG] Window successfully shown" << std::endl;
    return true;
}

bool WindowManager::IsWindowHidden(HWND hwnd) const {
    auto it = hiddenWindows.find(hwnd);
    if (it == hiddenWindows.end()) return false;
    return it->second.isInjected;
}

void WindowManager::ShowAllHiddenWindows() {
    // Make a copy of the keys since we'll be modifying the map during iteration
    std::vector<HWND> windowsToShow;
    for (const auto& pair : hiddenWindows) {
        windowsToShow.push_back(pair.first);
    }
    
    // Show all windows
    for (HWND hwnd : windowsToShow) {
        ShowWindow(hwnd);
    }
}

std::unordered_map<HWND, std::wstring> WindowManager::GetAllVisibleWindows() const {
    EnumWindowsData data;
    data.manager = const_cast<WindowManager*>(this);
    
    // Enumerate windows
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    
    return data.windows;
}

std::unordered_map<HWND, std::wstring> WindowManager::GetHiddenWindows() const {
    std::unordered_map<HWND, std::wstring> result;
    
    // Scan for hidden windows first
    const_cast<WindowManager*>(this)->ScanForHiddenWindows();
    
    for (const auto& pair : hiddenWindows) {
        HWND hwnd = pair.first;
        if (HasDisplayAffinity(hwnd)) {
            std::wstring processName = GetProcessName(hwnd);
            if (!processName.empty()) {
                result[hwnd] = processName;
            } else {
                result[hwnd] = GetWindowTitle(hwnd);
            }
        }
    }
    
    return result;
}

std::wstring WindowManager::GetWindowTitle(HWND hwnd) const {
    int length = GetWindowTextLengthW(hwnd);
    if (length == 0) {
        return L"";
    }
    
    std::wstring title(length + 1, L'\0');
    GetWindowTextW(hwnd, &title[0], length + 1);
    title.resize(length);
    
    return title;
}

std::wstring WindowManager::GetProcessName(HWND hwnd) const {
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    
    if (processId == 0) {
        return L"";
    }
    
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess == NULL) {
        return L"";
    }
    
    wchar_t processName[MAX_PATH];
    DWORD size = MAX_PATH;
    
    if (QueryFullProcessImageNameW(hProcess, 0, processName, &size)) {
        // Extract just the filename
        wchar_t* fileName = wcsrchr(processName, L'\\');
        
        if (fileName != NULL) {
            fileName++; // Skip the backslash
            
            // Remove the extension
            wchar_t* extension = wcsrchr(fileName, L'.');
            if (extension != NULL) {
                *extension = L'\0';
            }
            
            CloseHandle(hProcess);
            return fileName;
        }
    }
    
    CloseHandle(hProcess);
    return L"";
}

bool WindowManager::HasDisplayAffinity(HWND hwnd) const {
    DWORD affinity;
    if (GetWindowDisplayAffinity(hwnd, &affinity)) {
        return affinity == WDA_EXCLUDEFROMCAPTURE;
    }
    return false;
}

// Callback for scanning hidden windows
BOOL CALLBACK ScanHiddenWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd) || IsChild(NULL, hwnd)) {
        return TRUE;
    }

    auto* manager = reinterpret_cast<WindowManager*>(lParam);
    if (manager->HasDisplayAffinity(hwnd)) {
        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);
        
        wchar_t title[256];
        GetWindowTextW(hwnd, title, 256);
        
        WindowState state(hwnd, processId, title);
        state.isInjected = true;
        manager->hiddenWindows[hwnd] = state;
    }
    
    return TRUE;
}

void WindowManager::ScanForHiddenWindows() {
    EnumWindows(ScanHiddenWindowsProc, reinterpret_cast<LPARAM>(this));
}

}  // namespace WindowHider 