#include <ntifs.h> //必须在ntddk.h前面
#include <ntddk.h>

//atoi
#include <stdlib.h>
//INVALID_HANDLE_VALUE
#include <handleapi.h>

#define MYDRIVER
#include "Common.h"
#include "SysMon.h"


//声明
void SampleUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS OnCreateOrClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS OnDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS OnRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS OnWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS CompleteIo(PIRP Irp, NTSTATUS status, ULONG_PTR info = 0);
void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
void SysUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS SysMonitorRead(PDEVICE_OBJECT, PIRP Irp);



//全局变量
Globals g_Globals;
PDEVICE_OBJECT g_DeviceObject = nullptr;

extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DeiverObject, _In_ PUNICODE_STRING RegistryPath)
{
	DbgPrint("Entering DriverEntry\n");

	KdPrint(("Sample Driver Load\n"));

	UNREFERENCED_PARAMETER(RegistryPath);

	g_Globals.Init();

	//设置Unload回调
	DeiverObject->DriverUnload = SysUnload;//SampleUnload;
	//设置分发例程
	DeiverObject->MajorFunction[IRP_MJ_CREATE] = OnCreateOrClose;
	DeiverObject->MajorFunction[IRP_MJ_CLOSE] = OnCreateOrClose;
	DeiverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = OnDeviceControl;
	DeiverObject->MajorFunction[IRP_MJ_READ] = SysMonitorRead;//OnRead;
	DeiverObject->MajorFunction[IRP_MJ_WRITE] = OnWrite;


	//创建设备对象
	NTSTATUS status = STATUS_SUCCESS;
	bool symLinkCreated = false;
	//方便执行dowhile循环外的finally代码，但只针对正常流程
	//因为如果抛出异常，需要用try等语句进行处理
	do
	{
		status = IoCreateDevice(
			DeiverObject,
			0,
			&devName,
			FILE_DEVICE_UNKNOWN,
			0,
			FALSE,
			&g_DeviceObject
		);

		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failed to create device object (0x%08X)\n", status));
			break;
		}
		//使用直接IO
		g_DeviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failed to create symbolLink (0x%08X)\n", status));
			break;
		}
		symLinkCreated = true;

		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failed to create process notify (0x%08X)\n", status));
			break;
		}

	}while(false);
	
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create symbol link (0x%08X)\n", status));

		if(symLinkCreated)
			IoDeleteSymbolicLink(&symLink);
		if(g_DeviceObject)
			IoDeleteDevice(g_DeviceObject);
	}

	return status;
}


NTSTATUS SysMonitorRead(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto usableReadBufferLength = stack->Parameters.Read.Length;

	auto status = STATUS_SUCCESS;
	auto readCount = 0;

	NT_ASSERT(Irp->MdlAddress);

	//强转成UCHAR*,方便后面对地址进行偏移操作
	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else
	{
		AutoLock<FastMutex> lock(g_Globals.Mutex);
		while (true)
		{
			//或者直接判断count
			if (IsListEmpty(&g_Globals.ItemsHeader))
			{
				break;
			}

			auto entry = RemoveHeadList(&g_Globals.ItemsHeader);
			auto item = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = item->Info.Size;

			if (usableReadBufferLength < size)
			{
				//剩余空间不够再插入一个通知
				InsertHeadList(&g_Globals.ItemsHeader, entry);
				break;
			}

			--g_Globals.ItemCount;
			memcpy(buffer, &item->Info, size);
			usableReadBufferLength -= size;
			readCount += size;

			buffer += size;

			//释放内存
			ExFreePool(item);
		}

	}

	CompleteIo(Irp, status, readCount);
	return status;
}

void SysUnload(PDRIVER_OBJECT DriverObject)
{
	KdPrint(("Sample Driver UnLoad!\n"));
	//取消回调
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);

	IoDeleteSymbolicLink(&symLink);

	IoDeleteDevice(DriverObject->DeviceObject);

	while (IsListEmpty(&g_Globals.ItemsHeader))
	{
		auto entry =RemoveHeadList(&g_Globals.ItemsHeader);
		
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
	
	}
}


