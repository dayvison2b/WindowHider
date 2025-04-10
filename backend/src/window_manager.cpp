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
    
    // Get window title
    std::wstring windowTitle = data->manager->GetWindowTitle(hwnd);
    
    // Get process name
    std::wstring processName = data->manager->GetProcessName(hwnd);
    if (processName.empty() && windowTitle.empty()) {
        return TRUE;
    }
    
    // Skip windows that are too small
    RECT rect;
    GetWindowRect(hwnd, &rect);
    if ((rect.right - rect.left) < 100 || (rect.bottom - rect.top) < 100) {
        return TRUE;
    }
    
    // Store the process name for now - we'll enhance this later in GetAllVisibleWindows
    data->windows[hwnd] = processName;
    
    return TRUE;
}

// Add this helper function before the WindowManager class implementation
namespace {
    bool ShouldFilterProcess(const wchar_t* processName) {
        static const wchar_t* filteredProcesses[] = {
            L"TextInputHost.exe",
            L"SystemSettings.exe",
            L"SearchHost.exe",
            L"StartMenuExperienceHost.exe"
        };
        
        for (const auto& filtered : filteredProcesses) {
            if (_wcsicmp(processName, filtered) == 0) {
                return true;
            }
        }
        return false;
    }

    // Helper to get the real window title for UWP apps
    std::wstring GetUWPWindowInfo(HWND hwnd, DWORD& outProcessId) {
        wchar_t title[256];
        if (GetWindowTextW(hwnd, title, 256) > 0) {
            // Use window title if it's meaningful
            if (wcslen(title) > 0 && wcscmp(title, L"Windows") != 0) {
                return title;
            }
        }

        // Try to find the actual UWP app window
        DWORD parentProcessId = outProcessId;
        
        // First try the main window class
        wchar_t className[256];
        if (GetClassNameW(hwnd, className, 256)) {
            // Look for modern app identifiers in class name
            if (wcsstr(className, L"ApplicationFrame") != nullptr ||
                wcsstr(className, L"Windows.UI") != nullptr ||
                wcsstr(className, L"Microsoft.") != nullptr) {
                return title;  // Return the window title for modern apps
            }
        }

        // Then check child windows
        EnumChildWindows(hwnd, [](HWND childHwnd, LPARAM lParam) -> BOOL {
            auto* params = reinterpret_cast<std::pair<DWORD*, std::wstring*>*>(lParam);
            
            wchar_t childClass[256];
            if (GetClassNameW(childHwnd, childClass, 256)) {
                // Look for common UWP window classes
                if (wcscmp(childClass, L"Windows.UI.Core.CoreWindow") == 0 ||
                    wcsstr(childClass, L"ApplicationFrame") != nullptr) {
                    DWORD childProcessId;
                    GetWindowThreadProcessId(childHwnd, &childProcessId);
                    if (childProcessId != *params->first) {
                        *params->first = childProcessId;
                        
                        // Get the child window title
                        wchar_t childTitle[256];
                        if (GetWindowTextW(childHwnd, childTitle, 256) > 0) {
                            *params->second = childTitle;
                        }
                        return FALSE;  // Stop enumeration
                    }
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&std::make_pair(&outProcessId, &std::wstring())));

        return title;
    }
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
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    
    // Check if already hidden
    if (IsWindowHidden(hwnd)) {
        return true;
    }
    
    // Get process information
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        wchar_t processName[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, processName, &size)) {
            wchar_t* fileName = wcsrchr(processName, L'\\');
            if (fileName && ShouldFilterProcess(fileName + 1)) {
                CloseHandle(hProcess);
                return false;
            }
        }
        CloseHandle(hProcess);
    }
    
    // Get window information
    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, sizeof(title)/sizeof(wchar_t));
    
    // Create window state
    WindowState state(hwnd, processId, title);
    state.lastUpdated = time(nullptr);
    
    // Try direct method first
    BOOL directResult = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    
    if (directResult) {
        // Verify the change
        DWORD checkAffinity = 0;
        if (GetWindowDisplayAffinity(hwnd, &checkAffinity) && checkAffinity == WDA_EXCLUDEFROMCAPTURE) {
            state.isInjected = false;
            hiddenWindows[hwnd] = state;
            return true;
        }
    }
    
    // If direct method failed, try DLL injection
    std::wstring dllPath = Injector::GetDLLPath();
    if (!PathFileExistsW(dllPath.c_str())) {
        return false;
    }
    
    // Always inject the DLL into the process - each process has its own memory space
    Injector injector;
    bool injectionResult = injector.InjectDLL(processId, dllPath);
    
    if (!injectionResult) {
        std::cout << "[ERROR] Failed to inject DLL into process" << std::endl;
        return false;
    }
    
    // Call SetTargetWindow with this specific window handle
    if (!injector.CallExportedFunction(processId, dllPath, "SetTargetWindow", (DWORD_PTR)hwnd)) {
        std::cout << "[ERROR] Failed to call SetTargetWindow for window" << std::endl;
        return false;
    }
    
    state.isInjected = true;
    hiddenWindows[hwnd] = state;
    return true;
}

