#include <windows.h>
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <TlHelp32.h>

// Performance testing utilities
class PerformanceTimer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    
public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    double elapsed_ms() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000.0;
    }
};

// DLL injection helper functions
bool InjectDLL(DWORD processId, const std::wstring& dllPath) {
    // Get process handle
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) {
        std::cout << "Failed to open process: " << GetLastError() << std::endl;
        return false;
    }
    
    // Allocate memory in target process for DLL path
    size_t pathSize = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pDllPath = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT, PAGE_READWRITE);
    if (!pDllPath) {
        std::cout << "Failed to allocate memory in target process: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    
    // Write DLL path to target process
    if (!WriteProcessMemory(hProcess, pDllPath, dllPath.c_str(), pathSize, NULL)) {
        std::cout << "Failed to write DLL path to target process: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    // Get LoadLibraryW address
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID pLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");
    
    // Create remote thread to load DLL
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, 
                                       (LPTHREAD_START_ROUTINE)pLoadLibraryW, 
                                       pDllPath, 0, NULL);
    if (!hThread) {
        std::cout << "Failed to create remote thread: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    // Wait for thread to complete
    WaitForSingleObject(hThread, INFINITE);
    
    // Clean up
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    
    std::cout << "DLL injection successful!" << std::endl;
    return true;
}

// Self-injection function
bool InjectDLLIntoSelf(const std::wstring& dllPath) {
    std::cout << "Attempting to inject DLL into current process..." << std::endl;
    
    // Load the DLL directly into current process
    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        std::cout << "Failed to load DLL: " << GetLastError() << std::endl;
        return false;
    }
    
    std::cout << "DLL loaded successfully at address: " << hDll << std::endl;
    
    // Call the dummy export function to verify DLL is working
    typedef void (*DumbFunc)();
    DumbFunc dumbFunc = (DumbFunc)GetProcAddress(hDll, "dumb");
    if (dumbFunc) {
        std::cout << "Calling dummy export function..." << std::endl;
        dumbFunc();
    }
    
    // Call test log function
    typedef void (*TestLogFunc)();
    TestLogFunc testLogFunc = (TestLogFunc)GetProcAddress(hDll, "testLog");
    if (testLogFunc) {
        std::cout << "Calling test log function..." << std::endl;
        testLogFunc();
    }
    
    // Call extension test function
    typedef bool (*TestExtFunc)(const char*);
    TestExtFunc testExtFunc = (TestExtFunc)GetProcAddress(hDll, "testExtension");
    if (testExtFunc) {
        std::cout << "Testing extension monitoring..." << std::endl;
        bool result = testExtFunc(".pdf");
        std::cout << "Extension .pdf should be monitored: " << (result ? "YES" : "NO") << std::endl;
        
        result = testExtFunc(".txt");
        std::cout << "Extension .txt should be monitored: " << (result ? "YES" : "NO") << std::endl;
        
        result = testExtFunc(".log");
        std::cout << "Extension .log should be monitored: " << (result ? "YES" : "NO") << std::endl;
    }
    
    return true;
}

