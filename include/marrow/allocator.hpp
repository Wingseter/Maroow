#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace marrow {

class Allocator {
public:
    /// @brief Virtual destructor for allocator implementations.
    virtual ~Allocator() = default;

    /**
     * @brief Allocates raw storage with the requested alignment.
     * @param size Number of bytes to allocate.
     * @param alignment Required memory alignment.
     * @return Pointer to the allocated storage.
     */
    virtual void* allocate(std::size_t size, std::size_t alignment) = 0;
    /**
     * @brief Releases storage previously allocated by this allocator.
     * @param ptr Pointer returned by `allocate()`.
     * @param size Size originally associated with the allocation.
     */
    virtual void deallocate(void* ptr, std::size_t size) noexcept = 0;
};

/// @brief Returns the built-in allocator implementation.
/// @return The process default allocator.
Allocator& default_allocator() noexcept;
/// @brief Returns the allocator currently configured for Marrow allocations.
/// @return The active process-wide allocator.
Allocator& get_allocator() noexcept;
// Process-wide allocator selection. Configure it during startup before
// creating Marrow objects on worker threads. Changing the allocator while live
// Marrow allocations exist fails.
/**
 * @brief Sets the process-wide allocator used by Marrow containers and objects.
 * @param allocator Allocator to install, or `nullptr` to restore the default allocator.
 * @return `true` when the allocator changed successfully; otherwise `false`.
 */
bool set_allocator(Allocator* allocator) noexcept;

/**
 * @brief Allocates raw bytes from the active allocator.
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment for the allocation.
 * @return Pointer to the allocated storage.
 */
void* allocate_bytes(
    std::size_t size,
    std::size_t alignment = alignof(std::max_align_t));
/**
 * @brief Releases raw bytes previously allocated by `allocate_bytes()`.
 * @param ptr Pointer returned by `allocate_bytes()`.
 * @param size Size originally associated with the allocation.
 */
void deallocate_bytes(void* ptr, std::size_t size) noexcept;

template <typename T>
class StlAllocator {
public:
    using value_type = T;

    /// @brief Creates an STL-compatible allocator wrapper over the active Marrow allocator.
    StlAllocator() noexcept = default;

    template <typename U>
    /**
     * @brief Rebind constructor used by STL containers.
     * @param other Source allocator instance for another value type.
     */
    StlAllocator(const StlAllocator<U>& other) noexcept {
        static_cast<void>(other);
    }

    /**
     * @brief Allocates storage for `count` values of `T`.
     * @param count Number of values to allocate.
     * @return Pointer to storage for `count` values.
     */
    T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        return static_cast<T*>(allocate_bytes(count * sizeof(T), alignof(T)));
    }

    /**
     * @brief Releases storage for `count` values of `T`.
     * @param ptr Pointer returned by `allocate()`.
     * @param count Number of values previously allocated.
     */
    void deallocate(T* ptr, std::size_t count) noexcept {
        deallocate_bytes(ptr, count * sizeof(T));
    }

    template <typename U>
    /**
     * @brief Reports allocator equality for STL container interoperability.
     * @param other Allocator instance to compare against.
     * @return Always `true` because all `StlAllocator` instances share the same backend.
     */
    bool operator==(const StlAllocator<U>& other) const noexcept {
        static_cast<void>(other);
        return true;
    }

    template <typename U>
    /**
     * @brief Reports allocator inequality for STL container interoperability.
     * @param other Allocator instance to compare against.
     * @return Always `false` because all `StlAllocator` instances share the same backend.
     */
    bool operator!=(const StlAllocator<U>& other) const noexcept {
        static_cast<void>(other);
        return false;
    }

    template <typename U>
    struct rebind {
        using other = StlAllocator<U>;
    };
};

template <typename T>
/**
 * @brief Destroys an object allocated with Marrow allocation helpers.
 * @param ptr Object pointer to destroy. `nullptr` is allowed.
 */
void destroy_object(T* ptr) noexcept;

template <typename T>
struct AllocatedObjectDeleter {
    /**
     * @brief Deletes an object allocated with Marrow allocation helpers.
     * @param ptr Object pointer to destroy. `nullptr` is allowed.
     */
    void operator()(T* ptr) const noexcept {
        destroy_object(ptr);
    }
};

template <typename T>
using UniquePtr = std::unique_ptr<T, AllocatedObjectDeleter<T>>;

template <typename T, typename... Args>
/**
 * @brief Allocates and constructs an object with the active allocator.
 * @param args Constructor arguments forwarded to `T`.
 * @return Pointer to the constructed object.
 */
T* allocate_object(Args&&... args) {
    void* storage = allocate_bytes(sizeof(T), alignof(T));
    try {
        return new (storage) T(std::forward<Args>(args)...);
    } catch (...) {
        deallocate_bytes(storage, sizeof(T));
        throw;
    }
}

template <typename T>
/**
 * @brief Destroys an object and returns its storage to the active allocator.
 * @param ptr Object pointer to destroy. `nullptr` is allowed.
 */
void destroy_object(T* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }

    ptr->~T();
    deallocate_bytes(ptr, sizeof(T));
}

template <typename T, typename... Args>
/**
 * @brief Allocates and constructs a unique-ownership object.
 * @param args Constructor arguments forwarded to `T`.
 * @return Unique pointer owning the constructed object.
 */
UniquePtr<T> allocate_unique(Args&&... args) {
    return UniquePtr<T>(allocate_object<T>(std::forward<Args>(args)...));
}

template <typename T, typename... Args>
/**
 * @brief Allocates and constructs a shared-ownership object.
 * @param args Constructor arguments forwarded to `T`.
 * @return Shared pointer owning the constructed object.
 */
std::shared_ptr<T> allocate_shared(Args&&... args) {
    return std::allocate_shared<T>(StlAllocator<T>{}, std::forward<Args>(args)...);
}

} // namespace marrow
