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
        old_md->size = old_md->size - size - _size_meta_data();
        MallocMetadata* new_md = (MallocMetadata*)(old_md->p + old_md->size);
        new_md->size = size;
        new_md->is_free = false;
        new_md->lower = old_md;
        new_md->higher = old_md->higher;
        new_md->p = old_md->p + old_md->size + _size_meta_data();
        // next and prev??
        old_md->higher = new_md;
        old_md->is_free = true;
        this->free_blocks++;
        this->free_bytes += old_md->size;
        
        if (old_md->higher != nullptr)
        {
            old_md->higher->lower = new_md;
        }
        return new_md;
    }
    void uniteFreeBlocksBlocks (MallocMetadata* low, MallocMetadata* high)
    {
        this->free_blocks--;
        this->free_bytes += sizeof(MallocMetadata);
        low->size += high->size + sizeof(MallocMetadata);
        low->higher = high->higher;
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
        this->alloc_bytes+= new_space;
        return this->wilderness;
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
    void* findFreeBlock (size_t size)
    {
        if (this->alloc_blocks == 0 || this->free_blocks == 0)
        {
            return nullptr;
        }
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
                return nullptr;
            }
        }
        else
        {
            //there is a block that is big enough
            this->updateBusyBlock(tmp); //updates free, free next & prev
            this->free_blocks --;
            this->free_bytes -= tmp->size;
            if (tmp->size >= 128 +  _size_meta_data())
            {
                return split(tmp, size);
            }
            return (void*)tmp;
        }   
    }
    //get allocated block by pointer
    MallocMetadata* getBlock (void* p)
    {
        if (p == nullptr)
        {
            return nullptr;
        }
        MallocMetadata* tmp = this->wilderness;
        while (tmp != nullptr && tmp->p != p)
        {
            tmp = tmp->lower;
        }
        if (tmp == nullptr || tmp->is_free)
        {
            return nullptr;
        }
        return tmp;
    }
    void freeBigBlock(MallocMetadata* tmp)
    {
        if (tmp->lower != nullptr) 
        {
            tmp->lower->higher = tmp->higher;
        }
        if (tmp->upper != nullptr)
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
        MallocMetadata* tmp = this->wilderness;
        while (tmp != nullptr && tmp->p != p)
        {
            tmp = tmp->lower;
        }
        if (tmp == nullptr)
        {
            //TODO:: shouldnt happen
            return ;
        }
        if (tmp->size >= 128*1024)
        {
            this->freeBigBlock(tmp);
            return;
        }
        tmp->is_free = true;
        this->free_blocks ++;
        this->free_bytes += tmp->size;
        if (tmp->upper->is_free)
        {
            uniteFreeBlocks(tmp, tmp->upper);
        }
        if (tmp->lower->is_free)
        {
            uniteFreeBlocks(tmp->lower, tmp);
        }
        this->insertFreeBlock(tmp);
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
    size_t bigger = 8*((size / 8) + 1);
    size_t smaller = 8*(size / 8);
    return (size - smaller < bigger - size ) ? smaller : bigger; 
}

void* allocateBlock(size_t size)
{
    if (size == 0 || size > 1e8 )
    {
        return nullptr;
    }
    size = align(size);
    if (size >= 128*1024)
    {

    }
    MallocList& m_list = MallocList::getInstance();
    void* result = m_list.findFreeBlock(size);
    if (result != nullptr)
    {
        return result;
    }
    result = Sbrk(size + sizeof(MallocMetadata));
    if (result == nullptr)
    {
        return nullptr;
    }
    MallocMetadata* meta_data = (MallocMetadata*)result;
    m_list.updateNewBlock(meta_data);
    meta_data->size = size;
    meta_data->p = (void*)(result + sizeof(MallocMetadata));
    m_list.insertNewAllocatedBlock(meta_data);
    return meta_data;
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
    memset(meta_data->p, 0, meta_data->size);
    return meta_data->p;
}

void sfree(void* p)
{
    MallocList& m_list = MallocList::getInstance();
    m_list.freeBlock(p);
}

void* srealloc(void* oldp, size_t size)
{
    MallocList& m_list = MallocList::getInstance();
    MallocMetadata* old_meta_data = m_list.getBlock(oldp);
    if (old_meta_data != nullptr && old_meta_data->size >= size)
    {
        return oldp;
    }
    void* result = allocateBlock(size);
    if (result == nullptr)
    {
        return nullptr;
    }
    MallocMetadata* meta_data = (MallocMetadata*)result;
    if (old_meta_data != nullptr)
    {
        memmove(meta_data->p, oldp, old_meta_data->size);    
        //after success we free oldp
        m_list.freeBlock(old_meta_data->p);
    }
    return meta_data->p;
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