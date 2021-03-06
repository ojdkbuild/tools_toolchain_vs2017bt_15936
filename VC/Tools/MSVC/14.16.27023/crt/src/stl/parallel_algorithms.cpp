// Copyright (c) Microsoft Corporation. All rights reserved.
#include <corecrt_terminate.h>
#include <internal_shared.h>
#include <intrin0.h>

// This must be as small as possible, because its contents are
// injected into the msvcprt.lib and msvcprtd.lib import libraries.
// Do not include or define anything else here.
// In particular, basic_string must not be included here.

// support for <execution>

#if _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8
 #pragma comment(lib, "synchronization") // for WaitOnAddress family
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8 */

#if _STL_WIN32_WINNT < _WIN32_WINNT_WIN8
namespace {
struct _Parallel_init_info
	{
	unsigned int _Hw_threads;
 #if _STL_WIN32_WINNT < _WIN32_WINNT_VISTA
	decltype(CreateThreadpoolWork) * _Pfn_CreateThreadpoolWork;
	decltype(SubmitThreadpoolWork) * _Pfn_SubmitThreadpoolWork;
	decltype(CloseThreadpoolWork) * _Pfn_CloseThreadpoolWork;
	decltype(WaitForThreadpoolWorkCallbacks) * _Pfn_WaitForThreadpoolWorkCallbacks;
	decltype(AcquireSRWLockExclusive) * _Pfn_AcquireSRWLockExclusive; // nullptr if _Pfn_WaitOnAddress is non-nullptr
	decltype(ReleaseSRWLockExclusive) * _Pfn_ReleaseSRWLockExclusive; // ditto
	decltype(SleepConditionVariableSRW) * _Pfn_SleepConditionVariableSRW; // ditto
	decltype(WakeAllConditionVariable) * _Pfn_WakeAllConditionVariable; // ditto
 #endif /* _STL_WIN32_WINNT < _WIN32_WINNT_VISTA */
	decltype(WaitOnAddress) * _Pfn_WaitOnAddress;
	decltype(WakeByAddressAll) * _Pfn_WakeByAddressAll;
	};

_Parallel_init_info * _Parallel_info;

struct _Wait_semaphore
	{
	SRWLOCK _Mtx;
	CONDITION_VARIABLE _Cv;
	};

constexpr int _Wait_table_size = 256; // one 4k page
constexpr int _Wait_table_max_index = _Wait_table_size - 1;
_Wait_semaphore _Wait_table[_Wait_table_size]{};
size_t _Choose_wait_entry(const volatile void * _Target) noexcept
	{
	auto _Num = reinterpret_cast<uintptr_t>(_Target);
#ifdef _WIN64
	_Num = (_Num & ((1ull << 32) - 1ull)) ^ (_Num >> 32); // down to 32 bits
#endif /* _WIN64 */
	_Num = (_Num & ((1u << 16) - 1u)) ^ (_Num >> 16); // to 16 bits
	_Num = (_Num & ((1u << 8) - 1u)) ^ (_Num >> 8); // to 8 bits
	static_assert(_Wait_table_max_index == (1 << 8) - 1, "Bad wait table size assumption");
	return (_Num);
	}

unsigned char _Atomic_load_uchar(const volatile unsigned char * _Ptr) noexcept
	{	// atomic load of unsigned char, copied from <atomic>
	unsigned char _Value;
#if defined(_M_ARM) || defined(_M_ARM64)
	_Value = __iso_volatile_load8(_Ptr);
	__dmb(0xB /* _ARM_BARRIER_ISH / _ARM64_BARRIER_ISH */)
#elif defined(_M_IX86) || defined(_M_X64)
	_Value = *_Ptr;
	_ReadWriteBarrier();
#else
 #error Unsupported architecture
#endif /* architecture */
	return (_Value);
	}

struct _Cleanup_parallel_algorithms
	{
	~_Cleanup_parallel_algorithms()
		{
		if (_Parallel_info)
			{
			VirtualFree(_Parallel_info, 0, MEM_RELEASE);
			}
		}
	};

_Cleanup_parallel_algorithms _Cleanup_instance;

bool _Initialize_parallel_init_info(_Parallel_init_info * _Result)
	{	// try to fill in _Result and return whether any parallelism is possible
	HMODULE _Kernel32 = GetModuleHandleW(L"kernel32.dll");
 #if _STL_WIN32_WINNT < _WIN32_WINNT_VISTA
	_Result->_Pfn_CreateThreadpoolWork = reinterpret_cast<decltype(CreateThreadpoolWork) *>(
		GetProcAddress(_Kernel32, "CreateThreadpoolWork"));
	_Result->_Pfn_SubmitThreadpoolWork = reinterpret_cast<decltype(SubmitThreadpoolWork) *>(
		GetProcAddress(_Kernel32, "SubmitThreadpoolWork"));
	_Result->_Pfn_CloseThreadpoolWork = reinterpret_cast<decltype(CloseThreadpoolWork) *>(
		GetProcAddress(_Kernel32, "CloseThreadpoolWork"));
	_Result->_Pfn_WaitForThreadpoolWorkCallbacks = reinterpret_cast<decltype(WaitForThreadpoolWorkCallbacks) *>(
		GetProcAddress(_Kernel32, "WaitForThreadpoolWorkCallbacks"));
	if (!_Result->_Pfn_CreateThreadpoolWork
		|| !_Result->_Pfn_SubmitThreadpoolWork
		|| !_Result->_Pfn_CloseThreadpoolWork
		|| !_Result->_Pfn_WaitForThreadpoolWorkCallbacks)
		{	// don't parallelize without the Vista threadpool
		return (false);
		}
 #endif /* _STL_WIN32_WINNT < _WIN32_WINNT_VISTA */

	_Result->_Pfn_WaitOnAddress = reinterpret_cast<decltype(WaitOnAddress) *>(
		GetProcAddress(_Kernel32, "WaitOnAddress"));
	_Result->_Pfn_WakeByAddressAll = reinterpret_cast<decltype(WakeByAddressAll) *>(
		GetProcAddress(_Kernel32, "WakeByAddressAll"));
	if ((_Result->_Pfn_WaitOnAddress == nullptr) != (_Result->_Pfn_WakeByAddressAll == nullptr))
		{	// if we don't have both we have neither
		_Result->_Pfn_WaitOnAddress = nullptr;
		_Result->_Pfn_WakeByAddressAll = nullptr;
		}

 #if _STL_WIN32_WINNT < _WIN32_WINNT_VISTA
	if (_Result->_Pfn_WaitOnAddress)
		{	// no need for SRWLOCK or CONDITION_VARIABLE if we have WaitOnAddress
		return (true);
		}

	_Result->_Pfn_AcquireSRWLockExclusive = reinterpret_cast<decltype(AcquireSRWLockExclusive) *>(
		GetProcAddress(_Kernel32, "AcquireSRWLockExclusive"));
	_Result->_Pfn_ReleaseSRWLockExclusive = reinterpret_cast<decltype(ReleaseSRWLockExclusive) *>(
		GetProcAddress(_Kernel32, "ReleaseSRWLockExclusive"));
	_Result->_Pfn_SleepConditionVariableSRW = reinterpret_cast<decltype(SleepConditionVariableSRW) *>(
		GetProcAddress(_Kernel32, "SleepConditionVariableSRW"));
	_Result->_Pfn_WakeAllConditionVariable = reinterpret_cast<decltype(WakeAllConditionVariable) *>(
		GetProcAddress(_Kernel32, "WakeAllConditionVariable"));

	if (!_Result->_Pfn_AcquireSRWLockExclusive
		|| !_Result->_Pfn_ReleaseSRWLockExclusive
		|| !_Result->_Pfn_SleepConditionVariableSRW
		|| !_Result->_Pfn_WakeAllConditionVariable)
		{	// no fallback for WaitOnAddress; shouldn't be possible as these APIs were
			// added at the same time as the Vista threadpool API
		return (false);
		}
 #endif /* _STL_WIN32_WINNT < _WIN32_WINNT_VISTA */

	return (true);
	}
}	// unnamed namespace
#endif /* _STL_WIN32_WINNT < _WIN32_WINNT_WIN8 */

