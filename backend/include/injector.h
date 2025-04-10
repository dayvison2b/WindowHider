#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>

class Injector {
public:
    static bool InjectDLL(DWORD processId, const std::wstring& dllPath);
    static bool EjectDLL(DWORD processId, const std::wstring& dllPath);
    static std::wstring GetDLLPath();
    static HMODULE GetRemoteModuleHandle(DWORD processId, const std::wstring& moduleName);
    static FARPROC GetRemoteProcAddress(DWORD processId, HMODULE hModule, const char* procName);
    static HWND FindWindowInProcess(DWORD processId);
    static bool CallExportedFunction(DWORD processId, const std::wstring& dllPath, const char* functionName, DWORD_PTR parameter);
}; 