#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>

// BoundaryBlock�p�w�b�_
class BoundaryBlockHeader
{
    uint32_t size;

public:
    BoundaryBlockHeader()
        : size() {}
    uint32_t getSize() const { return size; }
    void setSize(uint32_t size) { this->size = size; }
};

// Boundaryblock�N���X
template <class Header = BoundaryBlockHeader, class EndTag = uint32_t>
class BoundaryBlock
{
    // ��[�^�O����������
    void writeEndTag()
    {
        new ((std::byte*)this + getBlockSize() - getEndTagSize()) EndTag(getBlockSize());
    }

    // ��[�^�O�T�C�Y���擾
    uint32_t getEndTagSize()
    {
        return sizeof(EndTag);
    }

public:
    Header header;

    BoundaryBlock(uint32_t size)
    {
        header.setSize(size);
        writeEndTag();
    };

    // �Ǘ��������ւ̃|�C���^���擾
    void* getMemory()
    {
        return (std::byte*)this + sizeof(BoundaryBlock);
    }

    // �Ǘ��������T�C�Y���擾
    uint32_t getMemorySize() const
    {
        return header.getSize();
    }

    // �u���b�N�T�C�Y���擾
    uint32_t getBlockSize()
    {
        return sizeof(BoundaryBlock) + header.getSize() + getEndTagSize();
    }

    // ���̃u���b�N�ւ̃|�C���^���擾
    BoundaryBlock* next()
    {
        return (BoundaryBlock*)((std::byte*)this + getBlockSize());
    }

    // �O�̃u���b�N�ւ̃|�C���^���擾
    BoundaryBlock* prev()
    {
        std::uint32_t* preSize = (std::uint32_t*)((std::byte*)this - getEndTagSize());
        return (BoundaryBlock*)((std::byte*)this - *preSize);
    }

    // �E�u���b�N���}�[�W
    void merge()
    {
        // �E�u���b�N���擾
        BoundaryBlock* nextBlock = next();
        // �^�O��ύX
        uint32_t newSize = header.getSize() + getEndTagSize() +
            sizeof(BoundaryBlock) + nextBlock->getMemorySize();
        header.setSize(newSize);
        writeEndTag();
    }

    // �u���b�N�𕪊�
    BoundaryBlock* split(uint32_t size)
    {
        // �V�K�u���b�N�����T�C�Y���������NULL
        uint32_t needSize = size + getEndTagSize() + sizeof(BoundaryBlock);
        if (needSize > header.getSize())
            return nullptr;

        // �V�K�u���b�N�̃������T�C�Y���Z�o
        uint32_t newBlockMemSize = header.getSize() - needSize;

        // �����T�C�Y�������T�C�Y�ɏk��
        header.setSize(size);
        writeEndTag();

        // �V�K�u���b�N���쐬
        BoundaryBlock* newBlock = next();
        new (newBlock) BoundaryBlock(newBlockMemSize);

        return newBlock;
    }

    // �w��T�C�Y�ɕ����\�H
    bool enableSplit(uint32_t size)
    {
        // �V�K�u���b�N�����T�C�Y���������NULL
        uint32_t needSize = size + getEndTagSize() + sizeof(BoundaryBlock);

        return needSize <= header.getSize();
    }
};


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


template<uint32_t kSplitNum = 4>
class TLSFAllocator
{
public:
    TLSFAllocator() = delete;

    // �R���X�g���N�^
    TLSFAllocator(std::byte* mainMemory, uint32_t byteSize)
    : mMemory(mainMemory)
        , mMaxSize(byteSize - sizeof(TLSFBlockHeader) - sizeof(uint32_t))
        , mAllSize(byteSize)
        , mBlockArraySize((static_cast<uint32_t>(getMSB(mMaxSize)) - kSplitNum + 1)* (1ul << kSplitNum))
    {
        mBlockArray = new BoundaryBlock<TLSFBlockHeader>*[mBlockArraySize];
        clearAll();
    }

    ~TLSFAllocator()
    {
#ifndef NDEBUG
        for (size_t i = 0; i < mBlockArraySize; ++i)
        {
            if (mBlockArray[i] && reinterpret_cast<std::byte*>(mBlockArray[i]) >= mMemory && reinterpret_cast<std::byte*>(mBlockArray[i]) <= mMemory + mMaxSize)
            {
                mBlockArray[i]->~BoundaryBlock();
            }
        }
#endif

        delete[] mBlockArray;
    }

