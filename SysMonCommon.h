#pragma once

#ifdef MYDRIVER
#include <ntdef.h>
#endif
enum class EItemType :short
{
	None,
	ProcessCreate,
	ProcessExit
};

struct ItemHeader
{
public:
	EItemType Type;
	USHORT Size;
	LARGE_INTEGER Time;
};

struct ProcessExitInfo :ItemHeader
{
	ULONG ProcessId;
};

struct ProcessCreateInfo :ItemHeader
{
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
};

