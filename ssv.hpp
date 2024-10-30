#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <string_view>

// ssv: small string vector, a space-efficient append-only vector of immutable strings
// - 128 bytes long by default
// - can store up to 120 bytes in place
// - can store up to 9 strings in place
// - then spills to a contiguous buffer on the heap

// always stores at least Bufsize bytes in place
template <unsigned Bufsize = 120, typename Index = uint64_t>
struct ssv {
    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;

    // mask is used as a sentinel
    static_assert(Bufsize != std::bit_ceil(Bufsize) - 1, "Bufsize cannot be a power of 2 - 1");
    constexpr static auto bits = std::bit_width(Bufsize);
    constexpr static auto mask = (Index(1) << bits) - 1;
    static_assert(mask > Bufsize, "not enough free bits for the sentinel");

    constexpr static auto Maxstrings = (sizeof(Index) * 8 - 1) / bits;
    constexpr static auto bitmask_size = Maxstrings * bits;
    constexpr static Index fullmask = (Index(1) << bitmask_size) - 1;

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
            return end[-ptrdiff_t(idx) - 1];
        }
        inline u64 offset(size_t idx) const {
            auto endalloc = reinterpret_cast<const char *>(this);
            auto end = reinterpret_cast<const u64 *>(endalloc + capacity);
            return end[-ptrdiff_t(idx) - 1];
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
    static_assert(Bufsize >= sizeof(heapvec *), "Bufsize is smaller than a pointer!");

    union {
        struct {
            Index inplace : 1, lengths : bitmask_size;
            std::array<char, Bufsize> data{};
        };
        struct {
            Index index_storage;
            std::array<char, (Bufsize - sizeof(heapvec *))> datasmol;
            heapvec *heap;
        };
    };
    // [[deprecated("Bufsize wastes space!!")]] constexpr static bool is_wasteful_f() { return {}; }
    // constexpr static auto is_wasteful = Bufsize % sizeof(heapvec *) == 0 || is_wasteful_f();

    struct decoded {
        u8 nfields;
        u8 size;
        std::array<u8, Maxstrings> lenarray;
    };

    // this is a bounded number of iterations, on a very tiny amount of data
    inline auto inplace_decode() const {
        u64 lens = lengths;
        u8 totalsize{}, i{};
        std::array<u8, Maxstrings> lenarray{};

        for (; (lens & mask) != mask && i < Maxstrings; lens >>= bits, i++) {
            lenarray[i] = lens & mask;
            totalsize += lenarray[i] + 1;
            // size += strlen(&data[lens & mask]) + 1;
            // needs strlen because the next offset could be the sentinel
            // (ssv doesn't actually store where a string ends)
            // todo: change this?
        }

        return decoded{i, totalsize, lenarray};
    }

  public:
    using index = Index;

    ssv()
        : inplace(1),
          lengths(fullmask) {}

    ssv(const ssv &other) {
        if (this == &other)
            return;

        inplace = other.inplace;
        lengths = other.lengths;

        if (inplace)
            data = other.data;
        else {
            datasmol = other.datasmol;
            void *alloc = malloc(other.heap->capacity);
            if (alloc == nullptr)
                throw std::bad_alloc();
            heap = reinterpret_cast<heapvec *>(alloc);
            memcpy(heap, other.heap, other.heap->capacity);
        }
    }
    ssv(ssv &&other) noexcept {
        if (this == &other)
            return;
        std::swap(data, other.data);
        std::swap(index_storage, other.index_storage);
    }
    ssv &operator=(const ssv &other) noexcept {
        if (this != &other) {
            ssv tmp(other);
            std::swap(data, tmp.data);
            std::swap(index_storage, tmp.index_storage);
        }
        return *this;
    }
    ssv &operator=(ssv &&other) noexcept {
        if (this != &other) {
            std::swap(data, other.data);
            std::swap(index_storage, other.index_storage);
        }
        return *this;
    }
    ssv(std::initializer_list<std::string_view> list)
        : inplace(1),
          lengths(fullmask) {
        for (const auto &s : list)
            append(s);
    }
    template <typename inputit>
    ssv(inputit first, inputit last)
        : inplace(1),
          lengths(fullmask) {
        for (; first != last; ++first)
            append(*first);
    }

    ~ssv() {
        if (!inplace)
            free(heap);
    }

    u64 size() const {
        auto decoded = inplace_decode();
        return inplace ? decoded.size : decoded.size + heap->size();
    }
    u64 nstrings() const {
        auto decoded = inplace_decode();
        return inplace ? decoded.nfields : decoded.nfields + heap->nstrings;
    }
    bool empty() const { return inplace ? (lengths & mask) == mask : heap->nstrings == 0; }
    void clear() { *this = {}; }

    bool isonheap() const { return inplace == 0; }
    bool isinplace() const { return inplace == 1; }
    constexpr static auto bufsize() { return Bufsize; }
    constexpr static auto maxstrings() { return Maxstrings; }

    void append(std::string_view s) {
        if (inplace) {
            auto decoded = inplace_decode();
            if (decoded.nfields < Maxstrings && decoded.size + s.size() + 1 <= sizeof(data)) {
                lengths &= ~(mask << (decoded.nfields * bits));
                lengths |= u64{s.size()} << (decoded.nfields * bits);
                // we started with all 1s so the next field already contains the mask

                char *dest = &data[decoded.size];
                memcpy(dest, s.data(), s.size());
                dest[s.size()] = 0;
            }
            else {
                size_t spaceneeded = s.size() + 1 + sizeof(u64) + sizeof(heapvec);
                std::array<u8, Maxstrings> offsets;

                // copy all the strings overlapping with the pointer to the heap
                int mustmove = Maxstrings;
                size_t totalsize{};
                for (int i = 0; i < decoded.nfields; i++) {
                    offsets[i] = totalsize;
                    totalsize += decoded.lenarray[i] + 1;
                    if (totalsize > sizeof datasmol) {
                        if (mustmove == Maxstrings)
                            mustmove = i;
                        spaceneeded += decoded.lenarray[i] + 1;
                    }
                }
                // start with at least this much space
                if (spaceneeded < sizeof(*this))
                    spaceneeded = sizeof(*this);

                // align to 8 bytes because the offsets are u64s
                spaceneeded = (spaceneeded + 7) / 8 * 8;

                void *alloc = calloc(1, spaceneeded);
                if (alloc == nullptr)
                    throw std::bad_alloc();

                auto heapalloc = reinterpret_cast<decltype(heap)>(alloc);
                heapalloc->capacity = spaceneeded;

                inplace = 0;
                if (mustmove != Maxstrings) {
                    // mark them as not in place anymore
                    lengths |= mask << (mustmove * bits);

                    for (; mustmove < decoded.nfields; mustmove++)
                        heapalloc->append({&data[offsets[mustmove]], decoded.lenarray[mustmove]});
                }

                heap = heapalloc;
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
        auto decoded = inplace_decode();
        if (idx < decoded.nfields) {
            size_t offset = 0;
            for (auto i = 0u; i < idx; i++)
                offset += decoded.lenarray[i] + 1;
            return {&data[offset], decoded.lenarray[idx]};
        }
        return (*heap)[idx - decoded.nfields];
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
};
