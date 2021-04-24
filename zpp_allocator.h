#ifndef ZPP_ALLOCATOR_H
#define ZPP_ALLOCATOR_H
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace zpp
{
template <typename Type>
class allocator;

template <>
class allocator<std::byte>
{
public:
    template <typename Type>
    struct span
    {
        auto data() const noexcept
        {
            return m_data;
        }

        auto size() const noexcept
        {
            return m_size;
        }

        auto & front() const noexcept
        {
            return *m_data;
        }

        auto & back() const noexcept
        {
            return *(m_data + m_size);
        }

        auto begin() const noexcept
        {
            return m_data;
        }

        auto end() const noexcept
        {
            return m_data + m_size;
        }

        Type * m_data{};
        std::size_t m_size{};
    };

    struct list
    {
        struct node
        {
            constexpr node() = default;
            constexpr explicit node(std::size_t size) : m_size(size)
            {
            }

            node(node &&) = delete;
            node(const node &) = delete;
            node & operator=(node &&) = delete;
            node & operator=(const node &) = delete;
            ~node() = default;

            std::byte * address() noexcept
            {
                return reinterpret_cast<std::byte *>(this);
            }

            const std::byte * address() const noexcept
            {
                return reinterpret_cast<const std::byte *>(this);
            }

            std::size_t size() const noexcept
            {
                return m_size & ~std::size_t(0x1);
            }

            node * next() const noexcept
            {
                return m_next;
            }

            node * next_free() const noexcept
            {
                return m_next_free;
            }

            node * prev() const noexcept
            {
                return m_prev;
            }

            node * prev_free() const noexcept
            {
                return m_prev_free;
            }

            void append_to_list(node * p) noexcept
            {
                // Insert the node.
                if (m_next) {
                    m_next->m_prev = p;
                }
                p->m_next = m_next;
                p->m_prev = this;
                m_next = p;
            }

            void prepend_to_list(node * p) noexcept
            {
                if (m_prev) {
                    m_prev->m_next = p;
                }
                p->m_prev = m_prev;
                p->m_next = this;
                m_prev = p;
            }

            void append_to_freelist(node * p) noexcept
            {
                if (m_next_free) {
                    m_next_free->m_prev_free = p;
                }
                p->m_next_free = m_next_free;
                p->m_prev_free = this;
                m_next_free = p;
                p->set_free();
                p->merge();
            }

            void prepend_to_freelist(node * p) noexcept
            {
                if (m_prev_free) {
                    m_prev_free->m_next_free = p;
                }
                p->m_prev_free = m_prev_free;
                p->m_next_free = this;
                m_prev_free = p;
                p->set_free();
                p->merge();
            }

            node * unlink_from_list() noexcept
            {
                if (m_prev) {
                    m_prev->m_next = m_next;
                }

                if (m_next) {
                    m_next->m_prev = m_prev;
                }

                return this;
            }

            node * unlink_from_freelist() noexcept
            {
                if (m_prev_free) {
                    m_prev_free->m_next_free = m_next_free;
                }

                if (m_next_free) {
                    m_next_free->m_prev_free = m_prev_free;
                }

                set_allocated();
                return this;
            }

            node * split(std::size_t size) noexcept
            {
                // Create new node.
                node * tail = ::new (address() + size) node(m_size - size);
                append_to_list(tail);
                append_to_freelist(tail);

                // Shrink current node.
                m_size = size;

                return tail;
            }

            void merge_next() noexcept
            {
                // Increase current size.
                m_size += m_next_free->m_size;

                // Remove next from free list.
                m_next_free->unlink_from_freelist();

                // Remove node from complete list.
                m_next->unlink_from_list();
            }

            void merge() noexcept
            {
                if (m_next_free &&
                    address() + m_size == m_next_free->address()) {
                    merge_next();
                }

                if (m_prev_free &&
                    m_prev_free->address() + m_prev_free->m_size ==
                        address()) {
                    m_prev_free->merge_next();
                }
            }

            std::byte * data() noexcept
            {
                return std::launder(reinterpret_cast<std::byte *>(
                    std::addressof(m_next_free)));
            }

            static node * from_data(std::byte * data) noexcept
            {
                return std::launder(reinterpret_cast<node *>(
                    data - offsetof(node, m_next_free)));
            }

            static const node * from_data(const std::byte * data) noexcept
            {
                return std::launder(reinterpret_cast<const node *>(
                    data - offsetof(node, m_next_free)));
            }

            bool is_free() const noexcept
            {
                return !(m_size & 0x1);
            }

            void set_free() noexcept
            {
                m_size &= ~std::size_t(0x1);
            }

            void set_allocated() noexcept
            {
                m_size |= 0x1;
            }

            constexpr static std::size_t
            alignment(std::size_t size) noexcept
            {
                return ((size + alignof(node) - 1) / alignof(node) *
                        alignof(node)) -
                       size;
            }

            alignas(2 > alignof(node *) ? 2
                                        : alignof(node *)) node * m_next{};
            node * m_prev{};
            std::size_t m_size{};
            alignas(std::max_align_t) node * m_next_free{};
            node * m_prev_free{};
        };

        explicit list(const span<std::byte> & memory) noexcept :
            m_first_free(::new (memory.data()) node(memory.size())),
            m_first(m_first_free)
        {
        }

        list(list && other) noexcept :
            m_first_free(other.m_first_free),
            m_allocated(other.m_allocated)
        {
            other.m_first_free = nullptr;
            other.m_allocated = {};
        }

        list & operator=(list && other) noexcept
        {
            m_first_free = other.m_first_free;
            m_allocated = other.m_allocated;
            other.m_first_free = nullptr;
            other.m_allocated = {};
            return *this;
        }

        list(const list &) = delete;
        list & operator=(const list &) = delete;

        node * allocate(std::size_t size) const noexcept
        {
            // Node sizes include themselves, we can override
            // m_next_free and m_prev_free in the allocation.
            size += offsetof(node, m_next_free);

            // Make sure we only allocate properly aligned data.
            size += node::alignment(size);

            for (auto p = m_first_free; p; p = p->next_free()) {
                // If not enough size, continue.
                if (p->size() < size) {
                    continue;
                }

                // If there is leftover space for a node,
                // split the current node.
                if (p->size() - size >= sizeof(node)) {
                    p->split(size);
                }

                // Unlink the node from the freelist.
                p->unlink_from_freelist();

                // If we just allocated the first node,
                // update the first free node.
                if (p == m_first_free) {
                    m_first_free = p->next_free();
                }

                // Count allocation.
                m_allocated += p->size();
                return p;
            }

            return nullptr;
        }

        void deallocate(node * node, std::size_t) const noexcept
        {
            // Bring back the free list pointers.
            ::new (&node->m_next_free) struct node *;
            ::new (&node->m_prev_free) struct node *;

            // Count deallocation.
            m_allocated -= node->size();

            // Find first free node.
            for (auto p = node->prev(); p; p = p->prev()) {
                if (!p->is_free()) {
                    continue;
                }
                p->append_to_freelist(node);
                return;
            }

            // If this is not the last node, prepend to first.
            if (m_first_free) {
                m_first_free->prepend_to_freelist(node);
            } else {
                node->m_next_free = {};
                node->m_prev_free = {};
            }
            m_first_free = node;
        }

        std::size_t allocation_size(const node * node) const noexcept
        {
            return (node->m_size & ~std::size_t(0x1)) -
                   offsetof(struct node, m_next_free);
        }

        mutable node * m_first_free{};
        mutable std::size_t m_allocated{};
        node * m_first{};
    };

    using value_type = std::byte;

    explicit allocator(std::byte * memory, std::size_t size) noexcept :
        m_memory{memory + list::node::alignment(
                              reinterpret_cast<std::uintptr_t>(memory)),
                 size - list::node::alignment(
                            reinterpret_cast<std::uintptr_t>(memory))},
        m_list(m_memory)
    {
    }

    std::byte * allocate(std::size_t size) const noexcept
    {
        auto node = m_list.allocate(size);
        if (!node) {
            return nullptr;
        }
        return node->data();
    }

    void deallocate(std::byte * pointer, std::size_t size) const noexcept
    {
        if (!pointer) {
            return;
        }
        m_list.deallocate(list::node::from_data(pointer), size);
    }

    std::size_t allocation_size(const void * pointer) const noexcept
    {
        return m_list.allocation_size(list::node::from_data(
            static_cast<const std::byte *>(pointer)));
    }

    bool contains(const void * address) const noexcept
    {
        return &m_memory.front() <= address && address < &m_memory.back();
    }

    std::size_t allocated() const noexcept
    {
        return m_list.m_allocated;
    }

    std::size_t size() const noexcept
    {
        return m_memory.size();
    }

private:
    span<std::byte> m_memory;
    list m_list;
};

template <typename Type>
class allocator
{
public:
    using value_type = Type;

    constexpr explicit allocator(std::byte * data, std::size_t size) :
        m_allocator(data, size)
    {
    }

    Type * allocate(std::size_t size) const noexcept
    {
        return reinterpret_cast<Type *>(
            m_allocator.allocate(sizeof(Type) * size));
    }

    void deallocate(Type * pointer, std::size_t size) const noexcept
    {
        return m_allocator.deallocate(
            reinterpret_cast<std::byte *>(pointer), sizeof(Type) * size);
    }

    std::size_t allocation_size(const void * pointer) const noexcept
    {
        return m_allocator.allocation_size(pointer);
    }

    bool contains(const void * pointer) const noexcept
    {
        return m_allocator.contains(pointer);
    }

private:
    allocator<std::byte> m_allocator;
};

template <std::size_t Index = 0>
class heap
{
public:
    using allocator_type = allocator<std::byte>;

    static void create(std::byte * memory, std::size_t size) noexcept
    {
        ::new (std::addressof(m_allocator)) allocator_type(memory, size);
    }

    static const allocator_type & get_allocator() noexcept
    {
        return *std::launder(reinterpret_cast<allocator_type *>(
            std::addressof(m_allocator)));
    }

private:
    static inline std::aligned_storage_t<sizeof(allocator_type),
                                         alignof(allocator_type)>
        m_allocator;
};

template <typename Type, typename Source = heap<>>
class static_allocator
{
public:
    using value_type = Type;

    Type * allocate(std::size_t size) noexcept
    {
        return reinterpret_cast<Type *>(
            Source::get_allocator().allocate(sizeof(Type) * size));
    }

    void deallocate(Type * pointer, std::size_t size) noexcept
    {
        return Source::get_allocator().deallocate(
            reinterpret_cast<std::byte *>(pointer), sizeof(Type) * size);
    }
};

} // namespace zpp

#endif
