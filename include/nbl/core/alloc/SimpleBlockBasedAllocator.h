// Copyright (C) 2018-2020 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#ifndef __NBL_CORE_SIMPLE_BLOCK_BASED_ALLOCATOR_H_INCLUDED__
#define __NBL_CORE_SIMPLE_BLOCK_BASED_ALLOCATOR_H_INCLUDED__

#include "nbl/core/Types.h"
#include "nbl/core/alloc/aligned_allocator.h"
#include "nbl/core/alloc/address_allocator_traits.h"
#include "nbl/core/alloc/AddressAllocatorConcurrencyAdaptors.h"

#include <memory>

namespace nbl
{
namespace core
{

//! Doesn't resize memory arenas, therefore once allocated pointers shall not move
template<class AddressAllocator, template<class> class DataAllocator, typename... Args>
class SimpleBlockBasedAllocator
{
	public:
		using size_type = typename address_allocator_traits<AddressAllocator>::size_type;
		_NBL_STATIC_INLINE_CONSTEXPR size_type meta_alignment = 64u;

	private:
		class Block
		{
				AddressAllocator addrAlloc;

			public:
				Block(size_type blockSize, const Args&... args) :
					addrAlloc(AddressAllocator(data()+blockSize, 0u, 0u, meta_alignment, blockSize, args...))
				{
					assert(address_allocator_traits<AddressAllocator>::get_align_offset(addrAlloc) == 0ul);
					assert(address_allocator_traits<AddressAllocator>::get_combined_offset(addrAlloc) == 0u);
				}

				static size_type size_of(size_type blockSize, const Args&... args)
				{
					return core::alignUp(sizeof(AddressAllocator),meta_alignment)+blockSize+address_allocator_traits<AddressAllocator>::reserved_size(meta_alignment,blockSize,args...);
				}

				uint8_t* data() { return reinterpret_cast<uint8_t*>(this)+core::alignUp(sizeof(AddressAllocator),meta_alignment); }
				const uint8_t* data() const
				{
					return const_cast<Block*>(this)->data();
				}
				
				const AddressAllocator& getAllocator() const { return addrAlloc; }

				size_type alloc(size_type bytes, size_type alignment)
				{
					size_type addr = AddressAllocator::invalid_address;
					address_allocator_traits<AddressAllocator>::multi_alloc_addr(addrAlloc, 1u, &addr, &bytes, &alignment);
					return addr;
				}

				void free(size_type addr, size_type bytes)
				{
					address_allocator_traits<AddressAllocator>::multi_free_addr(addrAlloc, 1u, &addr, &bytes);
				}
		};

    public:
        virtual ~SimpleBlockBasedAllocator()
		{
			reset();
			metaAlloc.deallocate(blocks,maxBlockCount);
		}

		SimpleBlockBasedAllocator(size_type _blockSize, size_type _maxBlockCount, Args&&... args) :
			blockSize(_blockSize), effectiveBlockSize(Block::size_of(blockSize,args...)), maxBlockCount(_maxBlockCount),
			metaAlloc(), blocks(metaAlloc.allocate(maxBlockCount, meta_alignment)),
			blockAlloc(), blockCreationArgs(args...)
		{
			assert(maxBlockCount > 0u);
			std::fill(blocks,blocks+maxBlockCount,nullptr);
		}

		SimpleBlockBasedAllocator& operator=(SimpleBlockBasedAllocator&& other)
        {
			std::swap(blockSize, other.blockSize);
			std::swap(effectiveBlockSize, other.effectiveBlockSize);
			std::swap(maxBlockCount, other.maxBlockCount);
			std::swap(metaAlloc, other.metaAlloc);
			std::swap(blocks, other.blocks);
            std::swap(blockAlloc,other.blockAlloc);
            return *this;
        }

        inline void		reset()
        {
			for (auto i=0u; i<maxBlockCount; i++)
				deleteBlock(i);
        }



		inline void*	allocate(size_type bytes, size_type alignment) noexcept
		{
			constexpr auto invalid_address = AddressAllocator::invalid_address;
			for (size_type i=0u; i<maxBlockCount; i++)
			{
				auto& block = blocks[i];

				bool die = i==(maxBlockCount-1u);
				if (!block)
				{
					block = createBlock();
					die = true;
				}

				size_type addr = block->alloc(bytes, alignment);
				if (addr == invalid_address)
				{
					if (die)
						break;
					else
						continue;
				}

				return block->data()+addr;
			}
			return nullptr;
		}
		inline void		deallocate(void* p, size_type bytes) noexcept
		{
			for (size_type i=0u; i<maxBlockCount; i++)
			{
				auto& block = blocks[i];
				if (!block)
					continue;
                    
				size_type addr = p-block->data();
				if (addr<blockSize)
				{
					block->free(addr,bytes);
					if (address_allocator_traits<AddressAllocator>::get_allocated_size(block->getAllocator())==size_type(0u))
						deleteBlock(i);
					return;
				}
			}
			assert(false);
		}

		inline bool		operator!=(const SimpleBlockBasedAllocator<AddressAllocator,DataAllocator>& other) const noexcept
		{
			if (blockSize != other.blockSize)
				return true;
			if (effectiveBlockSize != other.effectiveBlockSize)
				return true;
			if (maxBlockCount != other.maxBlockCount)
				return true;
			if (metaAlloc != other.metaAlloc)
				return true;
			if (blocks != other.blocks)
				return true;
			if (blockAlloc != other.blockAlloc)
				return true;
			return false;
		}
		inline bool		operator==(const SimpleBlockBasedAllocator<AddressAllocator,DataAllocator>& other) const noexcept
		{
			return !operator!=(other);
		}
    protected:
		size_type blockSize;
		size_type effectiveBlockSize;
		size_type maxBlockCount;
		DataAllocator<Block*> metaAlloc;
		Block** blocks;
		DataAllocator<uint8_t> blockAlloc;

		std::tuple<Args...> blockCreationArgs;


		template<int ...> struct seq {};
		template<int N, int ...S> struct gens : gens<N - 1, N - 1, S...> { };
		template<int ...S> struct gens<0, S...> { typedef seq<S...> type; };

		template<int ...S>
		void constructBlock(Block* mem,seq<S...>)
		{
			new(mem) Block(blockSize,std::get<S>(blockCreationArgs)...);
		}
		Block* createBlock()
		{
			auto retval = reinterpret_cast<Block*>(blockAlloc.allocate(effectiveBlockSize, meta_alignment));
			constructBlock(retval,typename gens<sizeof...(Args)>::type());
			return retval;
		}


		void deleteBlock(uint32_t index)
		{
			if (!blocks[index])
				return;

			blocks[index]->~Block();
			blockAlloc.deallocate(reinterpret_cast<uint8_t*>(blocks[index]),effectiveBlockSize);
			blocks[index] = nullptr;
		}
};


// no aliases

}
}

#endif