int WindowManager::GetProcessWindowCount(DWORD processId) const {
    int count = 0;
    for (const auto& pair : hiddenWindows) {
        if (pair.second.processId == processId) {
            count++;
        }
    }
    return count;
}

bool WindowManager::IsLastProcessWindow(HWND hwnd) const {
    auto it = hiddenWindows.find(hwnd);
    if (it == hiddenWindows.end()) {
        return true; // Not found, so it's the "last" by default
    }
    
    DWORD processId = it->second.processId;
    return GetProcessWindowCount(processId) <= 1;
}

bool WindowManager::ShowWindow(HWND hwnd) {
    std::cout << "[DEBUG] Starting show process for window handle: " << hwnd << std::endl;
    
    // First check if the window is actually still hidden
    if (!HasDisplayAffinity(hwnd)) {
        std::cout << "[DEBUG] Window is already shown (no display affinity)" << std::endl;
        
        // Remove from our hidden windows list if it's there
        auto it = hiddenWindows.find(hwnd);
        if (it != hiddenWindows.end()) {
            hiddenWindows.erase(it);
            SaveState();
        }
        
        return true;
    }
    
    // Find the window state
    auto it = hiddenWindows.find(hwnd);
    if (it == hiddenWindows.end()) {
        std::cout << "[ERROR] Window not found in hidden windows list" << std::endl;
        
        // Try direct method anyway
        BOOL directResult = SetWindowDisplayAffinity(hwnd, WDA_NONE);
        if (directResult) {
            std::cout << "[DEBUG] Successfully showed window via direct method" << std::endl;
            return true;
        }
        
        return false;
    }
    
    WindowState& state = it->second;
    std::cout << "[DEBUG] Found window in hidden list, process ID: " << state.processId << std::endl;
    
    // Check display affinity before showing
    DWORD affinity = 0;
    BOOL success = GetWindowDisplayAffinity(hwnd, &affinity);
    std::cout << "[DEBUG] Current display affinity: " << (success ? std::to_string(affinity) : "unknown") << std::endl;
    
    // Check if this window is using DLL injection
    bool isUsingDLL = state.isInjected;
    bool isLastWindow = IsLastProcessWindow(hwnd);
    
    std::cout << "[DEBUG] Window uses DLL injection: " << (isUsingDLL ? "yes" : "no") << std::endl;
    std::cout << "[DEBUG] Is last window for this process: " << (isLastWindow ? "yes" : "no") << std::endl;
    
    // First try direct method
    std::cout << "[DEBUG] Trying direct display affinity reset first..." << std::endl;
    BOOL directResult = SetWindowDisplayAffinity(hwnd, WDA_NONE);
    
    if (directResult) {
        std::cout << "[DEBUG] Successfully reset display affinity directly" << std::endl;
        
        // Check if it actually worked
        DWORD checkAffinity = 0;
        BOOL checkResult = GetWindowDisplayAffinity(hwnd, &checkAffinity);
        if (checkResult && checkAffinity == WDA_NONE) {
            hiddenWindows.erase(it);
            SaveState();
            return true;
        }
    } else {
        // If we get access denied (error 5), try to reinject the DLL
        if (GetLastError() == 5) {
            std::cout << "[DEBUG] Access denied when trying direct method, attempting to reinject DLL" << std::endl;
            // Try to reinject the DLL in case it was unloaded
            std::wstring dllPath = Injector::GetDLLPath();
            Injector injector;
            
            // Reinject the DLL
            bool reinjected = injector.InjectDLL(state.processId, dllPath);
            if (reinjected) {
                std::cout << "[DEBUG] Successfully reinjected DLL" << std::endl;
                
                // Call ShowWindowInCapture on the reinjected DLL
                bool callResult = injector.CallExportedFunction(
                    state.processId, 
                    dllPath, 
                    "ShowWindowInCapture", 
                    (DWORD_PTR)hwnd
                );
                
                if (callResult) {
                    std::cout << "[DEBUG] Successfully called ShowWindowInCapture after reinjection" << std::endl;
                    
                    // Verify that the window was actually shown
                    DWORD finalAffinity = 0;
                    if (GetWindowDisplayAffinity(hwnd, &finalAffinity) && finalAffinity == WDA_NONE) {
                        std::cout << "[DEBUG] Window display affinity confirmed reset to WDA_NONE" << std::endl;
                        hiddenWindows.erase(it);
                        SaveState();
                        return true;
                    }
                }
            }
        }
        
        std::cout << "[DEBUG] Direct display affinity reset failed, error: " << GetLastError() << std::endl;
    }
    
    // Try to use the existing DLL if it's still there
    if (isUsingDLL) {
        // Try to call the ShowWindowInCapture function in the DLL
        std::cout << "[DEBUG] Calling ShowWindowInCapture for this window..." << std::endl;
        
        std::wstring dllPath = Injector::GetDLLPath();
        Injector injector;
        bool callResult = injector.CallExportedFunction(
            state.processId, 
            dllPath, 
            "ShowWindowInCapture", 
            (DWORD_PTR)hwnd
        );
        
        if (callResult) {
            std::cout << "[DEBUG] Successfully called ShowWindowInCapture" << std::endl;
            
            // Verify that the window was actually shown
            DWORD finalAffinity = 0;
            if (GetWindowDisplayAffinity(hwnd, &finalAffinity) && finalAffinity == WDA_NONE) {
                std::cout << "[DEBUG] Window display affinity confirmed reset to WDA_NONE" << std::endl;
                hiddenWindows.erase(it);
                SaveState();
                return true;
            }
        } else {
            std::cout << "[ERROR] Failed to call ShowWindowInCapture" << std::endl;
        }
    }
    
    // If we got here, make one last attempt with administrator privileges
    // This is a fallback for the case where access is denied
    if (GetLastError() == 5) {
        std::cout << "[DEBUG] Access denied throughout - window may need elevated privileges to unhide" << std::endl;
        // Note: This would require elevation of the application, or it will fail
        std::cout << "[DEBUG] Making final attempt to unhide window..." << std::endl;
        
        // Last attempt to force the display affinity
        directResult = SetWindowDisplayAffinity(hwnd, WDA_NONE);
        if (directResult) {
            std::cout << "[DEBUG] Final attempt succeeded!" << std::endl;
            hiddenWindows.erase(it);
            SaveState();
            return true;
        }
    }
    
    // Final check if it worked by any means
    DWORD finalAffinity = 0;
    if (GetWindowDisplayAffinity(hwnd, &finalAffinity)) {
        std::cout << "[DEBUG] Final window display affinity: " << finalAffinity << std::endl;
        if (finalAffinity == WDA_NONE) {
            std::cout << "[DEBUG] Window successfully shown" << std::endl;
            hiddenWindows.erase(it);
            SaveState();
            return true;
        }
    }
    
    // If we got here, all attempts failed
    std::cout << "[ERROR] All attempts to show window failed" << std::endl;
    return false;
}

