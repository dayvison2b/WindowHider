#include "../include/injector.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <Shlwapi.h>
#include <iostream>
#include <string>
#pragma comment(lib, "Shlwapi.lib")

// Forward declare utf16_to_utf8 which is defined in window_manager.cpp
std::string utf16_to_utf8(const std::wstring& utf16);

bool Injector::InjectDLL(DWORD processId, const std::wstring& dllPath) {
    std::cout << "[DEBUG] Starting DLL injection for process ID: " << processId << std::endl;
    std::cout << "[DEBUG] DLL path: " << utf16_to_utf8(dllPath) << std::endl;
    
    // Check if DLL file exists
    if (!PathFileExistsW(dllPath.c_str())) {
        std::cout << "[ERROR] DLL file does not exist at: " << utf16_to_utf8(dllPath) << std::endl;
        return false;
    }
    
    // Open target process
    std::cout << "[DEBUG] Opening target process..." << std::endl;
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to open process (Error " << error << ")" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Process opened successfully" << std::endl;

    // Allocate memory for DLL path
    size_t pathSize = (dllPath.length() + 1) * sizeof(wchar_t);
    std::cout << "[DEBUG] Allocating " << pathSize << " bytes in target process for DLL path..." << std::endl;
    LPVOID dllPathAddr = VirtualAllocEx(hProcess, NULL, pathSize, 
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!dllPathAddr) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to allocate memory in target process (Error " << error << ")" << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] Memory allocated at 0x" << std::hex << dllPathAddr << std::dec << std::endl;

    // Write DLL path
    std::cout << "[DEBUG] Writing DLL path to target process memory..." << std::endl;
    if (!WriteProcessMemory(hProcess, dllPathAddr, dllPath.c_str(), pathSize, NULL)) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to write DLL path to process memory (Error " << error << ")" << std::endl;
        VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] DLL path written successfully" << std::endl;

    // Get LoadLibraryW address
    std::cout << "[DEBUG] Getting address of LoadLibraryW..." << std::endl;
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!loadLibraryAddr) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to get LoadLibraryW address (Error " << error << ")" << std::endl;
        VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] LoadLibraryW found at 0x" << std::hex << loadLibraryAddr << std::dec << std::endl;

    // Create remote thread to load DLL
    std::cout << "[DEBUG] Creating remote thread to load DLL..." << std::endl;
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)loadLibraryAddr, dllPathAddr, 0, NULL);

    if (!hThread) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to create remote thread (Error " << error << ")" << std::endl;
        VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] Remote thread created successfully" << std::endl;

    // Wait for thread completion
    std::cout << "[DEBUG] Waiting for remote thread to complete..." << std::endl;
    DWORD waitResult = WaitForSingleObject(hThread, 5000); // 5 second timeout
    if (waitResult != WAIT_OBJECT_0) {
        std::cout << "[ERROR] Remote thread wait failed or timed out (Result " << waitResult << ")" << std::endl;
        VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return false;
    }
    
    // Get thread exit code
    DWORD exitCode = 0;
    if (GetExitCodeThread(hThread, &exitCode)) {
        std::cout << "[DEBUG] Remote thread completed with exit code: 0x" << std::hex << exitCode << std::dec << std::endl;
        std::cout << "[DEBUG] Exit code should be the HMODULE of the loaded DLL, 0 means failure" << std::endl;
        if (exitCode == 0) {
            std::cout << "[ERROR] LoadLibraryW failed to load the DLL" << std::endl;
            VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
            CloseHandle(hThread);
            CloseHandle(hProcess);
            return false;
        }
    }
    
    // Verify DLL was loaded
    std::cout << "[DEBUG] Verifying DLL was loaded..." << std::endl;
    HMODULE hModule = GetRemoteModuleHandle(processId, PathFindFileNameW(dllPath.c_str()));
    if (!hModule) {
        std::cout << "[ERROR] Could not find DLL in target process module list" << std::endl;
        VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] DLL verified loaded at 0x" << std::hex << hModule << std::dec << std::endl;
    
    // Get the window handle to hide (passed as a parameter)
    HWND hwndToHide = FindWindowInProcess(processId);
    if (hwndToHide) {
        std::cout << "[DEBUG] Found target window handle: 0x" << std::hex << (DWORD_PTR)hwndToHide << std::dec << std::endl;
        
        // Get the address of the SetTargetWindow function in the DLL
        FARPROC setTargetWindowAddr = GetRemoteProcAddress(processId, hModule, "SetTargetWindow");
        if (setTargetWindowAddr) {
            std::cout << "[DEBUG] Found SetTargetWindow function at 0x" << std::hex << setTargetWindowAddr << std::dec << std::endl;
            
            // Create a remote thread to call SetTargetWindow with the window handle
            HANDLE hSetTargetThread = CreateRemoteThread(
                hProcess, 
                NULL, 
                0, 
                (LPTHREAD_START_ROUTINE)setTargetWindowAddr, 
                (LPVOID)(DWORD_PTR)hwndToHide, 
                0, 
                NULL
            );
            
            if (hSetTargetThread) {
                std::cout << "[DEBUG] Created remote thread to call SetTargetWindow" << std::endl;
                WaitForSingleObject(hSetTargetThread, 2000);
                CloseHandle(hSetTargetThread);
            } else {
                std::cout << "[ERROR] Failed to create remote thread for SetTargetWindow (Error " << GetLastError() << ")" << std::endl;
            }
        } else {
            std::cout << "[ERROR] Could not find SetTargetWindow function in DLL" << std::endl;
        }
    } else {
        std::cout << "[ERROR] Could not find target window handle" << std::endl;
    }
    
    // Cleanup
    std::cout << "[DEBUG] Cleaning up injection resources..." << std::endl;
    VirtualFreeEx(hProcess, dllPathAddr, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    
    std::cout << "[DEBUG] DLL injection successful" << std::endl;
    return true;
}

