#pragma once

#include <iostream>

// BoundaryBlock�p�w�b�_
class BoundaryBlockHeader
{
    uint32_t size;

public:
    BoundaryBlockHeader()
        : size() {}
    unsigned getSize() { return size; }
    void setSize(unsigned size) { this->size = size; }
};

// Boundaryblock�N���X
template <class HEADER = BoundaryBlockHeader, class ENDTAG = uint32_t>
class BoundaryBlock
{
    // ��[�^�O����������
    void writeEndTag()
    {
        new ((char*)this + getBlockSize() - getEndTagSize()) ENDTAG(getBlockSize());
    }

    // ��[�^�O�T�C�Y���擾
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

    // �Ǘ��������ւ̃|�C���^���擾
    void* getMemory()
    {
        return (char*)this + sizeof(BoundaryBlock);
    }

    // �Ǘ��������T�C�Y���擾
    unsigned getMemorySize()
    {
        return header.getSize();
    }

    // �u���b�N�T�C�Y���擾
    unsigned getBlockSize()
    {
        return sizeof(BoundaryBlock) + header.getSize() + getEndTagSize();
    }

    // ���̃u���b�N�ւ̃|�C���^���擾
    BoundaryBlock* next()
    {
        return (BoundaryBlock*)((char*)this + getBlockSize());
    }

    // �O�̃u���b�N�ւ̃|�C���^���擾
    BoundaryBlock* prev()
    {
        unsigned* preSize = (unsigned*)((char*)this - getEndTagSize());
        return (BoundaryBlock*)((char*)this - *preSize);
    }

    // �E�u���b�N���}�[�W
    void merge()
    {
        // �E�u���b�N���擾
        BoundaryBlock* nextBlock = next();
        // �^�O��ύX
        unsigned newSize = header.getSize() + getEndTagSize() +
                           sizeof(BoundaryBlock) + nextBlock->getMemorySize();
        header.setSize(newSize);
        writeEndTag();
    }

    // �u���b�N�𕪊�
    BoundaryBlock* split(unsigned size)
    {
        // �V�K�u���b�N�����T�C�Y���������NULL
        unsigned needSize = size + getEndTagSize() + sizeof(BoundaryBlock);
        if (needSize > header.getSize())
            return nullptr;

        // �V�K�u���b�N�̃������T�C�Y���Z�o
        unsigned newBlockMemSize = header.getSize() - needSize;

        // �����T�C�Y�������T�C�Y�ɏk��
        header.setSize(size);
        writeEndTag();

        // �V�K�u���b�N���쐬
        BoundaryBlock* newBlock = next();
        new (newBlock) BoundaryBlock(newBlockMemSize);

        return newBlock;
    }

    // �w��T�C�Y�ɕ����\�H
    bool enableSplit(unsigned size)
    {
        // �V�K�u���b�N�����T�C�Y���������NULL
        unsigned needSize = size + getEndTagSize() + sizeof(BoundaryBlock);
        if (needSize > header.getSize())
            return false;
        return true;
    }
};
