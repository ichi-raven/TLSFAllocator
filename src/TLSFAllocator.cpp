#include "TLSFAllocator.hpp"

#include <bitset>
#include <cassert>
#include <iostream>

TLSFAllocator::TLSFAllocator(std::byte* mainMemory, size_t byteSize)
    : mMemory(mainMemory)
    , mMaxSize(byteSize - sizeof(TLSFBlockHeader) - sizeof(uint32_t))
    , mAllSize(byteSize)
{
    const size_t size = (getMSB(mMaxSize) - SPLITNUM + 1) * (1 << SPLITNUM);
    mBlockArray       = new BoundaryBlock<TLSFBlockHeader>*[size];
    // std::cerr << size << "\n";
    for (size_t i = 0; i < size; ++i)
        mBlockArray[i] = nullptr;
    BoundaryBlock<TLSFBlockHeader>* block                        = new (mMemory) BoundaryBlock<TLSFBlockHeader>(mMaxSize);
    mBlockArray[(getMSB(mMaxSize) - SPLITNUM) * (1 << SPLITNUM)] = block;
    // std::cerr << (getMSB(mMaxSize) - SPLITNUM) * (1 << SPLITNUM) << "\n";

    mAllFLI = 1 << getMSB(mMaxSize);
}

TLSFAllocator::~TLSFAllocator()
{
    const size_t size = (getMSB(mMaxSize) - SPLITNUM + 1) * (1 << SPLITNUM);
    for (size_t i = 0; i < size; ++i)
        if (mBlockArray[i] && reinterpret_cast<std::byte*>(mBlockArray[i]) >= mMemory && reinterpret_cast<std::byte*>(mBlockArray[i]) <= mMemory + mMaxSize)
            mBlockArray[i]->~BoundaryBlock();

    std::cerr << "allocator clear start\n";
    delete[] mBlockArray;
    // std::cerr << "allocator clear succeeded\n";
}

void TLSFAllocator::clearAll()
{
    const size_t size = (getMSB(mMaxSize) - SPLITNUM + 1) * (1 << SPLITNUM);
    for (size_t i = 0; i < size; ++i)
        mBlockArray[i] = nullptr;
    BoundaryBlock<TLSFBlockHeader>* block                        = new (mMemory) BoundaryBlock<TLSFBlockHeader>(mMaxSize);
    mBlockArray[(getMSB(mMaxSize) - SPLITNUM) * (1 << SPLITNUM)] = block;
    mAllFLI                                                      = 1 << getMSB(mMaxSize);
}