bool Injector::EjectDLL(DWORD processId, const std::wstring& dllPath) {
    std::cout << "[DEBUG] Starting DLL ejection for process ID: " << processId << std::endl;
    std::cout << "[DEBUG] DLL path: " << utf16_to_utf8(dllPath) << std::endl;
    
    // Open target process
    std::cout << "[DEBUG] Opening target process..." << std::endl;
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to open process (Error " << error << ")" << std::endl;
        return false;
    }
    
    // Get module handle in remote process
    std::cout << "[DEBUG] Getting module handle in remote process..." << std::endl;
    HMODULE hModule = GetRemoteModuleHandle(processId, dllPath);
    if (!hModule) {
        std::cout << "[ERROR] Could not find DLL in target process" << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] Found DLL module at 0x" << std::hex << hModule << std::dec << std::endl;

    // First try calling the UnloadSelf function in the DLL
    std::cout << "[DEBUG] Trying to call UnloadSelf function in DLL..." << std::endl;
    FARPROC unloadSelfAddr = GetRemoteProcAddress(processId, hModule, "UnloadSelf");
    
    if (unloadSelfAddr) {
        std::cout << "[DEBUG] Found UnloadSelf function at 0x" << std::hex << unloadSelfAddr << std::dec << std::endl;
        
        // Create remote thread to call UnloadSelf
        HANDLE hUnloadThread = CreateRemoteThread(hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)unloadSelfAddr, NULL, 0, NULL);
            
        if (hUnloadThread) {
            std::cout << "[DEBUG] UnloadSelf remote thread created successfully" << std::endl;
            
            // Wait for thread completion
            DWORD waitResult = WaitForSingleObject(hUnloadThread, 2000); // 2 second timeout
            if (waitResult == WAIT_OBJECT_0) {
                std::cout << "[DEBUG] UnloadSelf thread completed" << std::endl;
            } else {
                std::cout << "[DEBUG] UnloadSelf thread wait failed or timed out (Result " << waitResult << ")" << std::endl;
            }
            
            CloseHandle(hUnloadThread);
            
            // Check if DLL is still loaded
            Sleep(200); // Give time for FreeLibraryAndExitThread to execute
            HMODULE checkModule = GetRemoteModuleHandle(processId, dllPath);
            if (!checkModule) {
                std::cout << "[DEBUG] DLL successfully unloaded by UnloadSelf" << std::endl;
                CloseHandle(hProcess);
                return true;
            } else {
                std::cout << "[DEBUG] DLL still loaded after UnloadSelf, trying FreeLibrary method" << std::endl;
            }
        } else {
            DWORD error = GetLastError();
            std::cout << "[ERROR] Failed to create remote thread for UnloadSelf (Error " << error << ")" << std::endl;
        }
    } else {
        std::cout << "[DEBUG] UnloadSelf function not found, using FreeLibrary method" << std::endl;
    }

    // Fallback to FreeLibrary method
    std::cout << "[DEBUG] Getting address of FreeLibrary..." << std::endl;
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID freeLibraryAddr = GetProcAddress(hKernel32, "FreeLibrary");
    if (!freeLibraryAddr) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to get FreeLibrary address (Error " << error << ")" << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] FreeLibrary found at 0x" << std::hex << freeLibraryAddr << std::dec << std::endl;

    // Create remote thread to unload DLL
    std::cout << "[DEBUG] Creating remote thread to unload DLL..." << std::endl;
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)freeLibraryAddr, hModule, 0, NULL);

    if (!hThread) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to create remote thread (Error " << error << ")" << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] Remote thread created successfully" << std::endl;

    // Wait for thread completion
    std::cout << "[DEBUG] Waiting for remote thread to complete..." << std::endl;
    DWORD waitResult = WaitForSingleObject(hThread, 5000); // 5 second timeout
    if (waitResult != WAIT_OBJECT_0) {
        std::cout << "[ERROR] Remote thread wait failed or timed out (Result " << waitResult << ")" << std::endl;
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return false;
    }
    
    // Get thread exit code
    DWORD exitCode = 0;
    if (GetExitCodeThread(hThread, &exitCode)) {
        std::cout << "[DEBUG] Remote thread completed with exit code: " << exitCode 
                 << " (Non-zero means success)" << std::endl;
        if (exitCode == 0) {
            std::cout << "[ERROR] FreeLibrary failed in the remote process" << std::endl;
            CloseHandle(hThread);
            CloseHandle(hProcess);
            return false;
        }
    }
    
    // Verify DLL was unloaded
    std::cout << "[DEBUG] Verifying DLL was unloaded..." << std::endl;
    HMODULE checkModule = GetRemoteModuleHandle(processId, dllPath);
    if (checkModule) {
        std::cout << "[WARNING] DLL still appears to be loaded at 0x" 
                 << std::hex << checkModule << std::dec << std::endl;
    } else {
        std::cout << "[DEBUG] DLL successfully unloaded" << std::endl;
    }
    
    // Cleanup
    std::cout << "[DEBUG] Cleaning up ejection resources..." << std::endl;
    CloseHandle(hThread);
    CloseHandle(hProcess);
    
    std::cout << "[DEBUG] DLL ejection successful" << std::endl;
    return true;
}

