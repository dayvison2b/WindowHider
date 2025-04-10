#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <locale>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <codecvt>
#include <shlwapi.h>
#include "../include/window_manager.h"
#include "../include/injector.h"

#pragma comment(lib, "Shlwapi.lib")

// Forward declarations
void processCommand(const std::wstring& command, const std::wstring& args, bool& running);
std::vector<HWND> g_visibleWindows;
std::vector<HWND> g_hiddenWindows;

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

// Helper function to convert string to lowercase
std::wstring toLower(const std::wstring& str) {
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

// Helper function to convert UTF-8 to UTF-16
std::wstring utf8_to_utf16(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.size(), NULL, 0);
    std::wstring utf16(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.size(), &utf16[0], size_needed);
    return utf16;
}

// Helper function to convert UTF-16 to UTF-8
std::string utf16_to_utf8(const std::wstring& utf16) {
    if (utf16.empty()) return std::string();
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &utf16[0], (int)utf16.size(), NULL, 0, NULL, NULL);
    std::string utf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &utf16[0], (int)utf16.size(), &utf8[0], size_needed, NULL, NULL);
    return utf8;
}

// EnumWindows callback to collect properly visible windows
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    std::vector<HWND>* windows = reinterpret_cast<std::vector<HWND>*>(lParam);
    
    // Skip invisible windows
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    
    // Skip windows with certain styles
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW)) {
        return TRUE;
    }
    
    // Skip child/owned windows
    if (GetWindow(hwnd, GW_OWNER) != NULL) {
        return TRUE;
    }
    
    // Skip windows that are too small (likely system trays, etc.)
    RECT rect;
    GetWindowRect(hwnd, &rect);
    if ((rect.right - rect.left) < 100 || (rect.bottom - rect.top) < 100) {
        return TRUE;
    }
    
    // Get window title and process name
    wchar_t title[256];
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    
    wchar_t processPath[MAX_PATH] = {0};
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, processPath, &size)) {
            // Get just the filename
            wchar_t* fileName = PathFindFileNameW(processPath);
            
            // Convert to lowercase for comparison
            wchar_t fileNameLower[MAX_PATH];
            wcscpy(fileNameLower, fileName);
            _wcslwr(fileNameLower);
            
            // Filter out system processes
            if (wcscmp(fileNameLower, L"systemsettings.exe") == 0 ||
                wcscmp(fileNameLower, L"textinputhost.exe") == 0) {
                CloseHandle(hProcess);
                return TRUE;
            }
        }
        CloseHandle(hProcess);
    }
    
    // Get window title
    if (GetWindowTextW(hwnd, title, sizeof(title)/sizeof(wchar_t)) > 0) {
        // Skip empty title windows
        if (wcslen(title) > 0) {
            windows->push_back(hwnd);
        }
    }
    
    return TRUE;
}

// Helper function to get window title
std::wstring GetWindowTitleText(HWND hwnd) {
    // Get process name
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    
    wchar_t processName[MAX_PATH] = {0};
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, processName, &size)) {
            wchar_t* fileName = PathFindFileNameW(processName);
            
            // For UWP apps (ApplicationFrameHost.exe), use window title instead
            if (_wcsicmp(fileName, L"ApplicationFrameHost.exe") == 0) {
                wchar_t title[256];
                if (GetWindowTextW(hwnd, title, sizeof(title)/sizeof(wchar_t)) > 0) {
                    CloseHandle(hProcess);
                    return title;
                }
            }
            
            // For regular apps, return executable name without extension
            wchar_t* extension = wcsrchr(fileName, L'.');
            if (extension) *extension = L'\0';
            CloseHandle(hProcess);
            return fileName;
        }
        CloseHandle(hProcess);
    }
    
    return L"Unknown";
}

// Helper function to refresh the window lists
void RefreshWindowLists() {
    // Clear previous lists
    g_visibleWindows.clear();
    
    // Enumerate all visible windows
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&g_visibleWindows));
    
    // Get hidden windows
    auto& windowManager = WindowHider::WindowManager::GetInstance();
    auto hiddenWindowsMap = windowManager.GetHiddenWindows();
    
    g_hiddenWindows.clear();
    for (const auto& pair : hiddenWindowsMap) {
        g_hiddenWindows.push_back(pair.first);
    }
}

// Print help information
void printHelp() {
    std::wcout << L"WindowHider CLI - Hide windows from screen capture\n\n";
    std::wcout << L"Commands:\n";
    std::wcout << L"  list                  - List all visible windows\n";
    std::wcout << L"  hidden                - List all hidden windows\n";
    std::wcout << L"  hide -i <index>       - Hide a window by index\n";
    std::wcout << L"  show -i <index>       - Show a previously hidden window by index\n";
    std::wcout << L"  show-all              - Show all hidden windows\n";
    std::wcout << L"  help                  - Show this help information\n";
    std::wcout << L"  exit                  - Exit the program\n";
}

