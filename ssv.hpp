#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <string_view>

// ssv: short string vector
// an append-only vector of immutable strings:
// - 128 bytes long
// - can store up to 120 bytes in place
// - can store up to 9 strings in place
// - then spills to a contiguous buffer on the heap

class ssv {
    using u8 = uint8_t;
    using u64 = uint64_t;

    struct heapvec {
        u64 capacity;
        u64 nstrings;
        // this is an overallocated structure
        //
        // (implicit) char data[];
        // (implicit) u64 offsets[]
        //
        // offsets[] is indexed backwards, and heapvec is resized when the two meet in the middle
        // (heapvec is allocated in powers of 2 so this stays aligned)
        //
        // since the 0th string is at offset 0, offsets[i] points at the start of the (i+1)th string
        // the length of the strings is stored implicitly

        inline auto data() { return reinterpret_cast<char *>(this) + sizeof(*this); }
        inline auto data() const { return reinterpret_cast<const char *>(this) + sizeof(*this); }

        inline u64 &offset(size_t idx) {
            auto endalloc = reinterpret_cast<char *>(this);
            auto end = reinterpret_cast<u64 *>(endalloc + capacity);
            return end[-idx - 1];
        }
        inline const u64 &offset(size_t idx) const {
            auto endalloc = reinterpret_cast<const char *>(this);
            auto end = reinterpret_cast<const u64 *>(endalloc + capacity);
            return end[-idx - 1];
        }

        std::string_view operator[](size_t idx) const {
            if (idx == 0)
                return {data(), offset(0) - 1};
            return {&data()[offset(idx - 1)], offset(idx) - offset(idx - 1) - 1};
        }

        inline auto size() const { return nstrings == 0 ? 0 : offset(nstrings - 1); }
        inline auto usable() const {
            return capacity - sizeof *this - size() - nstrings * sizeof(u64);
        }

        // caller needs to know that there's enough space!!!  it can't realloc itself!!!
        inline void append(std::string_view s) {
            auto off = size();
            auto buf = data() + off;

            memcpy(buf, s.data(), s.size());
            buf[s.size()] = 0;
            offset(nstrings++) = off + s.size() + 1;
        }
    };

    u64 inplace : 1, offsets : 63;

    union {
        std::array<char, 120> data{};
        struct {
            std::array<char, 120 - 8> datasmol;
            heapvec *heap;
        };
    };

    struct desc {
        u8 nfields;
        u8 size;
        std::array<u8, 9> offarray;
    };

    auto inplace_desc() const {
        u64 offs = offsets;
        u8 size{}, i{};
        std::array<u8, 9> offarray{};

        for (; (offs & 0x7f) != 0x7f && i < 9; offs >>= 7, i++) {
            offarray[i] = offs & 0x7f;
            size += strlen(&data[offs & 0x7f]) + 1;
            // todo: avoid strlen if the next offset isn't 0x7f
        }

        return desc{i, size, offarray};
    }

  public:
    ssv()
        : inplace(1),
          offsets(0x7f) {}

    ssv(const ssv &other)
        : inplace(other.inplace),
          offsets(other.offsets) {
        if (other.inplace)
            data = other.data;
        else {
            datasmol = other.datasmol;
            void *alloc = malloc(other.heap->capacity);
            if (alloc == nullptr)
                throw std::bad_alloc();
            heap = reinterpret_cast<decltype(heap)>(alloc);
            memcpy(heap, other.heap, other.heap->capacity);
        }
    }

    ssv(ssv &&other) noexcept
        : inplace(other.inplace),
          offsets(other.offsets) {
        if (other.inplace)
            data = other.data;
        else {
            datasmol = other.datasmol;
            heap = other.heap;
            other.inplace = 1;
        }
    }
    ssv &operator=(const ssv &other) {
        if (!inplace)
            free(heap);

        inplace = other.inplace;
        offsets = other.offsets;

        if (other.inplace)
            data = other.data;
        else {
            datasmol = other.datasmol;
            void *alloc = malloc(other.heap->capacity);
            if (alloc == nullptr)
                throw std::bad_alloc();
            heap = reinterpret_cast<heapvec *>(alloc);
            memcpy(heap, other.heap, other.heap->capacity);
        }
        return *this;
    }
    ssv &operator=(ssv &&other) {
        if (!inplace)
            free(heap);

        inplace = other.inplace;
        offsets = other.offsets;

        if (other.inplace)
            data = other.data;
        else {
            datasmol = other.datasmol;
            heap = other.heap;
            other.inplace = 1;
        }
        return *this;
    }