static DWORD _Get_number_of_processors() noexcept
	{
	SYSTEM_INFO _Info;
	GetNativeSystemInfo(&_Info);
	return (_Info.dwNumberOfProcessors);
	}

extern "C" {

unsigned int __stdcall __std_parallel_algorithms_hw_threads() noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8
	return (_Get_number_of_processors());
#else /* ^^^ Win8 ^^^ // vvv pre-Win8 vvv */
	for (;;)
		{
		const auto _Local = __crt_interlocked_read_pointer(&_Parallel_info);
		if (_Local)
			{
			return (_Local->_Hw_threads);
			}

		_Parallel_init_info * const _Result = static_cast<_Parallel_init_info *>(
			VirtualAlloc(0, sizeof(_Parallel_init_info), MEM_COMMIT, PAGE_READWRITE));
		if (!_Result)
			{	// don't parallelize, no RAM
			return (1);
			}

		*_Result = {};
		if (_Initialize_parallel_init_info(_Result))
			{
			_Result->_Hw_threads = _Get_number_of_processors();
			}
		else
			{	// we don't parallelize without more than 1 hardware thread
			_Result->_Hw_threads = 1;
			}

		DWORD _Unused;
		if (VirtualProtect(_Result, sizeof(_Parallel_init_info), PAGE_READONLY, &_Unused) == 0)
			{
			::terminate();
			}

		if (InterlockedCompareExchangePointer(reinterpret_cast<void * volatile *>(&_Parallel_info), _Result, 0))
			{	// another thread got there first
			VirtualFree(_Result, 0, MEM_RELEASE);
			}
		}
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8 */
	}

