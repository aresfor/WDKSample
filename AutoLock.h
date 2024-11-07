#pragma once

template <typename T>
struct AutoLock
{
public:
	AutoLock(T& lock): _lock(lock)
	{
		lock.Lock();
	}

	~AutoLock()
	{
		_lock.UnLock();
	}
private:
	T& _lock;
};