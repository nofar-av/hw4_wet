#include <unistd.h>
#include <iostream>
#include <cstring>
#include <sys/mman.h>


void* Sbrk(size_t size)
{
    void* p = Sbrk(size);
    if (p == (void*)(-1))
    {
        return nullptr;
    }
    return p;
}
typedef struct  malloc_meta_data_t{
    size_t size;
    bool is_free;
    void* p;
    //malloc_meta_data_t* next;
    //malloc_meta_data_t* prev;
    malloc_meta_data_t* lower;
    malloc_meta_data_t* higher;
    malloc_meta_data_t* free_next;
    malloc_meta_data_t* free_prev;
}MallocMetadata;

class MallocList {
    size_t free_blocks;
    size_t alloc_blocks; //free & used
    size_t free_bytes;
    size_t alloc_bytes; //free & used
    MallocMetadata* free_list_head;//  free list
    MallocMetadata* wilderness;// end of all blocks list
    MallocMetadata* mmaped_list_head;
     
public:
    MallocList()
    {
        this->free_blocks = 0;
        this->alloc_blocks = 0;
        this->free_bytes = 0;
        this->alloc_bytes = 0;
        this->free_list_head = nullptr;
        this->wilderness = nullptr;
        this->mmaped_list_head = nullptr;
    }
   /* ~MallocList ()//TODO::can we free a list? using what?
    {
        this->free_list_head->freeList();
    }*/
    //update free_ pointers to null and is_free <- false
    void updateBusyBlock(MallocMetadata* md)
    {
        if (md != nullptr)
        {
            md->is_free = false;
            md->free_next = nullptr;
            md->free_prev = nullptr;
        }
    }
    //update all pointers to null and is_free <- false
    void updateNewBlock(MallocMetadata* md)
    {
        if (md != nullptr) 
        {
            md->is_free = false;
            md->lower = nullptr;
            md->higher = nullptr;
            md->free_next = nullptr;
            md->free_prev = nullptr;
        }
    }
    static MallocList& getInstance() // make MallocList singleton
    {
        static MallocList instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }
    MallocMetadata* split(MallocMetadata* old_md, size_t size)
    {
        old_md->size = old_md->size - size - sizeof(MallocMetadata);
        MallocMetadata* new_md = (MallocMetadata*)(old_md->p + old_md->size);
        new_md->size = size;
        new_md->is_free = false;
        new_md->lower = old_md;
        new_md->higher = old_md->higher;
        new_md->p = old_md->p + old_md->size + sizeof(MallocMetadata);
        old_md->higher = new_md;
        old_md->is_free = true;
        this->free_blocks++;
        this->free_bytes += old_md->size;
        this->insertFreeBlock(new_md); //inserting new free block
        if (old_md->higher != nullptr)
        {
            old_md->higher->lower = new_md;
        }
        return old_md; //newly allocated block
    }
    MallocMetadata* mergeAdjBlocks (MallocMetadata* low, MallocMetadata* high, bool is_free)
    {
        this->free_blocks--;
        this->alloc_blocks--;
        if (is_free)
        {
            this->free_bytes += sizeof(MallocMetadata);
        }
        this->alloc_bytes += sizeof(MallocMetadata);
        low->size += high->size + sizeof(MallocMetadata);
        low->higher = high->higher;
        if (!is_free)
        {
            low->is_free = false;
            return low;
        }
        if (this->alloc_blocks == 1)
        {
            low->free_next = low->free_prev = nullptr;
            return low;
        }
        if (low->free_next == nullptr && low->free_prev == nullptr)
        {
            low->free_next = high->free_next;
            low->free_prev = high->free_prev;
            if (high->free_prev != nullptr)
            {
                (high->free_prev)->free_next = low;
            }
            if (high->free_next != nullptr)
            {
                (high->free_next)->free_prev = low;
            }
        }
        if (low->free_next == nullptr )
        {
            return low;
        }
        if (low->free_next->size < low->size || 
        (low->free_next->size == low->size && low->free_next->p < low->p))
        {
            (low->free_next)->free_prev = low->free_prev;
            if (low->free_prev != nullptr)
            {
                (low->free_prev)->free_next = low->free_next;
            }
            MallocMetadata* tmp = low->free_next;
            while(tmp->free_next != nullptr && tmp->size < low->size)
            {
                tmp = tmp->free_next;
            }
            if ((tmp->size > low->size) || (tmp->size == low->size && tmp->p < low->p))
            {
                tmp = tmp->free_prev;
            }
            //tmp ---> low ---> (tmp->free_next)
            if (tmp->free_next != nullptr)
            {
                tmp->free_next->free_prev = tmp;
            }
            low->free_next = tmp->free_next;
            tmp->free_next = low;
            low->free_prev = tmp;
        }
        return low;
    }
    MallocMetadata* unionWilderness(size_t size)
    {
        size_t new_space = size - this->wilderness->size;
        void* p = Sbrk(new_space);
        if (p == nullptr)
        {
            return nullptr;
        }
        this->wilderness->size = size;
        this->alloc_bytes += new_space;
        return this->wilderness;
    }
    void insertBigBlock (MallocMetadata* meta_data)
    {
        if (meta_data == nullptr)
        {
            return;
        }
        this->alloc_blocks ++;
        this->alloc_bytes += meta_data->size;
        MallocMetadata* curr = this->mmaped_list_head;
        MallocMetadata* prev = nullptr;
        while (curr != nullptr && curr->p < meta_data->p)
        {
            prev = curr;
            curr = curr->higher;
        }
        meta_data->higher = curr;
        meta_data->lower = prev;
        if (prev == nullptr)
        {
            this->mmaped_list_head = meta_data;
        }
        else
        {
            prev->higher = meta_data;
        }
        if (curr != nullptr)
        {
            curr->lower = meta_data;
        }
    }
    // lower and  higher = null
    // free_next and free_prev = null, already allocated , is_free = false
    void insertNewAllocatedBlock (MallocMetadata* meta_data) 
    {
        if (meta_data == nullptr)
        {
            return;
        }
        if (this->wilderness == nullptr)
        {
            this->wilderness = meta_data;
        }
        else
        {
            MallocMetadata* tmp = this->wilderness;
            this->wilderness->higher = meta_data;
            meta_data->lower = this->wilderness;
            //meta_data->free_prev = (this->wilderness->is_free) ? 
              //  this->wilderness : this->wilderness->free_prev; what that is not free do not have valid free pointers
            this->wilderness = meta_data;
        }
        this->alloc_blocks ++;
        this->alloc_bytes += meta_data->size;
    }
    MallocMetadata* reallocateBigBlock (MallocMetadata* md, size_t size)
    {
        if (md == nullptr)
        {
            return nullptr;
        }
        if (md->size == size)
        {
            return md;
        }
        MallocMetadata* new_md = this->allocateBigBlock(size);
        if (new_md != nullptr)
        {
            memmove(new_md->p, md->p, size);    
            //after success we free oldp
            this->freeBigBlock(md);
        }
        return new_md;
    }
    MallocMetadata* reallocateBlock (MallocMetadata* md, size_t size)
    {
        if (md == nullptr)
        {
            return nullptr;
        }
        if (md->size >= size)
        {
            return md;
        }
        size_t oldsize = md->size;
        void* oldp = md->p;
        MallocMetadata* merged = md;
        if (md->lower != nullptr && md->lower->is_free) //mrege with lower
        {
            merged = this->mergeAdjBlocks(md->lower, md, false);
            if (merged->size >= size)
            {
                return copyAndSplit(merged, size, oldp, oldsize);
            }
        }
        if (merged->higher != nullptr && merged->higher->is_free) //merge with higher
        {
            merged = this->mergeAdjBlocks(merged, merged->higher, false);
        }
        if (merged->size >= size)
        {
            return copyAndSplit (merged, size, oldp, oldsize);
        }
        if (merged->higher == nullptr) //enlarge wilderness
        {
            this->unionWilderness(size);
        }
        if (merged->size >= size)
        {
            return copyAndSplit(merged, size, oldp, oldsize);
        }
        else //find other block
        {
            MallocMetadata* new_md = this->findFreeBlock(size);
            memmove(new_md->p, oldp, oldsize);    
            //after success we free oldp
            this->freeBlock(merged->p);
            return new_md;
        }
    }
    MallocMetadata* copyAndSplit (MallocMetadata* md, size_t size, void* oldp, size_t oldsize)
    {
        if (oldp != md->p)
        {
            memmove(md->p, oldp, oldsize);
        }
        if (md->size > 128 + sizeof(MallocMetadata) + size)
        {
            return split(md, size);
        }
        return md;
    }
    
