#pragma once
#include <wdm.h>

struct FastMutex
{
	public:

	void Init();
	void Lock();
	void UnLock();
	private:
	FAST_MUTEX _mutex;
};