std::byte* TLSFAllocator::alloc(size_t size)
{
    if (size < 0)
    {
        // std::cerr << "size is invalid!\n";
        return nullptr;
    }
    if (size < (1 << SPLITNUM))
    {
        // std::cerr << "too small size!\n";
        size = 1 << SPLITNUM;
    }

    // size += sizeof(TLSFBlockHeader) + sizeof(uint32_t);
    if (size > mMaxSize)
    {
        // std::cerr << "requested size is over max size!\n";
        return nullptr;
    }
    // std::cerr << "requested size : " << size << "\n";
    // std::cerr << "prev all FLI : " << std::bitset<32>(mAllFLI) << "\n";

    uint32_t FLI = getMSB(size);
    uint32_t SLI = getSecondLevel(size, FLI, SPLITNUM);

    BoundaryBlock<TLSFBlockHeader>* target = mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI];

    TLSFBlockHeader* header = nullptr;
    if (target)
    {
        header = &target->header;

        for (size_t i = 0; header->next && header->used; ++i)
        {
            // std::cerr << "first search : " << i << "\n";
            header = header->next;
        }
    }

    if (!target || !header || header->used)  // 今より上の階層で探す
    {
        target = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(header);
        // std::cerr << "uprise\n";
        uint32_t freeListBit = 0;
        for (size_t i = 0; i < 1 << SPLITNUM; ++i)
        {
            if (mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + i])
            {
                TLSFBlockHeader* h = &mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + i]->header;
                for (size_t index = 0; h->next && h->used; ++index)
                {
                    // std::cerr << "second search : " << index << "\n";
                    h = h->next;
                }
                if (!h->used)
                    freeListBit |= 1 << i;
            }
        }

        // std::cerr << "freeList : " << std::bitset<32>(freeListBit) << "\n";
        uint32_t newSLI = getFreeListSLI(SLI, freeListBit);
        // std::cerr << "new SLI : " << newSLI << "\n";
        if (newSLI == -1)  // second levelにはなかった
        {
            // std::cerr << "none in Second Level\n";
            uint32_t newFLI = getFreeListFLI(FLI, mAllFLI);

            // std::cerr << "new FLI : " << newFLI << "\n";
            if (newFLI == -1)  // マジでなかった
            {
                // std::cerr << "failed to allocate!\n";
                return nullptr;
            }

            for (int i = (1 << SPLITNUM) - 1; i >= 0; --i)
            {
                auto* biggerBlock = mBlockArray[(newFLI - SPLITNUM) * (1 << SPLITNUM) + i];

                if (biggerBlock && !biggerBlock->header.used)
                {
                    // std::cerr << "got bigger block size : " << biggerBlock->getMemorySize() << "\n";
                    //  分割してつくる
                    if (!biggerBlock->enableSplit(size))
                    {
                        // std::cerr << "dead\n";
                        return nullptr;
                    }
                    // AllFLIから分割したところを消す
                    mAllFLI &= ~(1 << getMSB(biggerBlock->getMemorySize()));
                    // 分割したので消去
                    mBlockArray[(newFLI - SPLITNUM) * (1 << SPLITNUM) + i] = nullptr;

                    // std::cerr << "erased : " << (1 << getMSB(biggerBlock->getMemorySize())) <<"\n";
                    // 取得するのは分割したのこり
                    auto* splitted     = biggerBlock->split(size);
                    uint32_t debugSize = splitted->getMemorySize();
                    // 分割した対象サイズのものを当てはまる場所にセットする
                    mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = biggerBlock;

                    // std::cerr << "splitted size : " << splitted->getMemorySize() << "\n";

                    uint32_t splittedFLI = getMSB(splitted->getMemorySize());
                    uint32_t splittedSLI = getSecondLevel(splitted->getMemorySize(), splittedFLI, SPLITNUM);
                    if (!mBlockArray[(newFLI - SPLITNUM) * (1 << SPLITNUM) + i])
                    {
                        // std::cerr << "new splitted FLI : " << splittedFLI << "\n";
                        // std::cerr << "new splitted SLI : " << splittedSLI << "\n";

                        mBlockArray[(splittedFLI - SPLITNUM) * (1 << SPLITNUM) + splittedSLI] = splitted;
                        splitted->header.used                                                 = false;
                    }
                    else
                    {
                        // std::cerr << "added splitted FLI : " << splittedFLI << "\n";
                        // std::cerr << "added splitted SLI : " << splittedSLI << "\n";
                        auto* header         = &mBlockArray[(newFLI - SPLITNUM) * (1 << SPLITNUM) + i]->header;
                        auto* splittedHeader = &splitted->header;
                        header->next         = splittedHeader;
                        splittedHeader->pre  = header;
                        splittedHeader->next = nullptr;
                    }
                    // 新しく追加したFLIを登録
                    // mAllFLI |= 1 << getMSB(biggerBlock->getMemorySize()) | 1 << getMSB(splitted->getMemorySize());
                    mAllFLI |= 1 << splittedFLI;
                    // std::cerr << "check splitted size : " << splitted->getMemorySize() << "\n";
                    assert(splitted->getMemorySize() == debugSize);
                    break;
                }
            }
        }
        else
        {
            SLI = newSLI;
        }

        target = mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI];
    }
    // std::cerr << "FLI : " << FLI << "\n";
    // std::cerr << "SLI : " << SLI << "\n";
    // std::cerr << "all FLI : " << std::bitset<32>(mAllFLI) << "\n";
    target->header.used = true;
    // std::cerr << "allocated\n";
    return reinterpret_cast<std::byte*>(target->getMemory());
}

