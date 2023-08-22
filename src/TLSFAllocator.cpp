//#include "TLSFAllocator.hpp"
//
//#include <bitset>
//#include <cassert>
//#include <iostream>
//
//TLSFAllocator::TLSFAllocator(std::byte* mainMemory, size_t byteSize)
//	: mMemory(mainMemory)
//	, mMaxSize(byteSize - sizeof(TLSFBlockHeader) - sizeof(uint32_t))
//	, mAllSize(byteSize)
//	, mBlockArraySize((static_cast<size_t>(getMSB(mMaxSize)) - kSplitNum + 1)* (1ull << kSplitNum))
//{
//	// allocate block array
//	mBlockArray = new BoundaryBlock<TLSFBlockHeader>*[mBlockArraySize];
//	// clear BlockArray and create initial block
//	clearAll();
//}
//
//TLSFAllocator::~TLSFAllocator()
//{
//#ifndef NDEBUG
//	for (size_t i = 0; i < mBlockArraySize; ++i)
//	{
//		if (mBlockArray[i] && reinterpret_cast<std::byte*>(mBlockArray[i]) >= mMemory && reinterpret_cast<std::byte*>(mBlockArray[i]) <= mMemory + mMaxSize)
//		{
//			mBlockArray[i]->~BoundaryBlock();
//		}
//	}
//#endif
//
//	delete[] mBlockArray;
//}
//
//void TLSFAllocator::clearAll()
//{
//	for (size_t i = 0; i < mBlockArraySize; ++i)
//	{
//		mBlockArray[i] = nullptr;
//	}
//
//	BoundaryBlock<TLSFBlockHeader>* block = new (mMemory) BoundaryBlock<TLSFBlockHeader>(mMaxSize);
//	mBlockArray[getBlockArrayIndex(getMSB(mMaxSize), 0)] = block;
//	mAllFLI = 1 << getMSB(mMaxSize);
//}
//
//std::byte* TLSFAllocator::allocate(size_t size)
//{
//	if (size < 0)
//	{
//		assert(!"invalid allocation size!");
//		return nullptr;
//	}
//	if (size < (1ull << kSplitNum))
//	{
//		assert(!"too small size!");
//		size = 1ull << kSplitNum;
//	}
//
//	if (size > mMaxSize)
//	{
//		assert(!"requested size is over max size!");
//		return nullptr;
//	}
//
//	const uint32_t FLI = getMSB(size);
//	uint32_t SLI	   = getSecondLevel(size, FLI, kSplitNum);
//
//	BoundaryBlock<TLSFBlockHeader>* target = mBlockArray[getBlockArrayIndex(FLI, SLI)];
//
//	TLSFBlockHeader* header = nullptr;
//	if (target)
//	{
//		header = &target->header;
//
//		for (size_t i = 0; header->next && header->used; ++i)
//		{
//			header = header->next;
//		}
//	}
//
//	if (!target || !header || header->used)  // 今より上の階層で探す
//	{
//		target = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(header);
//
//		uint32_t freeListBit = 0;
//		for (size_t i = 0; i < (1ull << kSplitNum); ++i)
//		{
//			if (mBlockArray[getBlockArrayIndex(FLI, i)])
//			{
//				TLSFBlockHeader* h = &mBlockArray[getBlockArrayIndex(FLI, i)]->header;
//				for (size_t index = 0; h->next && h->used; ++index)
//				{
//					h = h->next;
//				}
//
//				if (!h->used)
//				{
//					freeListBit |= 1 << i;
//				}
//			}
//		}
//
//		uint32_t newSLI = getFreeListSLI(SLI, freeListBit);
//		if (newSLI == -1)  // second levelにはなかった
//		{
//			uint32_t newFLI = getFreeListFLI(FLI, mAllFLI);
//
//			if (newFLI == -1)  // マジでなかった
//			{
//				assert(!"failed to allocate!");
//				return nullptr;
//			}
//
//			// 一つ上のFLIのメモリブロックを分割して作成する 
//			for (int i = (1 << kSplitNum) - 1; i >= 0; --i)
//			{
//				auto* biggerBlock = mBlockArray[getBlockArrayIndex(newFLI, i)];
//
//				if (biggerBlock && !biggerBlock->header.used)
//				{
//					if (!biggerBlock->enableSplit(size))
//					{
//						assert(!"falied to split biggest block!");
//						return nullptr;
//					}
//
//					// AllFLIから分割したところを消す
//					unregisterFLI(biggerBlock->getMemorySize());
//					// 分割したので消去
//					mBlockArray[getBlockArrayIndex(newFLI, i)] = nullptr;
//
//					// 取得するのは分割したのこり
//					auto* splitted = biggerBlock->split(size);
//					// 分割した対象サイズのものを当てはまる場所にセットする
//					mBlockArray[getBlockArrayIndex(FLI, SLI)] = biggerBlock;
//
//					uint32_t splittedFLI = getMSB(splitted->getMemorySize());
//					uint32_t splittedSLI = getSecondLevel(splitted->getMemorySize(), splittedFLI, kSplitNum);
//					if (!mBlockArray[(newFLI - kSplitNum) * (1 << kSplitNum) + i])
//					{
//						mBlockArray[getBlockArrayIndex(splittedFLI, splittedSLI)] = splitted;
//						splitted->header.used = false;
//					}
//					else
//					{
//						auto* header = &mBlockArray[getBlockArrayIndex(newFLI, i)]->header;
//						auto* splittedHeader = &splitted->header;
//						header->next = splittedHeader;
//						splittedHeader->pre = header;
//						splittedHeader->next = nullptr;
//					}
//					// 新しく追加したFLIを登録
//					registerFLI(splitted->getMemorySize());
//					break;
//				}
//			}
//		}
//		else
//		{
//			SLI = newSLI;
//		}
//
//		target = mBlockArray[getBlockArrayIndex(FLI, SLI)];
//	}
//
//
//	target->header.used = true;
//	return reinterpret_cast<std::byte*>(target->getMemory());
//}
//
//bool TLSFAllocator::deallocate(void* address)
//{
//	if (!address)
//	{
//		assert(!"invalid free address!");
//		return false;
//	}
//
//	BoundaryBlock<TLSFBlockHeader>* pBlock = nullptr;
//	{
//		// 管理メモリからsizeof(TLSFBlockHeader)だけさかのぼってヘッダのアドレスを取得
//		auto* p = reinterpret_cast<std::byte*>(address);
//		pBlock = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(p - sizeof(TLSFBlockHeader));
//	}
//
//	pBlock->header.used = false;
//	assert(reinterpret_cast<TLSFBlockHeader*>(pBlock) == &pBlock->header);
//
//	removeBlockFromList(pBlock);
//
//	// 両隣が範囲外か使用されていればmergeしない
//	const auto* right = reinterpret_cast<std::byte*>(pBlock->next());
//	const auto* left = reinterpret_cast<std::byte*>(pBlock->prev());
//	const bool isRightFree = right && right >= mMemory && right <= (mMemory + mAllSize) && !(pBlock->next()->header.used) && pBlock->next()->getMemorySize();
//	const bool isLeftFree = left && left <= (mMemory + mAllSize) && left >= mMemory && !(pBlock->prev()->header.used) && pBlock->prev()->getMemorySize();
//
//	if (isRightFree)  // 右が空いてるのでマージ
//	{
//		removeBlockFromList(pBlock->next());
//
//		pBlock->merge();
//	}
//
//	// 左のブロックをマージ
//	if (isLeftFree)
//	{
//		// 現在のブロックを消し、左ブロックと統合
//		removeBlockFromList(pBlock->prev(), pBlock);
//		
//		pBlock = pBlock->prev();
//		pBlock->merge();
//	}
//
//	// どこのFLI, SLIにあたるか検索して, 再登録
//	const uint32_t FLI = getMSB(pBlock->getMemorySize());
//	const uint32_t SLI = getSecondLevel(pBlock->getMemorySize(), FLI, kSplitNum);
//
//#ifndef NDEBUG
//	{
//		auto* p = reinterpret_cast<std::byte*>(mBlockArray[getBlockArrayIndex(FLI, SLI)]);
//		assert(!p || !(p < mMemory || p > mMemory + mMaxSize) || !"invalid memory!!!");
//	}
//#endif
//
//	// マージしたブロックを登録する
//	if (!mBlockArray[getBlockArrayIndex(FLI, SLI)])
//	{
//		mBlockArray[getBlockArrayIndex(FLI, SLI)] = pBlock;
//	}
//	else
//	{
//		auto* header = &mBlockArray[getBlockArrayIndex(FLI, SLI)]->header;
//		auto* newHeader = &pBlock->header;
//		header->next = newHeader;
//		newHeader->pre = header;
//		newHeader->next = nullptr;
//	}
//
//	registerFLI(pBlock->getMemorySize());
//
//	return true;
//}
//
//void TLSFAllocator::dump()
//{
//	const auto maxFLI = getMSB(mMaxSize);
//	const auto maxSLI = 1 << kSplitNum;
//
//	std::cerr << "----------------dump-----------------\n";
//	for (size_t fli = 0; fli <= maxFLI; ++fli)
//	{
//		for (size_t sli = 0; sli < maxSLI; ++sli)
//		{
//			std::cerr << (1ull << fli) + (1ull << fli) / (1ull << kSplitNum) * sli << "(" << fli << " , " << sli << ") : ";
//			auto* p = reinterpret_cast<std::byte*>(mBlockArray[getBlockArrayIndex(fli, sli)]);
//			if (!p)
//			{
//				std::cerr << "null\n";
//			}
//			else if (p < mMemory || p >= mMemory + mAllSize)
//			{
//				std::cerr << "invalid\n";
//			}
//			else
//			{
//				std::cerr << "found\n";
//			}
//		}
//	}
//	std::cerr << "----------------end of dump-----------------\n";
//}
//
//uint32_t TLSFAllocator::getMSB(uint32_t data) const
//{
//	if (data == 0)
//	{
//		return 0;
//	}
//
//	uint32_t index = 0;
//	for (; data != 1; ++index)
//	{
//		data = data >> 1;
//	}
//
//	return index;
//}
//
//uint32_t TLSFAllocator::getLSB(uint32_t data) const
//{
//	uint32_t index = 0;
//	for (; data % 2 == 0; ++index)
//	{
//		data = data >> 1;
//	}
//
//	return index;
//}
//
//uint32_t TLSFAllocator::getSecondLevel(uint32_t size, uint32_t MSB, uint32_t N) const
//{
//	// 最上位ビット未満のビット列だけを有効にするマスク
//	const unsigned mask = (1 << MSB) - 1;  // 1000 0000 -> 0111 1111
//
//	// 右へのシフト数を算出
//	const unsigned rs = MSB - N;  // 7 - 3 = 4 （8分割ならN=3）
//
//	// 引数sizeにマスクをかけて、右へシフトすればインデックスに
//	return (size & mask) >> rs;
//}
//
//uint32_t TLSFAllocator::getFreeListSLI(uint32_t mySLI, uint32_t freeListBit)
//{
//	// 自分のSLI以上が立っているビット列を作成 (ID = 0なら0xffffffff）
//	uint32_t myBit = 0xffffffff << mySLI;
//
//	// myBitとfreeListBitを論理積すれば、確保可能なブロックを持つSLIが得られる
//	uint32_t enableListBit = freeListBit & myBit;
//
//	// LSBを求めれば確保可能な一番サイズの小さいフリーリストブロックの発見
//	if (enableListBit == 0)
//	{
//		return -1;  // フリーリスト無し
//	}
//
//	return getLSB(enableListBit);
//}
//
//uint32_t TLSFAllocator::getFreeListFLI(uint32_t myFLI, uint32_t globalFLI)
//{
//	uint32_t myBit = 0xffffffff << myFLI;
//	uint32_t enableFLIBit = globalFLI & myBit;
//	if (enableFLIBit == 0)
//	{
//		return -1;  // メモリが完全に無い
//	}
//
//	return getMSB(enableFLIBit);
//}
//
//size_t TLSFAllocator::getBlockArrayIndex(const uint32_t FLI, const uint32_t SLI) const
//{
//	return (FLI - kSplitNum) * (1 << kSplitNum) + SLI;
//}
//
//inline void TLSFAllocator::registerFLI(const uint32_t memorySize)
//{
//	mAllFLI |= (1 << getMSB(memorySize));
//}
//
//inline void TLSFAllocator::unregisterFLI(const uint32_t memorySize)
//{
//	mAllFLI &= ~(1 << getMSB(memorySize));
//}
//
//inline void TLSFAllocator::removeBlockFromList(BoundaryBlock<TLSFBlockHeader>* pBlock, BoundaryBlock<TLSFBlockHeader>* pRight)
//{
//	// そのブロックと同じFLI, SLIのブロックが存在しない
//	if (!pBlock->header.next && !pBlock->header.pre)
//	{
//		unregisterFLI(pBlock->getMemorySize());
//		const auto FLI = getMSB(pBlock->getMemorySize());
//		const auto SLI = getSecondLevel(pBlock->getMemorySize(), FLI, kSplitNum);
//		mBlockArray[getBlockArrayIndex(FLI, SLI)] = nullptr;
//	}
//	// 前のブロックは存在した
//	else if (pBlock->header.pre)
//	{
//		pBlock->header.pre->next = pBlock->header.next;
//		if (pBlock->header.next)
//		{
//			pBlock->header.next->pre = pBlock->header.pre;
//		}
//	}
//	// 後ろのブロックのみ存在した
//	else if (pBlock->header.next)
//	{
//		pBlock->header.next->pre = nullptr;
//		const auto FLI = getMSB(pBlock->getMemorySize());
//		const auto SLI = getSecondLevel(pBlock->getMemorySize(), FLI, kSplitNum);
//		if (pRight)
//		{
//			mBlockArray[getBlockArrayIndex(FLI, SLI)] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pRight->header.next);
//		}
//		else
//		{
//			mBlockArray[getBlockArrayIndex(FLI, SLI)] = reinterpret_cast<BoundaryBlock<TLSFBlockHeader>*>(pBlock->header.next);
//		}
//	}
//	else
//	{
//		assert(!"invalid");
//	}
//}