std::wstring Injector::GetDLLPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    
    // Always use payload.dll - single build approach
    PathAppendW(path, L"payload.dll");
    
    return path;
}

HMODULE Injector::GetRemoteModuleHandle(DWORD processId, const std::wstring& moduleName) {
    std::cout << "[DEBUG] Looking for module '" << utf16_to_utf8(moduleName) << "' in process " << processId << std::endl;
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to create module snapshot for process " << processId << " (Error: " << error << ")" << std::endl;
        return NULL;
    }
    
    MODULEENTRY32W me32 = {0};
    me32.dwSize = sizeof(MODULEENTRY32W);
    
    if (!Module32FirstW(hSnapshot, &me32)) {
        DWORD error = GetLastError();
        std::cout << "[ERROR] Failed to enumerate first module (Error: " << error << ")" << std::endl;
        CloseHandle(hSnapshot);
        return NULL;
    }
    
    HMODULE hModule = NULL;
    std::wstring targetModule = PathFindFileNameW(moduleName.c_str());
    
    std::cout << "[DEBUG] Target module basename: " << utf16_to_utf8(targetModule) << std::endl;
    std::cout << "[DEBUG] Enumerating modules in process " << processId << ":" << std::endl;
    
    do {
        std::wstring currentModule = me32.szModule;
        std::cout << "[DEBUG]  - " << utf16_to_utf8(currentModule) << " @ 0x" << std::hex << (DWORD_PTR)me32.hModule << std::dec << std::endl;
        
        // Case insensitive comparison of module names
        if (_wcsicmp(currentModule.c_str(), targetModule.c_str()) == 0) {
            hModule = me32.hModule;
            std::cout << "[DEBUG] Found matching module at 0x" << std::hex << (DWORD_PTR)hModule << std::dec << std::endl;
            break;
        }
    } while (Module32NextW(hSnapshot, &me32));
    
    CloseHandle(hSnapshot);
    
    if (!hModule) {
        std::cout << "[ERROR] Could not find DLL in target process" << std::endl;
    }
    
    return hModule;
}