bool TLSFAllocator::free(void* address)
{
    if (!address)
    {
        // std::cerr << "invalid free address!\n";
        return false;
    }

    BoundaryBlock<TLSFBlockHeader>* pBlock = nullptr;
    {
        auto* p = reinterpret_cast<std::byte*>(address);
        pBlock  = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(p - sizeof(TLSFBlockHeader));
    }
    pBlock->header.used = false;
    assert(reinterpret_cast<TLSFBlockHeader*>(pBlock) == &pBlock->header);

    // リストの状態にしたがって解放
    if (!pBlock->header.next && !pBlock->header.pre)
    {
        // std::cerr << "detached from all FLI\n";
        mAllFLI &= ~(1 << getMSB(pBlock->getMemorySize()));
        auto FLI                                              = getMSB(pBlock->getMemorySize());
        auto SLI                                              = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = nullptr;
    }
    else if (pBlock->header.pre)
    {
        pBlock->header.pre->next = pBlock->header.next;
        if (pBlock->header.next)
            pBlock->header.next->pre = pBlock->header.pre;
    }
    else if (pBlock->header.next)
    {
        pBlock->header.next->pre                              = nullptr;
        auto FLI                                              = getMSB(pBlock->getMemorySize());
        auto SLI                                              = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pBlock->header.next);
    }

    // 完全に範囲外か使用されていればmergeしない
    auto* prev     = reinterpret_cast<std::byte*>(pBlock->prev());
    auto* next     = reinterpret_cast<std::byte*>(pBlock->next());
    bool rightFree = next && next >= mMemory && next <= (mMemory + mAllSize) && !(pBlock->next()->header.used) && pBlock->next()->getMemorySize();
    bool leftFree  = prev && prev <= (mMemory + mAllSize) && prev >= mMemory && !(pBlock->prev()->header.used) && pBlock->prev()->getMemorySize();

    // std::cerr << "free size : " << pBlock->getMemorySize() << "\n";
    if (rightFree)  // 右が空いてるのでマージ
    {
        auto* pRight = pBlock->next();
        // std::cerr << "free right\n";
        std::cerr << "FLI : " << getMSB(pRight->getMemorySize()) << ", SLI : " << getSecondLevel(pRight->getMemorySize(), getMSB(pRight->getMemorySize()), SPLITNUM) << "\n";

        if (!pRight->header.pre && !pRight->header.next)
        {
            // std::cerr << "detached right FLI\n";
            // 両方消す(サイズ更新して再登録するので)
            mAllFLI &= ~(1 << getMSB(pRight->getMemorySize()));
            // std::cerr << "del FLI : " << std::bitset<32>((1 << getMSB(pRight->getMemorySize()))) << "\n";

            {  // right
                // std::cerr << "nullptr right : " << pRight->getMemorySize() << "\n";
                auto FLI                                              = getMSB(pRight->getMemorySize());
                auto SLI                                              = getSecondLevel(pRight->getMemorySize(), FLI, SPLITNUM);
                mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = nullptr;
            }
        }
        else if (pRight->header.pre)
        {
            // std::cerr << pRight << "\n";
            // std::cerr << pRight->header.pre << "\n";
            // std::cerr << "debug\n";
            auto* p = reinterpret_cast<std::byte*>(pRight->header.pre);
            assert(!(p < mMemory || p >= mMemory + mAllSize));
            assert(pRight->header.next);
            pRight->header.pre->next = pRight->header.next;
            if (pRight->header.next)
                pRight->header.next->pre = pRight->header.pre;
        }
        else if (pRight->header.next)  // nextのみ存在
        {
            pRight->header.next->pre                              = nullptr;
            auto FLI                                              = getMSB(pRight->getMemorySize());
            auto SLI                                              = getSecondLevel(pRight->getMemorySize(), FLI, SPLITNUM);
            mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pRight->header.next);
        }
        else
        {
            assert(0);
            // pRight->header.pre->next = nullptr;
        }
        // std::cerr << "right size : " << pRight->getMemorySize() << "\n";
        pBlock->merge();
        // std::cerr << "merged size : " << pBlock->getMemorySize() << "\n";
    }

    if (!pBlock->header.next && !pBlock->header.pre)
    {
        // std::cerr << "detached from all FLI\n";
        mAllFLI &= ~(1 << getMSB(pBlock->getMemorySize()));
        auto FLI                                              = getMSB(pBlock->getMemorySize());
        auto SLI                                              = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = nullptr;
    }
    else if (pBlock->header.pre)
    {
        pBlock->header.pre->next = pBlock->header.next;
        if (pBlock->header.next)
            pBlock->header.next->pre = pBlock->header.pre;
    }
    else if (pBlock->header.next)
    {
        pBlock->header.next->pre                              = nullptr;
        auto FLI                                              = getMSB(pBlock->getMemorySize());
        auto SLI                                              = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pBlock->header.next);
    }
    else
    {
        assert(0);
    }

    if (leftFree)
    {
        // 現在のブロックを消し、左ブロックと統合
        // std::cerr << "detached left\n";
        // mAllFLI &= ~(1 << getMSB(pBlock->getMemorySize()));
        auto* pLeft = pBlock->prev();
        if (!pLeft->header.pre && !pLeft->header.next)
        {
            // std::cerr << "detached left FLI\n";
            mAllFLI &= ~(1 << getMSB(pLeft->getMemorySize()));

            {  // left
                ////std::cerr << "nullptr center : " << pLeft->getMemorySize() << "\n";
                auto FLI                                              = getMSB(pLeft->getMemorySize());
                auto SLI                                              = getSecondLevel(pLeft->getMemorySize(), FLI, SPLITNUM);
                mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = nullptr;
            }

            // std::cerr << "del FLI : " << std::bitset<32>((1 << getMSB(pBlock->getMemorySize()))) << "\n";
        }
        else if (pLeft->header.pre)
        {
            pLeft->header.pre->next = pLeft->header.next;
            if (pLeft->header.next)
                pLeft->header.next->pre = pLeft->header.pre;
        }
        else if (pLeft->header.next)
        {
            pLeft->header.next->pre                               = nullptr;
            auto FLI                                              = getMSB(pLeft->getMemorySize());
            auto SLI                                              = getSecondLevel(pLeft->getMemorySize(), FLI, SPLITNUM);
            mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pBlock->header.next);
        }

        pBlock = pBlock->prev();
        pBlock->merge();
    }

    if (!pBlock->header.next && !pBlock->header.pre)
    {
        // std::cerr << "detached from all FLI\n";
        mAllFLI &= ~(1 << getMSB(pBlock->getMemorySize()));
        auto FLI                                              = getMSB(pBlock->getMemorySize());
        auto SLI                                              = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = nullptr;
    }
    else if (pBlock->header.pre)
    {
        pBlock->header.pre->next = pBlock->header.next;
        if (pBlock->header.next)
            pBlock->header.next->pre = pBlock->header.pre;
    }
    else if (pBlock->header.next)
    {
        pBlock->header.next->pre                              = nullptr;
        auto FLI                                              = getMSB(pBlock->getMemorySize());
        auto SLI                                              = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pBlock->header.next);
    }

    std::cerr << "merged size : " << pBlock->getMemorySize() << "\n";
    // std::cerr << "all FLI : " << std::bitset<32>(mAllFLI) << "\n";

    // std::cerr << "merge succeeded\n";
    // どこのFLI, SLIにあたるか検索して, 再登録
    uint32_t FLI = getMSB(pBlock->getMemorySize());
    uint32_t SLI = getSecondLevel(pBlock->getMemorySize(), FLI, SPLITNUM);

    {
        auto* p = reinterpret_cast<std::byte*>(mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI]);

        if (p && (p < mMemory || p > mMemory + mMaxSize))
        {
            std::cerr << "p : " << p << "\n";
            std::cerr << "main memory : " << mMemory << "\n";
            assert(!"invalid memory!!!");
        }
    }

    if (!mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI])
    {
        // std::cerr << "new FLI\n" << FLI << "\n";
        mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI] = pBlock;
    }
    else
    {
        // std::cerr << "added FLI : " << FLI << "\n";
        auto* header    = &mBlockArray[(FLI - SPLITNUM) * (1 << SPLITNUM) + SLI]->header;
        auto* newHeader = &pBlock->header;
        header->next    = newHeader;
        newHeader->pre  = header;
        newHeader->next = nullptr;
    }

    mAllFLI |= 1 << getMSB(pBlock->getMemorySize());

    // std::cerr << "end free\n";
    return true;
}

