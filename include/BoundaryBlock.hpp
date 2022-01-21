#pragma once

#include <iostream>

// BoundaryBlock用ヘッダ
class BoundaryBlockHeader
{
    uint32_t size;

public:
    BoundaryBlockHeader()
        : size() {}
    unsigned getSize() { return size; }
    void setSize(unsigned size) { this->size = size; }
};

// Boundaryblockクラス
template <class HEADER = BoundaryBlockHeader, class ENDTAG = uint32_t>
class BoundaryBlock
{
    // 後端タグを書き込み
    void writeEndTag()
    {
        new ((char*)this + getBlockSize() - getEndTagSize()) ENDTAG(getBlockSize());
    }

    // 後端タグサイズを取得
    unsigned getEndTagSize()
    {
        return sizeof(ENDTAG);
    }

public:
    HEADER header;

    BoundaryBlock(unsigned size)
    {
        header.setSize(size);
        writeEndTag();
    };

    // 管理メモリへのポインタを取得
    void* getMemory()
    {
        return (char*)this + sizeof(BoundaryBlock);
    }

    // 管理メモリサイズを取得
    unsigned getMemorySize()
    {
        return header.getSize();
    }

    // ブロックサイズを取得
    unsigned getBlockSize()
    {
        return sizeof(BoundaryBlock) + header.getSize() + getEndTagSize();
    }

    // 次のブロックへのポインタを取得
    BoundaryBlock* next()
    {
        return (BoundaryBlock*)((char*)this + getBlockSize());
    }

    // 前のブロックへのポインタを取得
    BoundaryBlock* prev()
    {
        unsigned* preSize = (unsigned*)((char*)this - getEndTagSize());
        return (BoundaryBlock*)((char*)this - *preSize);
    }

    // 右ブロックをマージ
    void merge()
    {
        // 右ブロックを取得
        BoundaryBlock* nextBlock = next();
        // タグを変更
        unsigned newSize = header.getSize() + getEndTagSize() +
                           sizeof(BoundaryBlock) + nextBlock->getMemorySize();
        header.setSize(newSize);
        writeEndTag();
    }

    // ブロックを分割
    BoundaryBlock* split(unsigned size)
    {
        // 新規ブロックを作るサイズが無ければNULL
        unsigned needSize = size + getEndTagSize() + sizeof(BoundaryBlock);
        if (needSize > header.getSize())
            return nullptr;

        // 新規ブロックのメモリサイズを算出
        unsigned newBlockMemSize = header.getSize() - needSize;

        // 既存サイズを引数サイズに縮小
        header.setSize(size);
        writeEndTag();

        // 新規ブロックを作成
        BoundaryBlock* newBlock = next();
        new (newBlock) BoundaryBlock(newBlockMemSize);

        return newBlock;
    }

    // 指定サイズに分割可能？
    bool enableSplit(unsigned size)
    {
        // 新規ブロックを作るサイズが無ければNULL
        unsigned needSize = size + getEndTagSize() + sizeof(BoundaryBlock);
        if (needSize > header.getSize())
            return false;
        return true;
    }
};
