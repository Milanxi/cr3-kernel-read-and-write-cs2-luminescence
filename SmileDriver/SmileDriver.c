// 来自神秘的驱动大佬： 夕
#include <ntifs.h>
#include <ntddk.h>
#include <windef.h>
#include <wdf.h>
#include <intrin.h>
#pragma intrinsic(__readcr3, __writecr3)

// 前置声明，防止未定义和重定义
ULONG_PTR GetProcessCr3(PEPROCESS Process);
NTSTATUS ReadVirtualMemoryByCr3(ULONG_PTR DirectoryTableBase,ULONG_PTR Address,PVOID Buffer, SIZE_T Size,PSIZE_T BytesRead);
NTSTATUS WriteVirtualMemoryByCr3(ULONG_PTR DirectoryTableBase,ULONG_PTR Address,PVOID Buffer,SIZE_T Size,PSIZE_T BytesWritten);

// Forward declarations for dispatch and unload routines
NTSTATUS DriverDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
VOID DriverUnload(PDRIVER_OBJECT DriverObject);

// PEB related structures - reordered to fix compilation errors
typedef struct _PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    HANDLE SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
    BOOLEAN ShutdownInProgress;
    HANDLE ShutdownThreadId;
} 
PEB_LDR_DATA, *PPEB_LDR_DATA;

// PEB structure definition
typedef struct _PEB {
    BYTE Reserved1[0x18];
    PPEB_LDR_DATA Ldr;
    // ... other fields not needed
} PEB, *PPEB;

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    union {
        LIST_ENTRY HashLinks;
        struct {
            PVOID SectionPointer;
            ULONG CheckSum;
        };
    };
    union {
        ULONG TimeDateStamp;
        PVOID LoadedImports;
    };
    PVOID EntryPointActivationContext;
    PVOID PatchInformation;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

// PsGetProcessPeb is not in WDK headers, so declare it
EXTERN_C PPEB PsGetProcessPeb(PEPROCESS Process);

// 通信结构体定义
typedef struct _KERNEL_READ_REQUEST{
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
    WCHAR ModuleName[260]; // 统一为260
    ULONG_PTR Response;
} KERNEL_GET_MODULE_REQUEST, *PKERNEL_GET_MODULE_REQUEST;

// IOCTL codes
#define IOCTL_BASE 0x800
#define SMILE_DRIVER_MAGIC 0x8421

