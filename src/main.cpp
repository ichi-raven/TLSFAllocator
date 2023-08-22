#include <bitset>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

#include "TLSFAllocator.hpp"

template <typename T>
struct TestArray
{
    T& operator[](size_t index)
    {
        return memory[index];
    }

    T* memory;
    size_t size;
};

template <typename T>
void allocTest(TLSFAllocator<>& allocator, std::vector<TestArray<T>>& data, uint32_t maxSize, uint32_t testTime)
{
    uint32_t onceMax = maxSize / sizeof(T) / testTime;
    assert(onceMax * testTime < maxSize);
    uint32_t sizeSum = 0;
    std::random_device seed_gen;
    std::mt19937 engine(seed_gen());
    std::uniform_int_distribution<> dist(onceMax / 3, onceMax - 1);

    for (size_t i = 0; i < testTime; ++i)
    {
        TestArray<T> a;
        uint32_t s = dist(engine);
        sizeSum += s * sizeof(T);
        std::cerr << "allocated size : " << s * sizeof(T) << "\n";
        std::cerr << "all size : " << sizeSum << "\n";
        a.memory = allocator.allocate<T>(s);
        a.size   = s;
        assert(a.memory);
        for (uint32_t j = 0; j < s; ++j)  // write read test
        {
            auto test = j;
            a[j]      = test;
            assert(a[j] == test);
        }
        data.emplace_back(a);
    }
}

template <typename T>
void freeTest(TLSFAllocator<>& allocator, std::vector<TestArray<T>>& data)
{
    size_t sum = 0;
    for (auto& d : data)
    {
        // value check
        for (int i = 0; i < d.size; ++i)
        {
            assert(d[i] == i);
        }

        allocator.deallocate(d.memory);
        std::cerr << "free size : " << d.size * sizeof(T) << "\n";
        sum += d.size * sizeof(T);
    }
    std::cerr << "free all size : " << sum << "\n";
    data.clear();
}

void checkAllCleared(void* mainMemory)
{
    auto* p = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(mainMemory);
    std::cerr << "check size : " << p->header.getSize() << "\n";
}

int main()
{
    constexpr size_t maxSize          = 8192;
    constexpr size_t surplusBlockSize = sizeof(TLSFBlockHeader) + sizeof(uint32_t);
    std::byte* mainmemory             = new std::byte[maxSize];
    {
        TLSFAllocator allocator(mainmemory, maxSize);

        std::vector<TestArray<uint32_t>> data;
        uint32_t testTime = 40;
        for (size_t time = 0; time < 10000; ++time)
        {
            std::cerr << "test time : " << time << "\n";
            allocTest<uint32_t>(allocator, data, maxSize - surplusBlockSize * testTime, testTime);
            freeTest<uint32_t>(allocator, data);
            std::cerr << "end free\n";
            allocator.clearAll();
            //allocator.dump();
        }

        // 40
        auto* p  = allocator.allocate<uint32_t>(10);
        auto* p2 = allocator.allocate<uint32_t>(10);
        auto* p3 = allocator.allocate<uint32_t>(10);
        allocator.deallocate(p2);
        auto* p4 = allocator.allocate<uint32_t>(10);

        const auto r = rand() % 10;
        p2[r]        = 0xdeadbeef;
        assert(p4[r] == 0xdeadbeef);
        std::cerr << "list free test clear\n";

        auto* p5 = allocator.allocate<uint32_t>(10);

        for (size_t i = 0; i < 10; ++i)
        {
            p[i] = p3[i] = p5[i] = static_cast<uint32_t>(i);
            assert(p[i] == i);
            assert(p3[i] == i);
            assert(p5[i] == i);
        }

        std::cerr << "end test\n";
    }

    delete[] mainmemory;

    std::cerr << "clear main memory\n";

    std::cerr << "end all\n";
    return 0;
}
