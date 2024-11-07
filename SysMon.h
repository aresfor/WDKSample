#pragma once

#ifdef MYDRIVER
#include <ntdef.h>
#include <ntifs.h>
#include <wdm.h>
#endif

#include <minwindef.h>
#include "SysMonCommon.h"
#include "AutoLock.h"
#include "FastMutex.h"

template <typename T>
struct FullItem
{
	LIST_ENTRY Entry;
	T Info;
};

struct Globals
{
	LIST_ENTRY ItemsHeader;
	int ItemCount;
	FastMutex Mutex;

	void Init()
	{
		if (!_hasInitialized)
		{
			_hasInitialized = true;
			InitializeListHead(&ItemsHeader);
			Mutex.Init();
		}

	}
private:
	bool _hasInitialized;
};