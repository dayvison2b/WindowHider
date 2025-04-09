#include <windows.h>
#include <fstream>
#include <string>
#include <sstream>
#include <time.h>

HWND g_hwnd = NULL;
HMODULE g_hModule = NULL;
DWORD g_targetWindowHandle = 0;  // Store the target window handle as DWORD

// Function prototypes with proper export syntax
extern "C" {
    __declspec(dllexport) void HideWindowFromCapture(HWND hwnd);
    __declspec(dllexport) void ShowWindowInCapture(HWND hwnd);
    __declspec(dllexport) void SetTargetWindow(DWORD windowHandle);
    __declspec(dllexport) void UnloadSelf();
}

// Write log entry to a file
void WriteLog(const std::string& message) {
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    
    // Extract directory
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
    }
    
    // Create log file path
    std::string logPath = std::string(path) + "payload_log.txt";
    
    // Get timestamp
    time_t now = time(NULL);
    char timeBuffer[80];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Open file for appending
    std::ofstream logFile(logPath.c_str(), std::ios::app);
    if (logFile.is_open()) {
        logFile << "[" << timeBuffer << "] " << message << std::endl;
        logFile.close();
    }
}

// Function implementations
void HideWindowFromCapture(HWND hwnd) {
    std::stringstream ss;
    ss << "HideWindowFromCapture called for window: " << hwnd;
    WriteLog(ss.str());
    
    if (hwnd) {
        WriteLog("Attempting to set WDA_EXCLUDEFROMCAPTURE");
        BOOL result = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
        
        DWORD error = GetLastError();
        ss.str("");
        ss << "SetWindowDisplayAffinity result: " << (result ? "SUCCESS" : "FAILED") 
           << " (Error: " << error << ")";
        WriteLog(ss.str());
        
        // Check if set correctly
        DWORD affinity = 0;
        if (GetWindowDisplayAffinity(hwnd, &affinity)) {
            ss.str("");
            ss << "Current display affinity: " << affinity 
               << " (Should be " << WDA_EXCLUDEFROMCAPTURE << " for hidden)";
            WriteLog(ss.str());
        } else {
            WriteLog("Failed to get window display affinity");
        }
        
        g_hwnd = hwnd; // Store for later use
    } else {
        WriteLog("Error: Invalid window handle (NULL)");
    }
}

void ShowWindowInCapture(HWND hwnd) {
    std::stringstream ss;
    ss << "ShowWindowInCapture called for window: " << hwnd;
    WriteLog(ss.str());
    
    if (hwnd) {
        WriteLog("Attempting to set WDA_NONE");
        BOOL result = SetWindowDisplayAffinity(hwnd, WDA_NONE);
        
        DWORD error = GetLastError();
        ss.str("");
        ss << "SetWindowDisplayAffinity result: " << (result ? "SUCCESS" : "FAILED")
           << " (Error: " << error << ")";
        WriteLog(ss.str());
        
        g_hwnd = NULL; // Clear stored handle
    } else {
        WriteLog("Error: Invalid window handle (NULL)");
    }
}

void SetTargetWindow(DWORD windowHandle) {
    std::stringstream ss;
    ss << "SetTargetWindow called with window handle: 0x" << std::hex << windowHandle;
    WriteLog(ss.str());
    
    g_targetWindowHandle = windowHandle;
    g_hwnd = (HWND)windowHandle;
    
    if (g_hwnd) {
        HideWindowFromCapture(g_hwnd);
    } else {
        WriteLog("Error: Invalid window handle (NULL) passed to SetTargetWindow");
    }
}

// This function unloads the DLL from within the target process
void UnloadSelf() {
    WriteLog("UnloadSelf called - preparing to unload DLL");
    
    // Reset window display affinity before unloading
    if (g_hwnd != NULL) {
        WriteLog("Resetting display affinity to WDA_NONE before unloading");
        ShowWindowInCapture(g_hwnd);
        g_hwnd = NULL;
    }
    
    // Create a separate thread to unload the DLL
    // We can't call FreeLibrary directly or it will crash during DllMain's DLL_PROCESS_DETACH
    HMODULE hModule = g_hModule;
    if (hModule != NULL) {
        WriteLog("Creating thread to call FreeLibraryAndExitThread");
        HANDLE hThread = CreateThread(NULL, 0, 
            [](LPVOID lpParam) -> DWORD {
                // Small delay to ensure our calling function has returned
                Sleep(100);
                // This will unload the DLL and terminate the thread
                FreeLibraryAndExitThread((HMODULE)lpParam, 0);
                return 0; // Never reached
            }, 
            hModule, 0, NULL);
            
        if (hThread) {
            WriteLog("Self-unload thread created successfully");
            CloseHandle(hThread); // Close handle, thread will still run
        } else {
            WriteLog("Failed to create self-unload thread");
        }
    } else {
        WriteLog("Cannot unload - module handle is NULL");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    g_hModule = hModule; // Store module handle for log file path
    
    // Initialize stringstream outside the switch to fix the C2360 error
    std::stringstream ss;
    
    try {
        switch (reason) {
            case DLL_PROCESS_ATTACH: {
                // Disable thread notifications to reduce overhead
                DisableThreadLibraryCalls(hModule);
                
                WriteLog("DLL_PROCESS_ATTACH");
                // Don't hide any window here. Wait for SetTargetWindow to be called
                WriteLog("Waiting for SetTargetWindow to be called with the correct window handle");
                break;
            }
                
            case DLL_PROCESS_DETACH: {
                WriteLog("DLL_PROCESS_DETACH");
                // Show window when DLL is unloaded
                if (g_hwnd != NULL) {
                    ShowWindowInCapture(g_hwnd);
                }
                break;
            }
                
            default:
                break;
        }
        
        return TRUE;
    }
    catch (const std::exception& e) {
        try {
            // Log the exception
            WriteLog(std::string("Exception in DllMain: ") + e.what());
        }
        catch (...) {
            // If even logging fails, there's not much we can do
        }
        return FALSE;
    }
    catch (...) {
        try {
            // Log the unknown exception
            WriteLog("Unknown exception in DllMain");
        }
        catch (...) {
            // If even logging fails, there's not much we can do
        }
        return FALSE;
    }
} 