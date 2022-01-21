#pragma once
#include <cstddef>
#include <cstdint>

#include "BoundaryBlock.hpp"

// 2^SPLITNUM分割
#define SPLITNUM (4)

class TLSFBlockHeader : public BoundaryBlockHeader
{
public:
    TLSFBlockHeader* pre;
    TLSFBlockHeader* next;
    bool used;

    TLSFBlockHeader()
        : pre(nullptr)
        , next(nullptr)
        , used(false) {}
};

class TLSFAllocator
{
public:
    TLSFAllocator() = delete;

    // コンストラクタ
    TLSFAllocator(std::byte* mainMemory, size_t byteSize);
    ~TLSFAllocator();

    // 割当
    std::byte* alloc(size_t size);

    // 割当の型指定Ver.
    template <typename T>
    T* alloc(size_t num)
    {
        return reinterpret_cast<T*>(alloc(sizeof(T) * num));
    }

    // 指定されたアドレスを解放
    bool free(void* address);

    //すべて解放しリセットする
    void clearAll();

    void checkMemTable();

private:
    uint32_t getMSB(uint32_t data) const;

    uint32_t getLSB(uint32_t data) const;

    uint32_t getSecondLevel(uint32_t size, uint32_t MSB, uint32_t N) const;

    uint32_t getFreeListSLI(uint32_t mySLI, uint32_t freeListBit);

    uint32_t getFreeListFLI(uint32_t myFLI, uint32_t globalFLI);

    BoundaryBlock<TLSFBlockHeader>** mBlockArray;
    std::byte* mMemory;
    const size_t mMaxSize;
    const size_t mAllSize;  //ブロックも含めた全体の大きさを指す
    uint32_t mAllFLI;
};