void TLSFAllocator::checkMemTable()
{
    std::cerr << "----------------check memory-----------------\n";
    for (size_t i = 0; i <= getMSB(mMaxSize); ++i)
    {
        for (size_t j = 0; j < (1 << SPLITNUM); ++j)
        {
            std::cerr << (1 << i) + (1 << i) / (1 << SPLITNUM) * j << "(" << i << " , " << j << ") : ";
            auto* p = reinterpret_cast<std::byte*>(mBlockArray[(i - SPLITNUM) * (1 << SPLITNUM) + j]);
            if (!mBlockArray[(i - SPLITNUM) * (1 << SPLITNUM) + j])
            {
                std::cerr << "null\n";
            }
            else if (p < mMemory || p >= mMemory + mAllSize)
            {
                std::cerr << "invalid\n";
                // mBlockArray[(i - SPLITNUM) * (1 << SPLITNUM) + j] = nullptr;
                // assert(0);
            }
            else
            {
                std::cerr << "found\n";
            }
        }
    }
    std::cerr << "----------------end check memory-----------------\n";
}

uint32_t TLSFAllocator::getMSB(uint32_t data) const
{
    if (data == 0)
        return 0;
    uint32_t index;
    for (index = 0; data != 1; ++index)
        data = data >> 1;
    return index;
}