    // ����
    std::byte* allocate(uint32_t size)
    {
        if (size < 0)
        {
            assert(!"invalid allocation size!");
            return nullptr;
        }
        if (size < (1ul << kSplitNum))
        {
            assert(!"too small size!");
            size = 1ul << kSplitNum;
        }

        if (size > mMaxSize)
        {
            assert(!"requested size is over max size!");
            return nullptr;
        }

        const uint32_t FLI = getMSB(size);
        uint32_t SLI = getSecondLevel(size, FLI, kSplitNum);

        BoundaryBlock<TLSFBlockHeader>* target = mBlockArray[getBlockArrayIndex(FLI, SLI)];

        TLSFBlockHeader* header = nullptr;
        if (target)
        {
            header = &target->header;

            for (size_t i = 0; header->next && header->used; ++i)
            {
                header = header->next;
            }
        }

        if (!target || !header || header->used)  // ������̊K�w�ŒT��
        {
            target = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(header);

            uint32_t freeListBit = 0;
            for (uint32_t i = 0; i < (1ul << kSplitNum); ++i)
            {
                if (mBlockArray[getBlockArrayIndex(FLI, i)])
                {
                    TLSFBlockHeader* h = &mBlockArray[getBlockArrayIndex(FLI, i)]->header;
                    for (size_t index = 0; h->next && h->used; ++index)
                    {
                        h = h->next;
                    }

                    if (!h->used)
                    {
                        freeListBit |= 1 << i;
                    }
                }
            }

            uint32_t newSLI = getFreeListSLI(SLI, freeListBit);
            if (newSLI == -1)  // second level�ɂ͂Ȃ�����
            {
                uint32_t newFLI = getFreeListFLI(FLI, mAllFLI);

                if (newFLI == -1)  // �S���Ȃ�����
                {
                    assert(!"failed to allocate!");
                    return nullptr;
                }

                // ����FLI�̃������u���b�N�𕪊����č쐬���� 
                for (int i = (1 << kSplitNum) - 1; i >= 0; --i)
                {
                    auto* biggerBlock = mBlockArray[getBlockArrayIndex(newFLI, i)];

                    if (biggerBlock && !biggerBlock->header.used)
                    {
                        if (!biggerBlock->enableSplit(size))
                        {
                            assert(!"falied to split biggest block!");
                            return nullptr;
                        }

                        // AllFLI���番�������Ƃ��������
                        unregisterFLI(biggerBlock->getMemorySize());
                        // ���������̂ŏ���
                        mBlockArray[getBlockArrayIndex(newFLI, i)] = nullptr;

                        // �擾����͕̂��������̂���
                        auto* splitted = biggerBlock->split(size);
                        // ���������ΏۃT�C�Y�̂��̂𓖂Ă͂܂�ꏊ�ɃZ�b�g����
                        mBlockArray[getBlockArrayIndex(FLI, SLI)] = biggerBlock;

                        uint32_t splittedFLI = getMSB(splitted->getMemorySize());
                        uint32_t splittedSLI = getSecondLevel(splitted->getMemorySize(), splittedFLI, kSplitNum);
                        if (!mBlockArray[(newFLI - kSplitNum) * (1 << kSplitNum) + i])
                        {
                            mBlockArray[getBlockArrayIndex(splittedFLI, splittedSLI)] = splitted;
                            splitted->header.used = false;
                        }
                        else
                        {
                            auto* header = &mBlockArray[getBlockArrayIndex(newFLI, i)]->header;
                            auto* splittedHeader = &splitted->header;
                            header->next = splittedHeader;
                            splittedHeader->pre = header;
                            splittedHeader->next = nullptr;
                        }
                        // �V�����ǉ�����FLI��o�^
                        registerFLI(splitted->getMemorySize());
                        break;
                    }
                }
            }
            else
            {
                SLI = newSLI;
            }

            target = mBlockArray[getBlockArrayIndex(FLI, SLI)];
        }


        target->header.used = true;
        return reinterpret_cast<std::byte*>(target->getMemory());
    }

    // �����̌^�w��Ver.
    template <typename T>
    T* allocate(uint32_t num)
    {
        return reinterpret_cast<T*>(allocate(sizeof(T) * num));
    }