#define IOCTL_SMILE_READ_MEMORY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SMILE_WRITE_MEMORY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SMILE_GET_MODULE_BASE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 内存读写函数
NTSTATUS ReadVirtualMemory(PEPROCESS Process, PVOID SourceAddress, PVOID TargetAddress, SIZE_T Size)
{
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T bytes = 0;
    
    __try {
        if (Process == PsGetCurrentProcess()) {
            RtlCopyMemory(TargetAddress, SourceAddress, Size);
        } else {
            ULONG_PTR ProcessCr3 = GetProcessCr3(Process);
            status = ReadVirtualMemoryByCr3(
                ProcessCr3,
                (ULONG_PTR)SourceAddress,
                TargetAddress,
                Size,
                &bytes
            );
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }
    return status;
}

NTSTATUS WriteVirtualMemory(PEPROCESS Process, PVOID SourceAddress, PVOID TargetAddress, SIZE_T Size)
{
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T bytes = 0;
    
    __try {
        if (Process == PsGetCurrentProcess()) {
            RtlCopyMemory(TargetAddress, SourceAddress, Size);
        } else {
            ULONG_PTR ProcessCr3 = GetProcessCr3(Process);
            status = WriteVirtualMemoryByCr3(
                ProcessCr3,
                (ULONG_PTR)TargetAddress,
                SourceAddress,
                Size,
                &bytes
            );
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }
    return status;
}

// CR3相关结构体定义
typedef struct _VIRTUAL_MEMORY_REGION {
    ULONG_PTR StartAddress;
    ULONG_PTR EndAddress;
} VIRTUAL_MEMORY_REGION, *PVIRTUAL_MEMORY_REGION;

// CR3切换函数（x64 MSVC支持）
__forceinline ULONG_PTR SwitchCr3(ULONG_PTR NewCr3) {
    ULONG_PTR OldCr3 = __readcr3();
    __writecr3(NewCr3);
    return OldCr3;
}

// 获取进程CR3的函数实现
ULONG_PTR GetProcessCr3(PEPROCESS Process) {
    ULONG_PTR Cr3 = 0;
#ifdef _AMD64_
    Cr3 = *(PULONG_PTR)((ULONG_PTR)Process + 0x28);  // DirectoryTableBase offset for Windows 10+
#else
    Cr3 = *(PULONG_PTR)((ULONG_PTR)Process + 0x18);  // 32位系统的偏移
#endif
    return Cr3;
}

// 使用CR3读取内存的实现
NTSTATUS ReadVirtualMemoryByCr3(ULONG_PTR DirectoryTableBase, ULONG_PTR Address,PVOID Buffer, SIZE_T Size, PSIZE_T BytesRead) {
    if (!DirectoryTableBase || !Address || !Buffer || !Size) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    *BytesRead = 0;

    // 保存当前CR3并切换到目标CR3
    ULONG_PTR oldCr3 = SwitchCr3(DirectoryTableBase);

    __try {
        RtlCopyMemory(Buffer, (PVOID)Address, Size);
        *BytesRead = Size;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
    }

    // 恢复原始CR3
    SwitchCr3(oldCr3);

    return Status;
}

// 使用CR3写入内存的实现
NTSTATUS WriteVirtualMemoryByCr3(ULONG_PTR DirectoryTableBase,ULONG_PTR Address,PVOID Buffer,SIZE_T Size,PSIZE_T BytesWritten) {
    if (!DirectoryTableBase || !Address || !Buffer || !Size) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    *BytesWritten = 0;

    // 保存当前CR3并切换到目标CR3
    ULONG_PTR oldCr3 = SwitchCr3(DirectoryTableBase);

    __try {
        RtlCopyMemory((PVOID)Address, Buffer, Size);
        *BytesWritten = Size;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
    }

    // 恢复原始CR3
    SwitchCr3(oldCr3);

    return Status;
}

// 驱动入口点
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath){
    UNREFERENCED_PARAMETER(RegistryPath);
    UNICODE_STRING deviceName, symLink;
    PDEVICE_OBJECT deviceObject = NULL;
    
    // 初始化设备名和符号链接
    RtlInitUnicodeString(&deviceName, L"\\Device\\SmileDriver");
    RtlInitUnicodeString(&symLink, L"\\??\\SmileDriver");
    
    // 创建设备对象
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
    );
    
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    
    // 创建符号链接
    status = IoCreateSymbolicLink(&symLink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(deviceObject);
        return status;
    }
    
    // 设置驱动分发函数
    DriverObject->MajorFunction[IRP_MJ_CREATE] = 
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = 
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDispatch;
    DriverObject->DriverUnload = DriverUnload;
    
    return STATUS_SUCCESS;
}

// 驱动卸载函数
VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, L"\\??\\SmileDriver");
    IoDeleteSymbolicLink(&symLink);
    IoDeleteDevice(DriverObject->DeviceObject);
}

