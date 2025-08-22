#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <vector>
#include <iostream>

using namespace std;

// IOCTL codes
#define IOCTL_BASE 0x800
#define SMILE_DRIVER_MAGIC 0x8421

#define IOCTL_SMILE_READ_MEMORY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SMILE_WRITE_MEMORY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SMILE_GET_MODULE_BASE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 通信结构体定义
typedef struct _KERNEL_READ_REQUEST
{
    ULONG ProcessId;
    ULONG_PTR Address;
    SIZE_T Size;
} KERNEL_READ_REQUEST, *PKERNEL_READ_REQUEST;

typedef struct _KERNEL_WRITE_REQUEST
{
    ULONG ProcessId;
    ULONG_PTR Address;
    SIZE_T Size;
    // 后面紧跟要写入的数据
} KERNEL_WRITE_REQUEST, *PKERNEL_WRITE_REQUEST;

typedef struct _KERNEL_GET_MODULE_REQUEST
{
    ULONG ProcessId;
    WCHAR ModuleName[260];
    ULONG_PTR Response;
} KERNEL_GET_MODULE_REQUEST, *PKERNEL_GET_MODULE_REQUEST;

class Memory {
private:
    HANDLE deviceHandle;

public:
    DWORD processId = 0;

    Memory(const wchar_t* processName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W processEntry;
            processEntry.dwSize = sizeof(processEntry);

            if (Process32FirstW(snapshot, &processEntry)) {
                do {
                    if (_wcsicmp(processEntry.szExeFile, processName) == 0) {
                        processId = processEntry.th32ProcessID;
                        deviceHandle = CreateFileW(L"\\\\.\\SmileDriver", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                        break;
                    }
                } while (Process32NextW(snapshot, &processEntry));
            }
            CloseHandle(snapshot);
        }
    }

    ~Memory() {
        if (deviceHandle != INVALID_HANDLE_VALUE && deviceHandle != nullptr) {
            CloseHandle(deviceHandle);
        }
    }

    template<typename T>
    T Read(uintptr_t address) {
        T value = T();
        ReadProcessMemory(address, &value, sizeof(T));
        return value;
    }

    template<typename T>
    bool Write(uintptr_t address, const T& value) {
        return WriteProcessMemory(address, &value, sizeof(T));
    }

    bool ReadProcessMemory(uintptr_t address, void* buffer, SIZE_T size) {
        if (!IsValid()) return false;
        
        KERNEL_READ_REQUEST request;
        request.ProcessId = processId;
        request.Address = address;
        request.Size = size;

        DWORD bytesReturned = 0;
        // 输出缓冲区直接为数据
        return DeviceIoControl(
            deviceHandle, 
            IOCTL_SMILE_READ_MEMORY, 
            &request, 
            sizeof(request),
            buffer, 
            size,
            &bytesReturned, 
            nullptr
        );
    }

    bool WriteProcessMemory(uintptr_t address, const void* buffer, SIZE_T size) {
        if (!IsValid()) return false;
        // 合并结构体和数据到一个缓冲区
        std::vector<uint8_t> writeBuffer(sizeof(KERNEL_WRITE_REQUEST) + size);
        KERNEL_WRITE_REQUEST* request = reinterpret_cast<KERNEL_WRITE_REQUEST*>(writeBuffer.data());
        request->ProcessId = processId;
        request->Address = address;
        request->Size = size;
        memcpy(writeBuffer.data() + sizeof(KERNEL_WRITE_REQUEST), buffer, size);

        DWORD bytesReturned = 0;
        return DeviceIoControl(
            deviceHandle, 
            IOCTL_SMILE_WRITE_MEMORY, 
            writeBuffer.data(), 
            (DWORD)writeBuffer.size(),
            nullptr, 
            0,
            &bytesReturned, 
            nullptr
        );
    }

    uintptr_t GetModuleAddress(const wchar_t* moduleName) {
        if (!IsValid()) return 0;

        KERNEL_GET_MODULE_REQUEST request = { 0 };
        request.ProcessId = processId;
        wcscpy_s(request.ModuleName, moduleName);
        request.Response = 0;

        DWORD bytesReturned = 0;
        bool success = DeviceIoControl(
            deviceHandle, 
            IOCTL_SMILE_GET_MODULE_BASE, 
            &request, 
            sizeof(request),
            &request, 
            sizeof(request),
            &bytesReturned, 
            nullptr
        );
        
        return success ? request.Response : 0;
    }

    bool IsValid() const {
        return processId != 0 && deviceHandle != INVALID_HANDLE_VALUE && deviceHandle != nullptr;
    }
};