    // �w�肳�ꂽ�A�h���X�����
    bool deallocate (void* address)
    {
        if (!address)
        {
            assert(!"invalid free address!");
            return false;
        }

        BoundaryBlock<TLSFBlockHeader>* pBlock = nullptr;
        {
            // �Ǘ�����������sizeof(TLSFBlockHeader)���������̂ڂ��ăw�b�_�̃A�h���X���擾
            auto* p = reinterpret_cast<std::byte*>(address);
            pBlock = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(p - sizeof(TLSFBlockHeader));
        }

        pBlock->header.used = false;
        assert(reinterpret_cast<TLSFBlockHeader*>(pBlock) == &pBlock->header);

        removeBlockFromList(pBlock);

        // ���ׂ��͈͊O���g�p����Ă����merge���Ȃ�
        const auto* right = reinterpret_cast<std::byte*>(pBlock->next());
        const auto* left = reinterpret_cast<std::byte*>(pBlock->prev());
        const bool isRightFree = right && right >= mMemory && right <= (mMemory + mAllSize) && !(pBlock->next()->header.used) && pBlock->next()->getMemorySize();
        const bool isLeftFree = left && left <= (mMemory + mAllSize) && left >= mMemory && !(pBlock->prev()->header.used) && pBlock->prev()->getMemorySize();

        if (isRightFree)  // �E���󂢂Ă�̂Ń}�[�W
        {
            removeBlockFromList(pBlock->next());

            pBlock->merge();
        }

        // ���̃u���b�N���}�[�W
        if (isLeftFree)
        {
            // ���݂̃u���b�N�������A���u���b�N�Ɠ���
            removeBlockFromList(pBlock->prev(), pBlock);

            pBlock = pBlock->prev();
            pBlock->merge();
        }

        // �ǂ���FLI, SLI�ɂ����邩��������, �ēo�^
        const uint32_t FLI = getMSB(pBlock->getMemorySize());
        const uint32_t SLI = getSecondLevel(pBlock->getMemorySize(), FLI, kSplitNum);

#ifndef NDEBUG
        {
            auto* p = reinterpret_cast<std::byte*>(mBlockArray[getBlockArrayIndex(FLI, SLI)]);
            assert(!p || !(p < mMemory || p > mMemory + mMaxSize) || !"invalid memory!!!");
        }
#endif

        // �}�[�W�����u���b�N��o�^����
        if (!mBlockArray[getBlockArrayIndex(FLI, SLI)])
        {
            mBlockArray[getBlockArrayIndex(FLI, SLI)] = pBlock;
        }
        else
        {
            auto* header = &mBlockArray[getBlockArrayIndex(FLI, SLI)]->header;
            auto* newHeader = &pBlock->header;
            header->next = newHeader;
            newHeader->pre = header;
            newHeader->next = nullptr;
        }

        registerFLI(pBlock->getMemorySize());

        return true;
    }

    //���ׂĉ�������Z�b�g����
    void clearAll()
    {
        for (size_t i = 0; i < mBlockArraySize; ++i)
        {
            mBlockArray[i] = nullptr;
        }

        BoundaryBlock<TLSFBlockHeader>* block = new (mMemory) BoundaryBlock<TLSFBlockHeader>(mMaxSize);
        mBlockArray[getBlockArrayIndex(getMSB(mMaxSize), 0)] = block;
        mAllFLI = 1 << getMSB(mMaxSize);
    }

    // ���݂̊��蓖�ď󋵂�dump����
    void dump()
    {
        const auto maxFLI = getMSB(mMaxSize);
        const auto maxSLI = 1 << kSplitNum;

        std::cerr << "----------------dump-----------------\n";
        for (size_t fli = 0; fli <= maxFLI; ++fli)
        {
            for (size_t sli = 0; sli < maxSLI; ++sli)
            {
                std::cerr << (1ul << fli) + (1ul << fli) / (1ul << kSplitNum) * sli << "(" << fli << " , " << sli << ") : ";
                auto* p = reinterpret_cast<std::byte*>(mBlockArray[getBlockArrayIndex(fli, sli)]);
                if (!p)
                {
                    std::cerr << "null\n";
                }
                else if (p < mMemory || p >= mMemory + mAllSize)
                {
                    std::cerr << "invalid\n";
                }
                else
                {
                    std::cerr << "found\n";
                }
            }
        }
        std::cerr << "----------------end of dump-----------------\n";
    }

private:

    inline uint32_t getMSB(uint32_t data) const
    {
        if (data == 0)
        {
            return 0;
        }

        uint32_t index = 0;
        for (; data != 1; ++index)
        {
            data = data >> 1;
        }

        return index;
    }

