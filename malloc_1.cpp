#include <unistd.h>
#include <iostream>
using namespace std;
void* smalloc(size_t size)
{
    if (size == 0 || size > 1e8 )
    {
        return nullptr;
    }
    void * result = sbrk(size);
    if (result == (void*)(-1))
    {
        return nullptr;
    }
    return result;
}


int main()
{
    cout<<"hello"<<endl;
    void * result = smalloc(0);
    if (result == nullptr)
    {
        cout<<"zero - null"<<endl;
    }
    void * before = sbrk(0);
     result = smalloc(2);
    void * after = sbrk(0);
    cout<<"diff is "<<after<<"and befor -"<<before<<endl;
}