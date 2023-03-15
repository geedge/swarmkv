#include "hash_fun.h"
#include "string.h"

static unsigned int BKDRHash(char *str)
{
    unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
    unsigned int hash = 0;
    while (*str)
    {
        hash = hash * seed + (*str++);
    }
    return (hash & 0x7FFFFFFF);
}

static unsigned int SDBMHash(char *str)
{
    unsigned int hash = 0;

    while (*str)
    {
        // equivalent to: hash = 65599*hash + (*str++);
        hash = (*str++) + (hash << 6) + (hash << 16) - hash;
    }

    return (hash & 0x7FFFFFFF);
}

static unsigned int RSHash(char *str)
{
    unsigned int b = 378551;
    unsigned int a = 63689;
    unsigned int hash = 0;

    while (*str)
    {
        hash = hash * a + (*str++);
        a *= b;
    }

    return (hash & 0x7FFFFFFF);
}

static unsigned int JSHash(char *str)
{
    unsigned int hash = 1315423911;

    while (*str)
    {
        hash ^= ((hash << 5) + (*str++) + (hash >> 2));
    }

    return (hash & 0x7FFFFFFF);
}

 static int FNVHash(char *str) {
    int fnvprime = 0x811C9DC5;
    int hash = 0;
    int len = (int)strlen(str);
    for (int i = 0; i < len; i++) {
        hash *= fnvprime;
        hash ^= (int)str[i];
    }

    return hash & 0x7FFFFFFF;
}

static unsigned int DJBHash(char *str)
{
    unsigned int hash = 5381;

    while (*str)
    {
        hash += (hash << 5) + (*str++);
    }

    return (hash & 0x7FFFFFFF);
}

static unsigned int APHash(char *str)
{
    unsigned int hash = 0;
    int i;

    for (i=0; *str; i++)
    {
        if ((i & 1) == 0)
        {
            hash ^= ((hash << 7) ^ (*str++) ^ (hash >> 3));
        }
        else
        {
            hash ^= (~((hash << 11) ^ (*str++) ^ (hash >> 5)));
        }
    }

    return (hash & 0x7FFFFFFF);
}
static int DEKHash(char *str) {
    int hash = (int)strlen(str);
    int len = (int)strlen(str);
    for (int i = 0; i < len; i++) {
        hash = (hash << 5) ^ (hash >> 27) ^ (int)str[i];
    }

    return hash & 0x7FFFFFFF;
}
unsigned int custom_get_hash(int select, char *str)
{
    unsigned int hash = 0;
    switch (select) {
        case 0:
            hash = SDBMHash(str);
            break;
        case 1:
            hash = FNVHash(str);
            break;
        case 2:
            hash = RSHash(str);
            break;
        case 3:
            hash = JSHash(str);
            break;
        case 4:
            hash = BKDRHash(str);
            break;
        case 5:
            hash = DJBHash(str);
            break;
        case 6:
            hash = DEKHash(str);
            break;
        case 7:
            hash = APHash(str);
            break;
        default:
            hash = 0;
    }
    return hash;
}