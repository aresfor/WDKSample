#pragma once

#ifdef MYDRIVER

#include <ntdef.h>
UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Sample");
UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Sample");
//#else
//#include <ntdef.h>

#endif

struct ThreadData
{
public:
	ULONG ThreadId;
	int Priority;
};
#define PRIORITY_DEVICE 0x8000

#define IOCTRL_SET_PRIORITY CTL_CODE(PRIORITY_DEVICE, 0x8000, METHOD_NEITHER, FILE_ANY_ACCESS)

#define DRIVER_TAG 'VDYM'