// Relaxed reads of _Parallel_info below because __std_parallel_algorithms_hw_threads must be called
// before any of these on each thread.

PTP_WORK __stdcall __std_create_threadpool_work(PTP_WORK_CALLBACK _Callback,
	void * _Context, PTP_CALLBACK_ENVIRON _Callback_environ
	) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA
	return (CreateThreadpoolWork(_Callback, _Context, _Callback_environ));
#else /* ^^^ Vista ^^^ // vvv pre-Vista vvv */
	return (_Parallel_info->_Pfn_CreateThreadpoolWork(_Callback, _Context, _Callback_environ));
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA */
	}

void __stdcall __std_submit_threadpool_work(PTP_WORK _Work) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA
	SubmitThreadpoolWork(_Work);
#else /* ^^^ Vista ^^^ // vvv pre-Vista vvv */
	_Parallel_info->_Pfn_SubmitThreadpoolWork(_Work);
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA */
	}

void __stdcall __std_bulk_submit_threadpool_work(PTP_WORK _Work, const size_t _Submissions) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA
	for (size_t _Idx = 0; _Idx < _Submissions; ++_Idx)
		{
		SubmitThreadpoolWork(_Work);
		}
#else /* ^^^ Vista ^^^ // vvv pre-Vista vvv */
	const auto _Fn = _Parallel_info->_Pfn_SubmitThreadpoolWork;
	for (size_t _Idx = 0; _Idx < _Submissions; ++_Idx)
		{
		_Fn(_Work);
		}
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA */
	}

void __stdcall __std_close_threadpool_work(PTP_WORK _Work) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA
	CloseThreadpoolWork(_Work);
#else /* ^^^ Vista ^^^ // vvv pre-Vista vvv */
	_Parallel_info->_Pfn_CloseThreadpoolWork(_Work);
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA */
	}

void __stdcall __std_wait_for_threadpool_work_callbacks(PTP_WORK _Work, BOOL _Cancel) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA
	WaitForThreadpoolWorkCallbacks(_Work, _Cancel);
