#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <vector>

static std::size_t g_allocated = 0;

template <typename T>
struct CountingAllocator
{
    using value_type = T;
    CountingAllocator() = default;
    template <typename U> CountingAllocator(const CountingAllocator<U>&) {}
    T* allocate(std::size_t n)
    {
        g_allocated += n * sizeof(T);
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) { ::operator delete(p); }
};

template <typename T, typename U>
bool operator==(const CountingAllocator<T>&, const CountingAllocator<U>&) { return true; }

using IdSet = std::set<uint16_t, std::less<uint16_t>, CountingAllocator<uint16_t>>;
struct MapEntry
{
    uint32_t lastHeardMs{};
    uint16_t lastSuperframe{};
    uint8_t state{};
    uint8_t missed{};
    IdSet advertised;
};
using MapTable = std::map<uint16_t,
                          MapEntry,
                          std::less<uint16_t>,
                          CountingAllocator<std::pair<const uint16_t, MapEntry>>>;

struct ArrayMeta
{
    uint32_t lastHeardMs{};
    uint16_t lastSuperframe{};
    uint8_t state{};
    uint8_t missed{};
};

template <typename Fn>
double TimedMicroseconds(Fn&& fn)
{
    const auto begin = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 2000; ++repeat) fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(end - begin).count() / 2000.0;
}

int main()
{
    std::cout << "node_limit,mean_degree,backend,allocated_bytes,bytes_per_active_neighbor,"
                 "two_hop_rebuild_us\n";
    for (const uint16_t n : {64, 128, 256})
    {
        for (const uint16_t degree : {4, 8, 16, 32})
        {
            if (degree >= n) continue;
            g_allocated = 0;
            MapTable table;
            for (uint16_t via = 0; via < degree; ++via)
            {
                auto& entry = table[via];
                entry.state = 1;
                for (uint16_t k = 1; k <= degree; ++k)
                    entry.advertised.insert(static_cast<uint16_t>((via + k) % n));
            }
            const auto mapBytes = g_allocated;
            const auto mapTime = TimedMicroseconds([&]() {
                IdSet twoHop;
                for (const auto& [via, entry] : table)
                    for (const auto target : entry.advertised)
                        if (target != via) twoHop.insert(target);
            });
            std::cout << n << ',' << degree << ",map-set," << mapBytes << ','
                      << static_cast<double>(mapBytes) / degree << ',' << mapTime << '\n';

            g_allocated = 0;
            const std::size_t words = (n + 63U) / 64U;
            std::vector<ArrayMeta, CountingAllocator<ArrayMeta>> meta(n);
            std::vector<uint64_t, CountingAllocator<uint64_t>> active(words);
            std::vector<uint64_t, CountingAllocator<uint64_t>> rows(n * words);
            for (uint16_t via = 0; via < degree; ++via)
            {
                meta[via].state = 1;
                active[via / 64U] |= UINT64_C(1) << (via % 64U);
                for (uint16_t k = 1; k <= degree; ++k)
                {
                    const uint16_t target = static_cast<uint16_t>((via + k) % n);
                    rows[via * words + target / 64U] |= UINT64_C(1) << (target % 64U);
                }
            }
            const auto arrayBytes = g_allocated;
            const auto arrayTime = TimedMicroseconds([&]() {
                std::vector<uint64_t> twoHop(words);
                for (uint16_t via = 0; via < n; ++via)
                    if (active[via / 64U] & (UINT64_C(1) << (via % 64U)))
                        for (std::size_t word = 0; word < words; ++word)
                            twoHop[word] |= rows[via * words + word];
            });
            std::cout << n << ',' << degree << ",array-bitmap," << arrayBytes << ','
                      << static_cast<double>(arrayBytes) / degree << ',' << arrayTime << '\n';

            g_allocated = 0;
            std::vector<ArrayMeta, CountingAllocator<ArrayMeta>> sparseMeta(degree);
            std::vector<uint32_t, CountingAllocator<uint32_t>> offsets(degree + 1);
            std::vector<uint16_t, CountingAllocator<uint16_t>> advertised;
            advertised.reserve(static_cast<std::size_t>(degree) * degree);
            for (uint16_t via = 0; via < degree; ++via)
            {
                sparseMeta[via].state = 1;
                offsets[via] = advertised.size();
                for (uint16_t k = 1; k <= degree; ++k)
                    advertised.push_back(static_cast<uint16_t>((via + k) % n));
            }
            offsets[degree] = advertised.size();
            const auto sparseBytes = g_allocated;
            const auto sparseTime = TimedMicroseconds([&]() {
                std::vector<bool> twoHop(n);
                for (uint16_t via = 0; via < degree; ++via)
                    for (uint32_t index = offsets[via]; index < offsets[via + 1]; ++index)
                        twoHop[advertised[index]] = true;
            });
            std::cout << n << ',' << degree << ",sparse-flat-list," << sparseBytes << ','
                      << static_cast<double>(sparseBytes) / degree << ',' << sparseTime << '\n';
        }
    }
}