    inline uint32_t getLSB(uint32_t data) const
    {
        uint32_t index = 0;
        for (; data % 2 == 0; ++index)
        {
            data = data >> 1;
        }

        return index;
    }

    inline uint32_t getSecondLevel(uint32_t size, uint32_t MSB, uint32_t N) const
    {
        // �ŏ�ʃr�b�g�����̃r�b�g�񂾂���L���ɂ���}�X�N
        const uint32_t mask = (1 << MSB) - 1;  // 1000 0000 -> 0111 1111

        // �E�ւ̃V�t�g�����Z�o
        const uint32_t rs = MSB - N;  // 7 - 3 = 4 �i8�����Ȃ�N=3�j

        // ����size�Ƀ}�X�N�������āA�E�փV�t�g����΃C���f�b�N�X��
        return (size & mask) >> rs;
    }

    inline uint32_t getFreeListSLI(uint32_t mySLI, uint32_t freeListBit)
    {
        // ������SLI�ȏオ�����Ă���r�b�g����쐬 (ID = 0�Ȃ�0xffffffff�j
        uint32_t myBit = 0xffffffff << mySLI;

        // myBit��freeListBit��_���ς���΁A�m�ۉ\�ȃu���b�N������SLI��������
        uint32_t enableListBit = freeListBit & myBit;

        // LSB�����߂�Ίm�ۉ\�Ȉ�ԃT�C�Y�̏������t���[���X�g�u���b�N�̔���
        if (enableListBit == 0)
        {
            return -1;  // �t���[���X�g����
        }

        return getLSB(enableListBit);
    }

    inline uint32_t getFreeListFLI(uint32_t myFLI, uint32_t globalFLI)
    {
        uint32_t myBit = 0xffffffff << myFLI;
        uint32_t enableFLIBit = globalFLI & myBit;
        if (enableFLIBit == 0)
        {
            return -1;  // �����������S�ɖ���
        }

        return getMSB(enableFLIBit);
    }

    inline size_t getBlockArrayIndex(const uint32_t FLI, const uint32_t SLI) const
    {
        return (FLI - kSplitNum) * (1 << kSplitNum) + SLI;
    }

    inline void registerFLI(const uint32_t memorySize)
    {
        mAllFLI |= (1 << getMSB(memorySize));
    }

    inline void unregisterFLI(const uint32_t memorySize)
    {
        mAllFLI &= ~(1 << getMSB(memorySize));
    }

    inline void removeBlockFromList(BoundaryBlock<TLSFBlockHeader>* pBlock, BoundaryBlock<TLSFBlockHeader>* pRight = nullptr)
    {
        // ���̃u���b�N�Ɠ���FLI, SLI�̃u���b�N�����݂��Ȃ�
        if (!pBlock->header.next && !pBlock->header.pre)
        {
            unregisterFLI(pBlock->getMemorySize());
            const auto FLI = getMSB(pBlock->getMemorySize());
            const auto SLI = getSecondLevel(pBlock->getMemorySize(), FLI, kSplitNum);
            mBlockArray[getBlockArrayIndex(FLI, SLI)] = nullptr;
        }
        // �O�̃u���b�N�͑��݂���
        else if (pBlock->header.pre)
        {
            pBlock->header.pre->next = pBlock->header.next;
            if (pBlock->header.next)
            {
                pBlock->header.next->pre = pBlock->header.pre;
            }
        }
        // ���̃u���b�N�̂ݑ��݂���
        else if (pBlock->header.next)
        {
            pBlock->header.next->pre = nullptr;
            const auto FLI = getMSB(pBlock->getMemorySize());
            const auto SLI = getSecondLevel(pBlock->getMemorySize(), FLI, kSplitNum);
            if (pRight)
            {
                mBlockArray[getBlockArrayIndex(FLI, SLI)] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pRight->header.next);
            }
            else
            {
                mBlockArray[getBlockArrayIndex(FLI, SLI)] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pBlock->header.next);
            }
        }
        else
        {
            assert(!"invalid");
        }
    }


// �����o�ϐ�
    BoundaryBlock<TLSFBlockHeader>** mBlockArray;
    std::byte* mMemory;
    const uint32_t mMaxSize;
    const uint32_t mAllSize;  //�u���b�N���܂߂��S�̂̑傫�����w��
    const uint32_t mBlockArraySize;
    uint32_t mAllFLI;
};