int main(int argc, char* argv[]) {
    // Set console output to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    
    // Initial window enumeration
    RefreshWindowLists();
    
    // If no arguments, run in interactive mode
    if (argc == 1) {
        // Print banner
        std::cout << "WindowHider CLI v1.0\n";
        std::cout << "Type 'help' for a list of commands\n\n";
        
        std::wstring line;
        bool running = true;
        
        while (running) {
            // Print prompt
            std::cout << "> ";
            std::getline(std::wcin, line);
            
            // Trim input
            line = trim(line);
            
            // Skip empty lines
            if (line.empty()) {
                continue;
            }
            
            // Parse command
            size_t spacePos = line.find(L' ');
            std::wstring command = spacePos != std::wstring::npos ? line.substr(0, spacePos) : line;
            std::wstring args = spacePos != std::wstring::npos ? line.substr(spacePos + 1) : L"";
            
            // Process command
            processCommand(command, args, running);
        }
    } else {
        // Non-interactive mode - process command line arguments
        std::wstring command = utf8_to_utf16(argv[1]);
        std::wstring args;
        
        // Combine all remaining arguments into a single string
        for (int i = 2; i < argc; i++) {
            if (i > 2) args += L" ";
            args += utf8_to_utf16(argv[i]);
        }
        
        bool dummy = false;
        processCommand(command, args, dummy);
    }
    
    return 0;
}

// Helper function to process commands
void processCommand(const std::wstring& command, const std::wstring& args, bool& running) {
    auto& windowManager = WindowHider::WindowManager::GetInstance();

    if (command == L"list") {
        // Refresh window lists
        RefreshWindowLists();
        
        // List all visible windows
        std::cout << "Visible windows:\n";
        
        if (g_visibleWindows.empty()) {
            std::cout << "  No visible windows found\n";
        } else {
            int index = 1;
            for (HWND hwnd : g_visibleWindows) {
                std::wstring title = GetWindowTitleText(hwnd);
                std::cout << "  " << index++ << ". " << utf16_to_utf8(title) << "\n";
            }
        }
    } else if (command == L"hidden") {
        // Refresh window lists
        RefreshWindowLists();
        
        // List all hidden windows
        std::cout << "Hidden windows:\n";
        
        if (g_hiddenWindows.empty()) {
            std::cout << "  No hidden windows found\n";
        } else {
            int index = 1;
            for (HWND hwnd : g_hiddenWindows) {
                std::wstring title = GetWindowTitleText(hwnd);
                std::cout << "  " << index++ << ". " << utf16_to_utf8(title) << "\n";
            }
        }
    } else if (command == L"hide") {
        // Parse arguments
        if (args.find(L"-i ") == 0) {
            try {
                int index = std::stoi(args.substr(3));
                // Refresh window lists
                RefreshWindowLists();
                
                if (index < 1 || index > g_visibleWindows.size()) {
                    std::cout << "Invalid window index. Use 'list' to see available windows.\n";
                    return;
                }
                
                HWND hwnd = g_visibleWindows[index - 1];
                if (windowManager.HideWindow(hwnd)) {
                    std::cout << "Window hidden successfully\n";
                } else {
                    std::cout << "Failed to hide window\n";
                }
            } catch (const std::exception&) {
                std::cout << "Invalid index format. Please provide a number.\n";
            }
        } else {
            std::cout << "Usage: hide -i <index>\n";
        }
    } else if (command == L"show") {
        // Parse arguments
        if (args.find(L"-i ") == 0) {
            try {
                int index = std::stoi(args.substr(3));
                // Refresh window lists
                RefreshWindowLists();
                
                if (index < 1 || index > g_hiddenWindows.size()) {
                    std::cout << "Invalid window index. Use 'hidden' to see available windows.\n";
                    return;
                }
                
                HWND hwnd = g_hiddenWindows[index - 1];
                if (windowManager.ShowWindow(hwnd)) {
                    std::cout << "Window shown successfully\n";
                } else {
                    std::cout << "Failed to show window\n";
                }
            } catch (const std::exception&) {
                std::cout << "Invalid index format. Please provide a number.\n";
            }
        } else {
            std::cout << "Usage: show -i <index>\n";
        }
    } else if (command == L"show-all") {
        windowManager.ShowAllHiddenWindows();
        std::cout << "All hidden windows have been shown\n";
    } else if (command == L"help") {
        printHelp();
    } else if (command == L"exit") {
        running = false;
    } else {
        std::cout << "Unknown command: " << utf16_to_utf8(command) << "\n";
        printHelp();
    }
} 