bool WindowManager::IsWindowHidden(HWND hwnd) const {
    // First check if we're tracking this window
    auto it = hiddenWindows.find(hwnd);
    if (it != hiddenWindows.end()) {
        return true; // Window is in our hidden windows map
    }
    
    // Also check display affinity in case it was hidden by another means
    return HasDisplayAffinity(hwnd);
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
    
    // Enhance the output with window titles
    std::unordered_map<HWND, std::wstring> enhancedData;
    for (const auto& pair : data.windows) {
        HWND hwnd = pair.first;
        std::wstring processName = pair.second;
        std::wstring windowTitle = GetWindowTitle(hwnd);
        
        // Combine process name with window title for better identification
        if (!windowTitle.empty()) {
            enhancedData[hwnd] = processName + L" - " + windowTitle;
        } else {
            enhancedData[hwnd] = processName;
        }
    }
    
    return enhancedData;
}

std::unordered_map<HWND, std::wstring> WindowManager::GetHiddenWindows() const {
    std::unordered_map<HWND, std::wstring> result;
    
    // Scan for hidden windows first
    const_cast<WindowManager*>(this)->ScanForHiddenWindows();
    
    for (const auto& pair : hiddenWindows) {
        HWND hwnd = pair.first;
        if (HasDisplayAffinity(hwnd)) {
            std::wstring processName = GetProcessName(hwnd);
            std::wstring windowTitle = GetWindowTitle(hwnd);
            
            // Combine process name with window title for better identification
            if (!processName.empty() && !windowTitle.empty()) {
                result[hwnd] = processName + L" - " + windowTitle;
            } else if (!processName.empty()) {
                result[hwnd] = processName;
            } else {
                result[hwnd] = windowTitle;
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
        wchar_t* fileName = wcsrchr(processName, L'\\');
        
        if (fileName != NULL) {
            fileName++; // Skip the backslash
            
            // Check if we should filter this process
            if (ShouldFilterProcess(fileName)) {
                CloseHandle(hProcess);
                return L"";
            }
            
            // Special handling for UWP apps and MS Teams
            if (_wcsicmp(fileName, L"ApplicationFrameHost.exe") == 0) {
                // Get window title first
                wchar_t title[256];
                if (GetWindowTextW(hwnd, title, 256) > 0) {
                    // Check for MS Teams in title
                    if (wcsstr(title, L"Microsoft Teams") != nullptr || 
                        wcsstr(title, L"Teams") != nullptr) {
                        CloseHandle(hProcess);
                        return L"Microsoft Teams";
                    }
                }
                
                // Check window class for Teams
                wchar_t className[256];
                if (GetClassNameW(hwnd, className, 256)) {
                    if (wcsstr(className, L"Teams") != nullptr || 
                        wcsstr(className, L"Microsoft.Teams") != nullptr) {
                        CloseHandle(hProcess);
                        return L"Microsoft Teams";
                    }
                }
                
                // For other UWP apps, try to find the actual process
                DWORD parentProcessId = processId;
                EnumChildWindows(hwnd, [](HWND childHwnd, LPARAM lParam) -> BOOL {
                    wchar_t className[256];
                    if (GetClassNameW(childHwnd, className, 256)) {
                        if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
                            DWORD childProcessId;
                            GetWindowThreadProcessId(childHwnd, &childProcessId);
                            if (childProcessId != *((DWORD*)lParam)) {
                                *((DWORD*)lParam) = childProcessId;
                                return FALSE;
                            }
                        }
                    }
                    return TRUE;
                }, (LPARAM)&processId);
                
                if (processId != parentProcessId) {
                    CloseHandle(hProcess);
                    hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
                    if (hProcess && QueryFullProcessImageNameW(hProcess, 0, processName, &size)) {
                        fileName = wcsrchr(processName, L'\\');
                        if (fileName) {
                            fileName++;
                        }
                    }
                }
            }
            
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

void WindowManager::ListWindows() {
    std::cout << "Visible windows:\n";
    int index = 1;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        if (IsWindowVisible(hwnd)) {
            char title[256];
            GetWindowTextA(hwnd, title, sizeof(title));
            
            // Skip empty titles
            if (strlen(title) == 0) return TRUE;
            
            // Get process name
            DWORD processId;
            GetWindowThreadProcessId(hwnd, &processId);
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
            char processName[MAX_PATH] = {0};
            
            if (hProcess) {
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameA(hProcess, 0, processName, &size)) {
                    std::cout << "[DEBUG] Raw process name: " << processName << std::endl << std::flush;
                    // Extract just the filename
                    char* lastSlash = strrchr(processName, '\\');
                    if (lastSlash) strcpy(processName, lastSlash + 1);
                    
                    // Debug log
                    std::cerr << "[DEBUG] Process name: " << processName << std::endl;
                    
                    // Convert to lowercase for comparison
                    char processNameLower[MAX_PATH];
                    strcpy(processNameLower, processName);
                    for (char* p = processNameLower; *p; ++p) *p = tolower(*p);
                    
                    // Debug log
                    std::cerr << "[DEBUG] Process name (lower): " << processNameLower << std::endl;
                    
                    // Filter out system processes
                    if (strcmp(processNameLower, "systemsettings.exe") == 0 ||
                        strcmp(processNameLower, "textinputhost.exe") == 0 ||
                        strcmp(processNameLower, "cursor.exe") == 0) {
                        std::cerr << "[DEBUG] Filtering out system process: " << processName << std::endl;
                        CloseHandle(hProcess);
                        return TRUE;
                    }
                    
                    // For UWP apps, use the window title instead
                    if (strcmp(processName, "ApplicationFrameHost.exe") == 0) {
                        if (strlen(title) > 0 && strcmp(title, "Windows") != 0) {
                            strcpy(processName, title);
                        }
                    }
                }
                CloseHandle(hProcess);
            }
            
            std::cout << "  " << *((int*)lParam) << ". " << processName << "\n";
            (*((int*)lParam))++;
        }
        return TRUE;
    }, (LPARAM)&index);
}

}  // namespace WindowHider 