// Test application that properly loads the DLL and tests API hooks
int main() {
    std::cout << "File Monitoring DLL Test Application" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << std::endl;
    
    // Get current directory and construct DLL path
    wchar_t currentDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, currentDir);
    std::wstring dllPath = std::wstring(currentDir) + L"\\sHookDLL.dll";
    
    std::cout << "Looking for DLL at: ";
    std::wcout << dllPath << std::endl;
    
    // Check if DLL exists
    DWORD fileAttr = GetFileAttributesW(dllPath.c_str());
    if (fileAttr == INVALID_FILE_ATTRIBUTES) {
        std::cout << "ERROR: DLL not found!" << std::endl;
        std::cout << "Please ensure sHookDLL.dll is in the same directory as this executable." << std::endl;
        std::cout << "Press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }
    
    std::cout << "DLL found successfully!" << std::endl;
    
    // Inject DLL into current process
    if (!InjectDLLIntoSelf(dllPath)) {
        std::cout << "ERROR: Failed to inject DLL!" << std::endl;
        std::cout << "Press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "DLL injection successful! Now testing file operations..." << std::endl;
    std::cout << std::endl;
    
    PerformanceTimer timer;
    
    // Test 1: Create and write to a monitored file (.pdf)
    std::cout << "Test 1: Creating and writing to test.pdf (should be monitored)..." << std::endl;
    {
        timer.start();
        HANDLE hFile = CreateFileW(
            L"test.pdf",
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* testData = "%PDF-1.4\nThis is a test PDF file for monitoring.\nThe DLL should log this write operation.\n";
            DWORD bytesWritten;
            
            WriteFile(hFile, testData, strlen(testData), &bytesWritten, NULL);
            CloseHandle(hFile);
            
            double elapsed = timer.elapsed_ms();
            std::cout << "[✓] PDF file created and written successfully (" << bytesWritten << " bytes) in " << elapsed << "ms" << std::endl;
        } else {
            std::cout << "[✗] Failed to create test.pdf" << std::endl;
        }
    }
    
    // Test 2: Create and write to another monitored file (.zip)
    std::cout << "Test 2: Creating and writing to test.zip (should be monitored)..." << std::endl;
    {
        timer.start();
        HANDLE hFile = CreateFileW(
            L"test.zip",
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* testData = "PK\x03\x04\nThis is a test ZIP file for monitoring.\n";
            DWORD bytesWritten;
            
            WriteFile(hFile, testData, strlen(testData), &bytesWritten, NULL);
            CloseHandle(hFile);
            
            double elapsed = timer.elapsed_ms();
            std::cout << "[✓] ZIP file created and written successfully (" << bytesWritten << " bytes) in " << elapsed << "ms" << std::endl;
        } else {
            std::cout << "[✗] Failed to create test.zip" << std::endl;
        }
    }
    
    // Test 3: Create and write to a non-monitored file (.log)
    std::cout << "Test 3: Creating and writing to test.log (should NOT be monitored)..." << std::endl;
    {
        timer.start();
        HANDLE hFile = CreateFileW(
            L"test.log",
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* testData = "This is a log file that should not be monitored.\n";
            DWORD bytesWritten;
            
            WriteFile(hFile, testData, strlen(testData), &bytesWritten, NULL);
            CloseHandle(hFile);
            
            double elapsed = timer.elapsed_ms();
            std::cout << "[✓] Log file created and written successfully (" << bytesWritten << " bytes) in " << elapsed << "ms" << std::endl;
        } else {
            std::cout << "[✗] Failed to create test.log" << std::endl;
        }
    }
    
    // Test 4: Read from the PDF file
    std::cout << "Test 4: Reading from test.pdf..." << std::endl;
    {
        timer.start();
        HANDLE hFile = CreateFileW(
            L"test.pdf",
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile != INVALID_HANDLE_VALUE) {
            char buffer[512];
            DWORD bytesRead;
            
            if (ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                double elapsed = timer.elapsed_ms();
                std::cout << "[✓] PDF file read successfully (" << bytesRead << " bytes) in " << elapsed << "ms" << std::endl;
            } else {
                std::cout << "[✗] Failed to read test.pdf" << std::endl;
            }
            
            CloseHandle(hFile);
        } else {
            std::cout << "[✗] Failed to open test.pdf for reading" << std::endl;
        }
    }
    
    // Test 5: Multiple small writes to test performance
    std::cout << "Test 5: Multiple small writes to performance_test.pdf..." << std::endl;
    {
        timer.start();
        HANDLE hFile = CreateFileW(
            L"performance_test.pdf",
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile != INVALID_HANDLE_VALUE) {
            for (int i = 0; i < 50; i++) {
                char buffer[64];
                sprintf(buffer, "Write operation %d\n", i + 1);
                DWORD bytesWritten;
                WriteFile(hFile, buffer, strlen(buffer), &bytesWritten, NULL);
            }
            CloseHandle(hFile);
            
            double elapsed = timer.elapsed_ms();
            std::cout << "[✓] Performance test completed (50 writes) in " << elapsed << "ms" << std::endl;
            std::cout << "  Average time per write: " << (elapsed / 50.0) << "ms" << std::endl;
        } else {
            std::cout << "[✗] Failed to create performance_test.pdf" << std::endl;
        }
    }
    
    // Test 6: Test different file extensions
    std::cout << "Test 6: Testing various file extensions..." << std::endl;
    {
        std::vector<std::wstring> testFiles = {
            L"test1.exe", L"test2.dll", L"test3.pdf", L"test4.zip", L"test5.avi",
            L"test6.txt", L"test7.doc", L"test8.xlsx", L"test9.ppt", L"test10.rar"
        };
        
        for (const auto& file : testFiles) {
            HANDLE hFile = CreateFileW(
                file.c_str(),
                GENERIC_WRITE,
                0,
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            if (hFile != INVALID_HANDLE_VALUE) {
                const char* data = "Extension test data\n";
                DWORD bytesWritten;
                WriteFile(hFile, data, strlen(data), &bytesWritten, NULL);
                CloseHandle(hFile);
                std::wcout << "[✓] Created: " << file << std::endl;
            } else {
                std::wcout << "[✗] Failed to create: " << file << std::endl;
            }
        }
    }
    
    // Test 7: Call performance stats function
    std::cout << "Test 7: Getting performance statistics..." << std::endl;
    {
        HMODULE hDll = GetModuleHandleW(dllPath.c_str());
        if (hDll) {
            typedef void (*GetStatsFunc)();
            GetStatsFunc getStatsFunc = (GetStatsFunc)GetProcAddress(hDll, "getStats");
            if (getStatsFunc) {
                getStatsFunc();
                std::cout << "[✓] Performance stats requested" << std::endl;
            } else {
                std::cout << "[✗] getStats function not found" << std::endl;
            }
        }
    }
    
    std::cout << std::endl;
    std::cout << "ALL TESTS COMPLETED!" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Check the following locations for log files:" << std::endl;
    std::cout << "- log.txt (current directory)" << std::endl;
    std::cout << "- D:\\log.txt" << std::endl;
    std::cout << "- C:\\temp\\log.txt" << std::endl;
    std::cout << "- C:\\Windows\\Temp\\log.txt" << std::endl;
    std::cout << "- %TEMP%\\log.txt" << std::endl;
    std::cout << std::endl;
    std::cout << "Expected log entries:" << std::endl;
    std::cout << "- DLL injection and process information" << std::endl;
    std::cout << "- File operations on .pdf, .zip, .avi, .doc, .xlsx, .ppt, .rar files" << std::endl;
    std::cout << "- Read/write byte counts for monitored files" << std::endl;
    std::cout << "- Performance statistics" << std::endl;
    std::cout << std::endl;
    std::cout << "Press any key to exit..." << std::endl;
    std::cin.get();
    
    return 0;
} 