/*--------------------------------------------------------------------------------------------------------
  APIHIJACK.H - Based on DelayLoadProfileDLL.CPP, by Matt Pietrek for MSJ February 2000.
  http://msdn.microsoft.com/library/periodic/period00/hood0200.htm
  Adapted by Wade Brainerd, wadeb@wadeb.com
--------------------------------------------------------------------------------------------------------*/
#ifndef APIHIJACK_H
#define APIHIJACK_H

#include <winsock2.h>
#include <windows.h>
#include <setupapi.h>

DWORD CPID;
HANDLE PRS = 0;

struct SFunctionHook
{
	char* DllName;
	BOOL isHook;
	WORD  opcordLen;
	unsigned char* opcord;
	unsigned char* Hookopcord;
    char* Name;         // Function name, e.g. "DirectDrawCreateEx".
	WORD  Ordinal;
    void* HookFn;       // Address of your function.
    void* OrigFn;       // Stored by HookAPICalls, the address of the original function.
	
};

struct SDLLHook
{
    void* DefaultFn;
    // Function hook array.  Terminated with a NULL Name field.
    SFunctionHook* Functions;
};


bool HookFuncCalls( SDLLHook* Hook );

HANDLE WINAPI MyCreateFileA(LPCTSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE WINAPI MyCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile );
BOOL WINAPI MyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
BOOL WINAPI MyWriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
BOOL WINAPI MyCloseHandle(HANDLE hObject);

// Export function for DLL injection
__MIDL_DECLSPEC_DLLEXPORT void dumb();
__MIDL_DECLSPEC_DLLEXPORT void testLog();
__MIDL_DECLSPEC_DLLEXPORT bool testExtension(const char* extension);

enum
{
	APIHook_CreateFileA		= 0,
	APIHook_CreateFileW		= 1,
	APIHook_ReadFile		= 2,
	APIHook_WriteFile		= 3,
	APIHook_CloseHandle		= 4,
};

// Static array for function hooks
static SFunctionHook g_functionHooks[] = {
	{ "Kernel32.dll", true, 5, NULL, NULL, "CreateFileA", 0, MyCreateFileA, NULL },
	{ "Kernel32.dll", true, 5, NULL, NULL, "CreateFileW", 0, MyCreateFileW, NULL },
	{ "Kernel32.dll", true, 5, NULL, NULL, "ReadFile", 0, MyReadFile, NULL },
	{ "Kernel32.dll", true, 5, NULL, NULL, "WriteFile", 0, MyWriteFile, NULL },
	{ "Kernel32.dll", true, 5, NULL, NULL, "CloseHandle", 0, MyCloseHandle, NULL },
	{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

SDLLHook APIHook = 
{
    NULL,		// Default hook disabled, NULL function pointer.
    g_functionHooks
};

#endif