// 驱动分发函数
NTSTATUS DriverDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp){
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    
    switch (stack->MajorFunction){
        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
            break;
            
        case IRP_MJ_DEVICE_CONTROL:{
            ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
            PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
            ULONG inSize = stack->Parameters.DeviceIoControl.InputBufferLength;
            ULONG outSize = stack->Parameters.DeviceIoControl.OutputBufferLength;
            
            switch (controlCode) {
                case IOCTL_SMILE_READ_MEMORY:{
                    if (inSize >= sizeof(KERNEL_READ_REQUEST) && outSize >= ((PKERNEL_READ_REQUEST)buffer)->Size) {
                        PKERNEL_READ_REQUEST request = (PKERNEL_READ_REQUEST)buffer;
                        PEPROCESS process = NULL;
                        status = PsLookupProcessByProcessId((HANDLE)request->ProcessId, &process);

                        if (NT_SUCCESS(status)){
                            status = ReadVirtualMemory(process,(PVOID)request->Address,buffer, request->Size);
                            if (NT_SUCCESS(status)) {
                                information = request->Size;
                            }
                            ObDereferenceObject(process);
                        }
                    }
                    else{
                        status = STATUS_INVALID_PARAMETER;
                    }
                    break;
                }
                
                case IOCTL_SMILE_WRITE_MEMORY:{
                    if (inSize >= sizeof(KERNEL_WRITE_REQUEST)){
                        PKERNEL_WRITE_REQUEST request = (PKERNEL_WRITE_REQUEST)buffer;
                        PEPROCESS process = NULL;
                        status = PsLookupProcessByProcessId((HANDLE)request->ProcessId, &process);
                        
                        if (NT_SUCCESS(status)){
                            // 数据紧跟在结构体后面
                            PVOID dataPtr = (PUCHAR)request + sizeof(KERNEL_WRITE_REQUEST);
                            status = WriteVirtualMemory(process,dataPtr, (PVOID)request->Address,request->Size );
                            if (NT_SUCCESS(status)){
                                information = sizeof(KERNEL_WRITE_REQUEST);
                            }
                            ObDereferenceObject(process);
                        }
                    }
                    else
                    {
                        status = STATUS_INVALID_PARAMETER;
                    }
                    break;
                }
                
                case IOCTL_SMILE_GET_MODULE_BASE:
                {
                    if (inSize >= sizeof(KERNEL_GET_MODULE_REQUEST))
                    {
                        PKERNEL_GET_MODULE_REQUEST request = (PKERNEL_GET_MODULE_REQUEST)buffer;
                        PEPROCESS process = NULL;
                        status = PsLookupProcessByProcessId((HANDLE)request->ProcessId, &process);
                        
                        if (NT_SUCCESS(status))
                        {
                            PPEB peb = PsGetProcessPeb(process);
                            if (peb)
                            {
                                KAPC_STATE apc;
                                KeStackAttachProcess(process, &apc);
                                
                                __try {
                                    PPEB_LDR_DATA ldr = peb->Ldr;
                                    PLIST_ENTRY head = &ldr->InLoadOrderModuleList;
                                    PLIST_ENTRY current = head->Flink;

                                    // 手动初始化UNICODE_STRING
                                    UNICODE_STRING moduleName;
                                    moduleName.Buffer = request->ModuleName;
                                    moduleName.Length = (USHORT)(wcslen(request->ModuleName) * sizeof(WCHAR));
                                    moduleName.MaximumLength = sizeof(request->ModuleName);

                                    while (current != head){
                                        PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(current, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                                        
                                        if (RtlCompareUnicodeString(&entry->BaseDllName, &moduleName, TRUE) == 0){
                                            request->Response = (ULONG_PTR)entry->DllBase;
                                            information = sizeof(KERNEL_GET_MODULE_REQUEST);
                                            break;
                                        }
                                        
                                        current = current->Flink;
                                     }
                                }
                                __except(EXCEPTION_EXECUTE_HANDLER){
                                    status = GetExceptionCode();
                                }
                                
                                KeUnstackDetachProcess(&apc);
                            }
                            else{
                                status = STATUS_NOT_FOUND;
                            }
                            
                            ObDereferenceObject(process);
                        }
                    }
                    else {
                        status = STATUS_INVALID_PARAMETER;
                    }
                    break;
                }
                
                default:
                    status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;
        }
        
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}