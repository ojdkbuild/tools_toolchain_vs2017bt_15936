// Copyright (c) Microsoft Corporation. All rights reserved.
#include <memory_resource>
#include <system_error>
#include <internal_shared.h>

static_assert(_HAS_ALIGNED_NEW,
	"This file must be built with aligned new support enabled.");

_STD_BEGIN
namespace pmr {

static memory_resource * _Default_resource = nullptr;

extern "C" _CRTIMP3 _Aligned_new_delete_resource_impl * __cdecl _Aligned_new_delete_resource() noexcept
	{
	return (&_Immortalize<_Aligned_new_delete_resource_impl>());
	}

extern "C" _CRTIMP3 _Unaligned_new_delete_resource_impl * __cdecl _Unaligned_new_delete_resource() noexcept
	{
	return (&_Immortalize<_Unaligned_new_delete_resource_impl>());
	}

extern "C" _CRTIMP3 memory_resource * __cdecl _Aligned_get_default_resource() noexcept
	{
	memory_resource * const _Temp = __crt_interlocked_read_pointer(&_Default_resource);
	if (_Temp)
		{
		return (_Temp);
		}

	return (_Aligned_new_delete_resource());
	}

extern "C" _CRTIMP3 memory_resource * __cdecl _Unaligned_get_default_resource() noexcept
	{
	memory_resource * const _Temp = __crt_interlocked_read_pointer(&_Default_resource);
	if (_Temp)
		{
		return (_Temp);
		}

	return (_Unaligned_new_delete_resource());
	}

extern "C" _CRTIMP3 memory_resource * __cdecl _Aligned_set_default_resource(memory_resource * const _Resource) noexcept
	{
	memory_resource * const _Temp = __crt_interlocked_exchange_pointer(&_Default_resource, _Resource);
	if (_Temp)
		{
		return (_Temp);
		}

	return (_Aligned_new_delete_resource());
	}

extern "C" _CRTIMP3 memory_resource * __cdecl _Unaligned_set_default_resource(memory_resource * const _Resource) noexcept
	{
	memory_resource * const _Temp = __crt_interlocked_exchange_pointer(&_Default_resource, _Resource);
	if (_Temp)
		{
		return (_Temp);
		}

	return (_Unaligned_new_delete_resource());
	}

		// FUNCTION null_memory_resource
extern "C" _CRTIMP3 _NODISCARD memory_resource * __cdecl null_memory_resource() noexcept
	{
	class _Null_resource final
		: public _Identity_equal_resource
		{
		virtual void * do_allocate(size_t, size_t) override
			{	// Sorry, OOM!
			_Xbad_alloc();
			}
		virtual void do_deallocate(void *, size_t, size_t) override
			{	// Nothing to do
			}
		};

	return (&_Immortalize<_Null_resource>());
	}

} // namespace pmr
_STD_END
