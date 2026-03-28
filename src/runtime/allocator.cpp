#include "marrow/allocator.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>

namespace marrow {
namespace {

struct AllocationHeader {
    void* base{nullptr};
};

std::size_t normalize_alignment(std::size_t alignment) noexcept {
    if (alignment == 0U) {
        alignment = alignof(std::max_align_t);
    }

    alignment = std::max(alignment, alignof(AllocationHeader));
    if ((alignment & (alignment - 1U)) == 0U) {
        return alignment;
    }

    std::size_t rounded = 1U;
    while (rounded < alignment && rounded <= (std::numeric_limits<std::size_t>::max() / 2U)) {
        rounded <<= 1U;
    }
    return std::max(rounded, alignof(AllocationHeader));
}

class DefaultAllocator final : public Allocator {
public:
    void* allocate(std::size_t size, std::size_t alignment) override {
        if (size == 0U) {
            size = 1U;
        }

        const std::size_t normalized_alignment = normalize_alignment(alignment);
        if (size > std::numeric_limits<std::size_t>::max() -
                (normalized_alignment - 1U) - sizeof(AllocationHeader)) {
            return nullptr;
        }

        const std::size_t total_size =
            size + (normalized_alignment - 1U) + sizeof(AllocationHeader);
        void* base = std::malloc(total_size);
        if (base == nullptr) {
            return nullptr;
        }

        void* aligned = static_cast<char*>(base) + sizeof(AllocationHeader);
        std::size_t space = total_size - sizeof(AllocationHeader);
        aligned = std::align(normalized_alignment, size, aligned, space);
        if (aligned == nullptr) {
            std::free(base);
            return nullptr;
        }

        auto* header = reinterpret_cast<AllocationHeader*>(
            static_cast<char*>(aligned) - sizeof(AllocationHeader));
        header->base = base;
        return aligned;
    }

    void deallocate(void* ptr, std::size_t) noexcept override {
        if (ptr == nullptr) {
            return;
        }

        const auto* header = reinterpret_cast<const AllocationHeader*>(
            static_cast<const char*>(ptr) - sizeof(AllocationHeader));
        std::free(header->base);
    }
};

Allocator*& allocator_slot() noexcept {
    static Allocator* allocator = nullptr;
    return allocator;
}

std::atomic<std::size_t>& live_allocation_count() noexcept {
    static std::atomic<std::size_t> count{0U};
    return count;
}

} // namespace

Allocator& default_allocator() noexcept {
    static DefaultAllocator allocator;
    return allocator;
}

Allocator& get_allocator() noexcept {
    Allocator*& allocator = allocator_slot();
    if (allocator == nullptr) {
        allocator = &default_allocator();
    }

    return *allocator;
}

bool set_allocator(Allocator* allocator) noexcept {
    if (live_allocation_count().load(std::memory_order_acquire) != 0U) {
        return false;
    }

    allocator_slot() = allocator != nullptr ? allocator : &default_allocator();
    return true;
}

void* allocate_bytes(std::size_t size, std::size_t alignment) {
    void* ptr = get_allocator().allocate(size == 0U ? 1U : size, alignment);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }

    live_allocation_count().fetch_add(1U, std::memory_order_acq_rel);
    return ptr;
}

void deallocate_bytes(void* ptr, std::size_t size) noexcept {
    if (ptr == nullptr) {
        return;
    }

    get_allocator().deallocate(ptr, size == 0U ? 1U : size);
    live_allocation_count().fetch_sub(1U, std::memory_order_acq_rel);
}

} // namespace marrow