FARPROC Injector::GetRemoteProcAddress(DWORD processId, HMODULE hModule, const char* procName) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) return NULL;

    // Get module info
    MODULEINFO moduleInfo;
    if (!GetModuleInformation(hProcess, hModule, &moduleInfo, sizeof(moduleInfo))) {
        CloseHandle(hProcess);
        return NULL;
    }

    // Read module headers
    IMAGE_DOS_HEADER dosHeader;
    if (!ReadProcessMemory(hProcess, hModule, &dosHeader, sizeof(dosHeader), NULL)) {
        CloseHandle(hProcess);
        return NULL;
    }

    IMAGE_NT_HEADERS ntHeaders;
    if (!ReadProcessMemory(hProcess, (BYTE*)hModule + dosHeader.e_lfanew, &ntHeaders, sizeof(ntHeaders), NULL)) {
        CloseHandle(hProcess);
        return NULL;
    }

    // Get export directory
    IMAGE_EXPORT_DIRECTORY exportDir;
    DWORD exportDirRVA = ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!ReadProcessMemory(hProcess, (BYTE*)hModule + exportDirRVA, &exportDir, sizeof(exportDir), NULL)) {
        CloseHandle(hProcess);
        return NULL;
    }

    // Read export tables
    std::vector<DWORD> functions(exportDir.NumberOfFunctions);
    std::vector<DWORD> names(exportDir.NumberOfNames);
    std::vector<WORD> ordinals(exportDir.NumberOfNames);

    if (!ReadProcessMemory(hProcess, (BYTE*)hModule + exportDir.AddressOfFunctions, functions.data(), functions.size() * sizeof(DWORD), NULL) ||
        !ReadProcessMemory(hProcess, (BYTE*)hModule + exportDir.AddressOfNames, names.data(), names.size() * sizeof(DWORD), NULL) ||
        !ReadProcessMemory(hProcess, (BYTE*)hModule + exportDir.AddressOfNameOrdinals, ordinals.data(), ordinals.size() * sizeof(WORD), NULL)) {
        CloseHandle(hProcess);
        return NULL;
    }

    // Find procedure
    for (DWORD i = 0; i < exportDir.NumberOfNames; i++) {
        char name[256];
        if (!ReadProcessMemory(hProcess, (BYTE*)hModule + names[i], name, sizeof(name), NULL)) {
            continue;
        }

        if (strcmp(name, procName) == 0) {
            DWORD funcRVA = functions[ordinals[i]];
            CloseHandle(hProcess);
            return (FARPROC)((BYTE*)hModule + funcRVA);
        }
    }

    CloseHandle(hProcess);
    return NULL;
}