uint32_t TLSFAllocator::getLSB(uint32_t data) const
{
    uint32_t index;
    for (index = 0; !(data % 2); ++index)
        data = data >> 1;
    return index;
}

uint32_t TLSFAllocator::getSecondLevel(uint32_t size, uint32_t MSB, uint32_t N) const
{
    // 最上位ビット未満のビット列だけを有効にするマスク
    const unsigned mask = (1 << MSB) - 1;  // 1000 0000 -> 0111 1111

    // 右へのシフト数を算出
    const unsigned rs = MSB - N;  // 7 - 3 = 4 （8分割ならN=3です）

    // 引数sizeにマスクをかけて、右へシフトすればインデックスに
    return (size & mask) >> rs;
}

uint32_t TLSFAllocator::getFreeListSLI(uint32_t mySLI, uint32_t freeListBit)
{
    // 自分のSLI以上が立っているビット列を作成 (ID = 0なら0xffffffff）
    unsigned myBit = 0xffffffff << mySLI;

    // myBitとfreeListBitを論理積すれば、確保可能なブロックを持つSLIが出てきます
    unsigned enableListBit = freeListBit & myBit;

    // LSBを求めれば確保可能な一番サイズの小さいフリーリストブロックの発見
    if (enableListBit == 0)
        return -1;  // フリーリスト無し
    return getLSB(enableListBit);
}

uint32_t TLSFAllocator::getFreeListFLI(uint32_t myFLI, uint32_t globalFLI)
{
    uint32_t myBit        = 0xffffffff << myFLI;
    uint32_t enableFLIBit = globalFLI & myBit;
    if (enableFLIBit == 0)
        return -1;  // メモリが完全に無い！！
    return getMSB(enableFLIBit);
}