    MallocMetadata* allocateBigBlock(size_t size)
    {
        void* p = mmap(nullptr, size + sizeof(MallocMetadata), PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        if (p == (void*)(-1))
        {
            return nullptr;
        }
        MallocMetadata* new_md = (MallocMetadata*)p;
        new_md->size = size;
        new_md->p = (void*)(new_md + sizeof(MallocMetadata));
        this->updateNewBlock(new_md);
        this->insertBigBlock(new_md);
        return new_md;
    }

    MallocMetadata* findFreeBlock (size_t size)
    {
        MallocMetadata* tmp = this->free_list_head;
        while (tmp != nullptr && tmp->size < size )
        {
            tmp = tmp->free_next;
        }
        if (tmp == nullptr)
        {
            //if there is no other free block return (if free) wilderness that is smaller than size
            if (this->wilderness != nullptr && this->wilderness->is_free)
            {
                MallocMetadata* meta_ret = unionWilderness(size);
                if (meta_ret == nullptr)
                {//sbrk failed
                    return nullptr;
                }
                this->updateBusyBlock(this->wilderness); //updates free, free next & prev
                this->free_blocks --;
                this->free_bytes -= this->wilderness->size;
                return meta_ret;
            }
            else 
            {
                //allocate a new block if there is no free block available
                void* p = Sbrk(size + sizeof(MallocMetadata));
                if (p == nullptr)
                {
                    return nullptr;
                }
                MallocMetadata* meta_data = (MallocMetadata*)p;
                this->updateNewBlock(meta_data);
                meta_data->size = size;
                meta_data->p = (void*)(p + sizeof(MallocMetadata));
                this->insertNewAllocatedBlock(meta_data);
                return meta_data;
            }
        }
        else
        {
            //there is a block that is big enough
            this->updateBusyBlock(tmp); //updates free, free next & prev
            this->free_blocks --;
            this->free_bytes -= tmp->size;
            if (tmp->size >= 128 +  sizeof(MallocMetadata))
            {
                return split(tmp, size);
            }
            return tmp;
        }   
    }
    //get allocated block by pointer
    MallocMetadata* getBlock (void* p)
    {
        if (p == nullptr)
        {
            return nullptr;
        }
        MallocMetadata* md = (MallocMetadata*)(p - sizeof(MallocMetadata));
        return md;
    }

    void freeBigBlock(MallocMetadata* tmp)
    {
        if (tmp->lower != nullptr) 
        {
            tmp->lower->higher = tmp->higher;
        }
        if (tmp->higher != nullptr)
        {
            tmp->higher->lower = tmp->lower;
        }
        if (this->mmaped_list_head == tmp)
        {
            this->mmaped_list_head = nullptr;
        }
        this->alloc_bytes -= tmp->size;
        this->alloc_blocks --;
        munmap(tmp, sizeof(MallocMetadata) + tmp->size);
    }

    void freeBlock (void * p)
    {
        if (p == nullptr)
        {
            return ;
        }
        MallocMetadata* md = (MallocMetadata*)(p - sizeof(MallocMetadata)); 
        if (md->size >= 128*1024)
        {
            this->freeBigBlock(md);
            return;
        }
        md->is_free = true;
        this->free_blocks ++;
        this->free_bytes += md->size;
        bool is_merged = false;
        if (md->higher->is_free)
        {
            is_merged = true;
            this->mergeAdjBlocks(md, md->higher, true);
        }
        if (md->lower->is_free)
        {
            is_merged = true;
            this->mergeAdjBlocks(md->lower, md, true);
        }
        if (!is_merged)
        {
            this->insertFreeBlock(md);
        }
    }

    void insertFreeBlock(MallocMetadata* meta)
    {
        MallocMetadata* tmp = this->free_list_head;
        MallocMetadata* prev = nullptr;
        while (tmp != nullptr && tmp->size < meta->size)
        {
            prev = tmp;
            tmp = tmp->free_next;
        }
        while(tmp != nullptr && tmp->size == meta->size && tmp->p < meta->p)
        {
            prev = tmp;
            tmp = tmp->free_next;
        }
        meta->free_prev = prev;
        if (prev != nullptr)
        {
            prev->free_next = meta;
        }
        else
        {
            this->free_list_head = meta;
        }
        meta->free_next = tmp;
        if (tmp != nullptr)
        {
            tmp->free_prev = meta;
        }
    }
    size_t getAllocBytes()
    {
        return this->alloc_bytes;
    }
    size_t getFreeBytes ()
    {
        return this->free_bytes;
    }
    size_t getAllocBlocks()
    {
        return this->alloc_blocks;
    }
    size_t getFreeBlocks ()
    {
        return this->free_blocks;
    }
    
};


size_t _num_free_blocks()
{
  MallocList& m_list = MallocList::getInstance();
  return m_list.getFreeBlocks();
}

size_t _num_free_bytes()
{
    MallocList& m_list = MallocList::getInstance();
    return m_list.getFreeBytes();
}

size_t _num_allocated_blocks()
{
    MallocList& m_list = MallocList::getInstance();
    return m_list.getAllocBlocks();
}

size_t _num_allocated_bytes()
{
    MallocList& m_list = MallocList::getInstance();
    return m_list.getAllocBytes();
}

size_t _size_meta_data()
{
    //Returns the number of bytes of a single meta-data structure in your system.
    return sizeof(MallocMetadata);
}

size_t _num_meta_data_bytes()
{
    MallocList& m_list = MallocList::getInstance();
    return m_list.getAllocBlocks() * _size_meta_data();
}

size_t align (size_t& size)
{
    if (size % 8 == 0)
    {
        return size;
    }
    return 8*((size / 8) + 1);
}
void* allocateBlock(size_t size)
{
    size = align(size);
    if (size == 0 || size > 1e8 )
    {
        return nullptr;
    }
    MallocList& m_list = MallocList::getInstance();
    if (size >= 128*1024)
    {
        MallocMetadata* new_md = m_list.allocateBigBlock(size);
        return new_md;
    }
    void* result = m_list.findFreeBlock(size);
    return result;
}

void* smalloc(size_t size)
{
    void* result = allocateBlock(size);
    if (result == nullptr)
    {
        return nullptr;
    }
    MallocMetadata* meta_data = (MallocMetadata*)result;
    return meta_data->p;
}

void* scalloc(size_t num, size_t size)
{
    void* result = allocateBlock(size*num);
    if (result == nullptr)
    {
        return nullptr;
    }
    MallocMetadata* meta_data = (MallocMetadata*)result;
    memset(meta_data->p, 0, size);
    return meta_data->p;
}

void sfree(void* p)
{
    MallocList& m_list = MallocList::getInstance();
    m_list.freeBlock(p);
}

void* srealloc(void* oldp, size_t size)
{
    size = align(size);
    if (size == 0 || size > 1e8 )
    {
        return oldp;
    }
    MallocList& m_list = MallocList::getInstance();
    MallocMetadata* old_meta_data = m_list.getBlock(oldp);
    void* result = nullptr;
    if (size  >= 128*1024)
    {
        result = m_list.reallocateBigBlock(old_meta_data, size);
    }
    else
    {
        result = m_list.reallocateBlock(old_meta_data, size);
    }
    return result;
}

/*else
        {
            MallocMetadata* tmp = this->free_list_head;
            while (tmp->next != nullptr && tmp->size < meta_data->size)
            {
                tmp = tmp->next;
            }
            while (tmp->next != nullptr && tmp->size == meta_data->size && 
                    tmp->p < meta_data->p)
            {
                tmp = tmp->next;
            }
            if (tmp->p == meta_data->p)
            {//TODO:: shouldn't happen
                return;
            }
            tmp->next = meta_data;
            meta_data->prev = tmp;
            this->wilderness->higher = meta_data;
            meta_data->lower = this->wilderness;// TODO::
            // metadata free_prev = wilderness or wilderness->free_prev
            this->wilderness = meta_data;
        }*/