    ~ssv() {
        if (!inplace)
            free(heap);
    }

    u64 size() const {
        auto desc = inplace_desc();
        return inplace ? desc.size : desc.size + heap->size();
    }
    u64 nstrings() const {
        auto desc = inplace_desc();
        return inplace ? desc.nfields : desc.nfields + heap->nstrings;
    }

    void append(std::string_view s) {
        if (inplace) {
            auto desc = inplace_desc();
            if (desc.nfields < 9 && desc.size + s.size() + 1 <= sizeof(data)) {
                offsets &= ~(0x7full << (desc.nfields * 7));
                offsets |= u64{desc.size} << (desc.nfields * 7);
                offsets |= 0x7full << ((desc.nfields + 1) * 7);

                char *dest = &data[desc.size];
                memcpy(dest, s.data(), s.size());
                dest[s.size()] = 0;
            }
            else {
                size_t capacity = s.size() + 1;

                // copy all the strings overlapping with the pointer to the heap
                int mustmove = 9;
                for (int i = 0; i < desc.nfields; i++) {
                    auto len = strlen(&data[desc.offarray[i]]) + 1;
                    if (desc.offarray[i] + len > sizeof datasmol) {
                        if (mustmove == 9)
                            mustmove = i;
                        capacity += len;
                    }
                }
                if (capacity < 128)
                    capacity = 128;

                capacity = std::bit_ceil(capacity);

                void *alloc = calloc(1, capacity);
                if (alloc == nullptr)
                    throw std::bad_alloc();

                heap = reinterpret_cast<decltype(heap)>(alloc);
                heap->capacity = capacity;

                inplace = 0;
                if (mustmove != 9) {
                    // mark them as not in place anymore
                    offsets |= 0x7full << (mustmove * 7);

                    for (; mustmove < desc.nfields; mustmove++)
                        heap->append(&data[desc.offarray[mustmove]]);
                }

                heap->append(s);
            }
        }
        else {
            // todo: make api less awkward
            if (s.size() + 1 + sizeof(u64) > heap->usable()) {
                // realloc doesn't help because we need to move the offsets
                void *alloc = calloc(1, heap->capacity * 2);
                if (alloc == nullptr)
                    throw std::bad_alloc();

                auto heapalloc = reinterpret_cast<decltype(heap)>(alloc);
                heapalloc->capacity = heap->capacity * 2;

                auto nstrings = heapalloc->nstrings = heap->nstrings;
                for (size_t i = 0; i < nstrings; i++)
                    heapalloc->offset(i) = heap->offset(i);

                memcpy(heapalloc->data(), heap->data(), heap->size());
                free(heap);
                heap = reinterpret_cast<decltype(heap)>(heapalloc);
            }
            heap->append(s);
        }
    }

    std::string_view operator[](size_t idx) const {
        auto desc = inplace_desc();
        if (idx < desc.nfields)
            return {&data[desc.offarray[idx]]};
        return (*heap)[idx - desc.nfields];
    }

    struct iterator {
        const ssv *ptr;
        size_t pos = -1;
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::string_view;
        using reference = std::string_view &;
        iterator(const ssv *obj, size_t pos)
            : ptr(obj),
              pos(pos) {}
        iterator &operator++() {
            pos++;
            return *this;
        }
        auto operator*() const { return (*ptr)[pos]; }
        bool operator==(const iterator &other) const = default;
        bool operator!=(const iterator &other) const = default;
    };
    auto begin() const { return iterator(this, 0); }
    auto end() const { return iterator(this, nstrings()); }

    friend std::ostream &operator<<(std::ostream &stream, const ssv &s);
};