#else /* ^^^ Vista ^^^ // vvv pre-Vista vvv */
	_Parallel_info->_Pfn_WaitForThreadpoolWorkCallbacks(_Work, _Cancel);
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_VISTA */
	}

void __stdcall __std_execution_wait_on_uchar(const volatile unsigned char * _Address, unsigned char _Compare) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8
	if (WaitOnAddress(const_cast<volatile unsigned char *>(_Address), &_Compare, 1, INFINITE) == FALSE)
		{	// this API failing should only be possible with a timeout, and we asked for INFINITE
		::terminate();
		}
#else /* ^^^ Win8 ^^^ // vvv pre-Win8 vvv */
	if (_Parallel_info->_Pfn_WaitOnAddress)
		{
		if (_Parallel_info->_Pfn_WaitOnAddress(
				const_cast<volatile unsigned char *>(_Address), &_Compare, 1, INFINITE) == FALSE)
			{
			::terminate();
			}

		return;
		}

	// fake WaitOnAddress via SRWLOCK and CONDITION_VARIABLE
	for (int _Idx = 0; _Idx < 4096; ++_Idx)
		{	// optimistic non-backoff spin
		if (_Atomic_load_uchar(_Address) == _Compare)
			{
			return;
			}
		}

	auto& _Wait_entry = _Wait_table[_Choose_wait_entry(_Address)];
 #if _STL_WIN32_WINNT < _WIN32_WINNT_VISTA
	_Parallel_info->_Pfn_AcquireSRWLockExclusive(&_Wait_entry._Mtx);
	while (_Atomic_load_uchar(_Address) == _Compare)
		{
		if (_Parallel_info->_Pfn_SleepConditionVariableSRW(&_Wait_entry._Cv, &_Wait_entry._Mtx, INFINITE, 0) == 0)
			{
			::terminate();
			}
		}

	_Parallel_info->_Pfn_ReleaseSRWLockExclusive(&_Wait_entry._Mtx);
 #else /* ^^^ pre-Vista ^^^ // vvv Vista vvv */
	AcquireSRWLockExclusive(&_Wait_entry._Mtx);
	while (_Atomic_load_uchar(_Address) == _Compare)
		{
		if (SleepConditionVariableSRW(&_Wait_entry._Cv, &_Wait_entry._Mtx, INFINITE, 0) == 0)
			{
			::terminate();
			}
		}

	ReleaseSRWLockExclusive(&_Wait_entry._Mtx);
 #endif /* _STL_WIN32_WINNT < _WIN32_WINNT_VISTA */
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8 */
	}

void __stdcall __std_execution_wake_by_address_all(const volatile void * _Address) noexcept
	{
#if _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8
	WakeByAddressAll(const_cast<void *>(_Address));
#else /* ^^^ Win8 ^^^ // vvv pre-Win8 vvv */
	if (_Parallel_info->_Pfn_WakeByAddressAll)
		{
		_Parallel_info->_Pfn_WakeByAddressAll(const_cast<void *>(_Address));
		}
	else
		{
		auto& _Wait_entry = _Wait_table[_Choose_wait_entry(_Address)];
 #if _STL_WIN32_WINNT < _WIN32_WINNT_VISTA
		_Parallel_info->_Pfn_AcquireSRWLockExclusive(&_Wait_entry._Mtx);
		_Parallel_info->_Pfn_ReleaseSRWLockExclusive(&_Wait_entry._Mtx);
		_Parallel_info->_Pfn_WakeAllConditionVariable(&_Wait_entry._Cv);
 #else /* ^^^ pre-Vista ^^^ // vvv Vista vvv */
		AcquireSRWLockExclusive(&_Wait_entry._Mtx);
		ReleaseSRWLockExclusive(&_Wait_entry._Mtx);
		WakeAllConditionVariable(&_Wait_entry._Cv);
 #endif /* _STL_WIN32_WINNT < _WIN32_WINNT_VISTA */
		}
#endif /* _STL_WIN32_WINNT >= _WIN32_WINNT_WIN8 */
	}

} // extern "C"