// Helper to find a window in a process
HWND Injector::FindWindowInProcess(DWORD processId) {
    HWND result = NULL;
    
    // Callback for EnumWindows
    struct EnumData {
        DWORD targetProcessId;
        HWND hwndFound;
    };
    
    EnumData data = { processId, NULL };
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumData* data = reinterpret_cast<EnumData*>(lParam);
        
        // Skip invisible windows
        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }
        
        // Get process ID for window
        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);
        
        // If this window belongs to our target process, store it
        if (processId == data->targetProcessId) {
            data->hwndFound = hwnd;
            return FALSE; // Stop enumeration
        }
        
        return TRUE; // Continue enumeration
    }, reinterpret_cast<LPARAM>(&data));
    
    return data.hwndFound;
}

bool Injector::CallExportedFunction(DWORD processId, const std::wstring& dllPath, const char* functionName, DWORD_PTR parameter) {
    std::cout << "[DEBUG] Calling exported function '" << functionName << "' with parameter 0x" << std::hex << parameter << std::dec << std::endl;
    
    // Open target process
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) {
        std::cout << "[ERROR] Failed to open process (Error " << GetLastError() << ")" << std::endl;
        return false;
    }
    
    // Get module handle in remote process
    HMODULE hModule = GetRemoteModuleHandle(processId, PathFindFileNameW(dllPath.c_str()));
    
    // If DLL isn't loaded, try to inject it first
    if (!hModule) {
        std::cout << "[DEBUG] DLL not found in process, attempting injection" << std::endl;
        
        // Close the process handle before reinjection attempt
        CloseHandle(hProcess);
        
        // Try to inject the DLL
        if (!InjectDLL(processId, dllPath)) {
            std::cout << "[ERROR] Failed to inject DLL" << std::endl;
            return false;
        }
        
        // Reopen process
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
        if (!hProcess) {
            std::cout << "[ERROR] Failed to reopen process after injection (Error " << GetLastError() << ")" << std::endl;
            return false;
        }
        
        // Get the module handle again
        hModule = GetRemoteModuleHandle(processId, PathFindFileNameW(dllPath.c_str()));
        if (!hModule) {
            std::cout << "[ERROR] DLL still not found after injection attempt" << std::endl;
            CloseHandle(hProcess);
            return false;
        }
    }
    
    std::cout << "[DEBUG] Found DLL module at 0x" << std::hex << hModule << std::dec << std::endl;
    
    // Get the address of the function in the DLL
    FARPROC functionAddr = GetRemoteProcAddress(processId, hModule, functionName);
    if (!functionAddr) {
        std::cout << "[ERROR] Could not find function '" << functionName << "' in DLL" << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[DEBUG] Found function at 0x" << std::hex << functionAddr << std::dec << std::endl;
    
    // Create a remote thread to call the function with the given parameter
    HANDLE hThread = CreateRemoteThread(
        hProcess, 
        NULL, 
        0, 
        (LPTHREAD_START_ROUTINE)functionAddr, 
        (LPVOID)parameter, 
        0, 
        NULL
    );
    
    if (!hThread) {
        std::cout << "[ERROR] Failed to create remote thread (Error " << GetLastError() << ")" << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    
    // Wait for the thread to complete
    DWORD waitResult = WaitForSingleObject(hThread, 2000);
    if (waitResult != WAIT_OBJECT_0) {
        std::cout << "[ERROR] Wait for remote thread failed or timed out (Error " << GetLastError() << ")" << std::endl;
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return false;
    }
    
    // Get thread exit code 
    DWORD exitCode = 0;
    if (!GetExitCodeThread(hThread, &exitCode)) {
        std::cout << "[ERROR] Failed to get thread exit code (Error " << GetLastError() << ")" << std::endl;
    } else {
        std::cout << "[DEBUG] Thread completed with exit code: " << exitCode << std::endl;
    }
    
    // Cleanup
    CloseHandle(hThread);
    CloseHandle(hProcess);
    
    std::cout << "[DEBUG] Successfully called exported function" << std::endl;
    return true;
} 