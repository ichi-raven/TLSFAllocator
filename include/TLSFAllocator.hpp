#pragma once
#include <cstddef>
#include <cstdint>

#include "BoundaryBlock.hpp"

// 2^SPLITNUM����
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

    // �R���X�g���N�^
    TLSFAllocator(std::byte* mainMemory, size_t byteSize);
    ~TLSFAllocator();

    // ����
    std::byte* alloc(size_t size);

    // �����̌^�w��Ver.
    template <typename T>
    T* alloc(size_t num)
    {
        return reinterpret_cast<T*>(alloc(sizeof(T) * num));
    }

    // �w�肳�ꂽ�A�h���X�����
    bool free(void* address);

    //���ׂĉ�������Z�b�g����
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
    const size_t mAllSize;  //�u���b�N���܂߂��S�̂̑傫�����w��
    uint32_t mAllFLI;
};