void PushItem(LIST_ENTRY* entry)
{
	AutoLock<FastMutex> lock(g_Globals.Mutex);

	HANDLE hDeviceKey;
	int maxItemCount = 1024;
	OBJECT_ATTRIBUTES ObjAttr;
	UNICODE_STRING KeyName, ValueName;

	RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\sample");
	RtlInitUnicodeString(&ValueName, L"DefaultProcessItemCount");

	InitializeObjectAttributes(&ObjAttr, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	NTSTATUS status = ZwOpenKey(&hDeviceKey, KEY_READ, &ObjAttr);
	ULONG resultLength;

	BYTE buffer[256];
	PKEY_VALUE_PARTIAL_INFORMATION pInfo = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;


	//如果openkey失败，看看能否创建
	if (!NT_SUCCESS(status))
	{
		if (status == STATUS_INVALID_DEVICE_REQUEST
			|| status == STATUS_OBJECT_NAME_NOT_FOUND)
		{
			KdPrint(("keyvalue not found, create new\n"));

			ULONG createInfo;
			status = ZwCreateKey(&hDeviceKey, KEY_ALL_ACCESS, &ObjAttr
				, 0, nullptr, REG_OPTION_VOLATILE, &createInfo);
			if (!NT_SUCCESS(status))
			{
				KdPrint(("Failed to Open REGE value, can not create key (0x%08X)\n", status));
				return;
			}
		}
		else
		{
			KdPrint(("Failed to Open REGE value with other error code (0x%08X)\n", status));
			return;
		}
		
	}

	//key存在，尝试查询值
	if (NT_SUCCESS(status)) {

		status = ZwQueryValueKey(hDeviceKey
			, &ValueName, KeyValuePartialInformation
			, pInfo, sizeof(buffer), &resultLength);

		//查询到了，就赋值
		if (NTSTATUS(status))
		{
			maxItemCount = atoi((const char*)pInfo->Data);
			KdPrint(("Success Change maxItemCount 2 %d\n", maxItemCount));
			if (maxItemCount == 0)
			{
				maxItemCount = 1024;
				KdPrint(("Read MaxItemCount is  0, change 2  1024\n"));

			}
		}
		else
		{
			KdPrint(("Failed to Query REGE value (0x%08X)\n", status));
		}
		
	}

	if(hDeviceKey != INVALID_HANDLE_VALUE)
	ZwClose(hDeviceKey);

	if (g_Globals.ItemCount > maxItemCount)
	{
		auto head = RemoveHeadList(&g_Globals.ItemsHeader);
		--g_Globals.ItemCount;
		//由于移除链表头，而链表头正好是FullItem的第一个对象
		//对应内存释放，指向实际入口的指针失效了
		//因此需要CONTAINING宏来获取当前的FullItem对象的起始位置
		auto info = CONTAINING_RECORD(head
			, FullItem<ProcessExitInfo>, Entry);
		ExFreePool(info);
	}
	//插入
	InsertTailList(&g_Globals.ItemsHeader, entry);
	++g_Globals.ItemCount;
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	//创建进程
	if (CreateInfo)
	{
		USHORT size = sizeof(FullItem<ProcessCreateInfo>);

		USHORT allocSize = size;
		USHORT commandLineSize = CreateInfo->CommandLine->Length;
		allocSize+= commandLineSize;

		auto item = (FullItem<ProcessCreateInfo>*)ExAllocatePool2(POOL_FLAG_PAGED
		, allocSize, DRIVER_TAG);
		if (item == nullptr)
		{
			KdPrint(("Failed allocate ProcessCreateInfo menmory\n"));
			return;
		}
		auto& info = item->Info;
		info.ProcessId = HandleToULong(ProcessId);
		KeQuerySystemTimePrecise(&info.Time);
		info.Type = EItemType::ProcessCreate;
		info.Size = allocSize;
		info.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
		//@TODO:添加映像名
		if (commandLineSize > 0)
		{
			info.CommandLineLength = commandLineSize /sizeof(WCHAR);
			memcpy((UCHAR*)item + size
			, CreateInfo->CommandLine->Buffer, commandLineSize);
			info.CommandLineOffset = size;
		}
		else
		{
			info.CommandLineLength = 0;
		}

		PushItem(&item->Entry);

	}
	//进程销毁
	else
	{
		//没有Tag参数和的申请函数有什么区别？WithTag后缀申请参数已过时
		auto item = (FullItem<ProcessExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (item == nullptr)
		{
			KdPrint(("Failed allocate ProcessExitInfo menmory\n"));
			return;
		}
		auto& info = item->Info;
		info.ProcessId = HandleToULong(ProcessId);
		KeQuerySystemTimePrecise(&info.Time);
		info.Type = EItemType::ProcessExit;
		info.Size = sizeof(ProcessExitInfo);

		PushItem(&item->Entry);
	}
}

void SampleUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	KdPrint(("Sample Driver UnLoad!\n"));

	IoDeleteSymbolicLink(&symLink);

	IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS OnCreateOrClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS OnDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	ThreadData* data = nullptr;
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTRL_SET_PRIORITY:
			if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ThreadData))
			{
				KdPrint(("Error: Buffer too small\n"));
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			data = (ThreadData*)stack->Parameters.DeviceIoControl.Type3InputBuffer;
			if (data == nullptr)
			{
				KdPrint(("Error: Invalid param\n"));

				status = STATUS_INVALID_PARAMETER;
				break;
			}

			if (data->Priority < 1 || data->Priority > 31)
			{
				KdPrint(("Error: Invalid thread priority param\n"));

				status = STATUS_INVALID_PARAMETER;
				break;
			}
			PETHREAD Thread;
			status = PsLookupThreadByThreadId(ULongToHandle(data->ThreadId), &Thread);
			if (!NT_SUCCESS(status))
			{
				KdPrint(("Error: threat not found, threadId:%d\n", data->ThreadId));

				break;
			}
			//这一步会显示增加线程的引用计数，因此后面一步要显式减少引用计数
			//否则线程无法正常释放，只能够重启解决，之前一直写错了，写成ReferenceObject了
			KeSetPriorityThread((PKTHREAD)Thread, data->Priority);
			ObDereferenceObject(Thread);
			KdPrint(("Thread Priority change for %d to %d\n", data->ThreadId, data->Priority));
			break;

		default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;

	}
	KdPrint(("Success: Complete IO, status:%d\n", status));

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;

}

NTSTATUS CompleteIo(PIRP Irp, NTSTATUS Status, ULONG_PTR Info)
{
	Irp->IoStatus.Status= Status;
	Irp->IoStatus.Information = Info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Status;
}

NTSTATUS OnRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto readLength = stack->Parameters.Read.Length;

	if (0 == readLength)
	{
		KdPrint(("Error: Read Length 0\n"));

		return CompleteIo(Irp, STATUS_INVALID_BUFFER_SIZE);
	}

	//直接IO，映射地址合法检测
	NT_ASSERT(Irp->MdlAddress);

	auto buffer =MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
	{
		KdPrint(("Error: Buffer Map 2 SystemBuffer Fail\n"));

		return CompleteIo(Irp, STATUS_INSUFFICIENT_RESOURCES);
	}

	//清空缓冲区
	memset(buffer, 0, readLength);

	return  CompleteIo(Irp, STATUS_SUCCESS, readLength);
}

NTSTATUS OnWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto writeLength = stack->Parameters.Write.Length;
	KdPrint(("Write Length: %d\n", writeLength));

	//相当于空设备，只是直接完成请求
	//这样也少了物理内存映射到系统内存，减少了消耗
	return CompleteIo(Irp, STATUS_SUCCESS, writeLength);
}

