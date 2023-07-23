/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DICT_H
#define __DICT_H

#include "mt19937-64.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define DICT_OK 0
#define DICT_ERR 1

typedef struct dictEntry {
    /** kv 结构中的 key 指针，指向一个 sds 实例 */
    void *key;

    /** kv 结构总的 vaue ，union 结构，表示可以存储下列数据结构的任意一种，类似 Java 中的 Object，但是这个 union 限制了对象存储的范围 */
    union {
        void *val; /** 非数字类型时使用此类型 */
        uint64_t u64; /** 无符号整数使用此类型 */
        int64_t s64; /** 有符号整数使用此类型 */
        double d; /** 浮点数时使用此类型 */
    } v;

    /** 链表指针，当有冲突时使用此 next 指针指向下一个 dictEntry */
    struct dictEntry *next;     /* Next entry in the same hash bucket. */

    /** 存储额外的元数据 */
    void *metadata[];           /* An arbitrary number of bytes (starting at a
                                 * pointer-aligned address) of size as returned
                                 * by dictType's dictEntryMetadataBytes(). */
} dictEntry;

typedef struct dict dict;

/**
 * 定义了一系列函数指针，用于处理字典中键和值的操作。这个结构体里的函数指针就像Java中的接口，需要被其他类做实现。
 * 
 * VS Code 中，按住 Ctrl + 鼠标左键，然后再选中 server.c 文件便可以看到所有的实现类。
 * Clion 也是 按住 Ctrl + 鼠标左键，然后弹出所有的实现类来，通过 server.c 的前缀进行筛选。
*/
typedef struct dictType {

    /** 用于计算给定 key 的哈希值，以便将 key 分布到哈希表的不同桶中 */
    uint64_t (*hashFunction)(const void *key);

    /** 
     * 用于复制给定 key 的副本  (Duplicate) 
     * 
     * 因为 key 指针被其他的位置的指针修改了之后也会变化，若要独立创建一个 key 不受到其他指针影响，则需要独立拷贝一个 key 出来。（valDup 同理）
     * 
     * */
    void *(*keyDup)(dict *d, const void *key);

    /** 用于复制给定 key 的副本  (Duplicate) */
    void *(*valDup)(dict *d, const void *obj);

    /** 用于比较 key1 和 key2 是否相等 */
    int (*keyCompare)(dict *d, const void *key1, const void *key2);
    
    /** 用于释放 key 占用的内存，当 key 被从字典中删除或字典被销毁时调用 */
    void (*keyDestructor)(dict *d, void *key);

    /** 用于释放 key 占用的内存，当 key 被从字典中删除或字典被销毁时调用。 */
    void (*valDestructor)(dict *d, void *obj);

    /** 用于检查是否允许字典扩容，并返回一个整数表示是否允许扩容 */
    int (*expandAllowed)(size_t moreMem, double usedRatio);

    /* Allow a dictEntry to carry extra caller-defined metadata.  The
     * extra memory is initialized to 0 when a dictEntry is allocated. */
    /** 用于返回额外的、由调用者定义的元数据字节数，每当创建一个新的字典条目时，这些额外的内存会被初始化为0。 */
    size_t (*dictEntryMetadataBytes)(dict *d);
} dictType;

#define DICTHT_SIZE(exp) ((exp) == -1 ? 0 : (unsigned long)1<<(exp))
#define DICTHT_SIZE_MASK(exp) ((exp) == -1 ? 0 : (DICTHT_SIZE(exp))-1)

struct dict {
    /** 特殊函数字典，字典中定义了对键与值的处理方式，比如：hash函数，对比函数等 */
    dictType *type;

    /** 
     * 实际存储kv键值对的地方，此处为一个数组，有两个元素，每个元素都是一个二级指针数组
     * 第一个数组实际存储 kv 键值对
     * 第二个数组则是用来实现渐进式 rehash 的
     * */
    dictEntry **ht_table[2];

    /** 存储每个hash数组中存储了多少个 kv 键值对 */
    unsigned long ht_used[2];

    /** 目前正在进行 rehash 操作的哈希表的索引 */
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */

    /* Keep small vars at end for optimal (minimal) struct padding */
    /** 
     * 用于暂停 rehash 操作的标志。如果值大于 0，表示 rehashing 被暂停了，小于 0 则表示出现了编码错误。 
     * 
     * 如果有迭代器正在遍历 hash 表，则会对 pauserehash 做加一的操作，遍历完后做减一操作。
     **/
    int16_t pauserehash; /* If >0 rehashing is paused (<0 indicates coding error) */

    /** 用于记录每个哈希表的大小指数。哈希表的大小是 2 的指数次方，该数组记录每个哈希表的大小指数。哈希表的大小指数组的大小。 */
    signed char ht_size_exp[2]; /* exponent of size. (size = 1<<exp) */
};

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
typedef struct dictIterator {
    dict *d;
    long index;
    int table, safe;
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    unsigned long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
typedef void (dictScanBucketFunction)(dict *d, dictEntry **bucketref);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_EXP      2
#define DICT_HT_INITIAL_SIZE     (1<<(DICT_HT_INITIAL_EXP))

/* ------------------------------- Macros ------------------------------------*/
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d), (entry)->v.val)

/**
 * 先判断字典中是否存在 value 的拷贝函数，如果存在的话，则把 value 拷贝一份再保存到 entry 的 value 中。
 * 如果不存在拷贝函数，则直接将 value 的指针保存进去。
*/
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d), _val_); \
    else \
        (entry)->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { (entry)->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { (entry)->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { (entry)->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d), (entry)->key)

/**
 * 先判断字典中是否存在 key 的拷贝函数，如果存在的话，则把 key 拷贝一份再保存到 entry 的 key 中。
 * 如果不存在拷贝函数，则直接将 key 的指针保存进去。
*/
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d), _key_); \
    else \
        (entry)->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d), key1, key2) : \
        (key1) == (key2))

#define dictMetadata(entry) (&(entry)->metadata)
#define dictMetadataSize(d) ((d)->type->dictEntryMetadataBytes \
                             ? (d)->type->dictEntryMetadataBytes(d) : 0)

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) (DICTHT_SIZE((d)->ht_size_exp[0])+DICTHT_SIZE((d)->ht_size_exp[1]))
#define dictSize(d) ((d)->ht_used[0]+(d)->ht_used[1])
#define dictIsRehashing(d) ((d)->rehashidx != -1)
#define dictPauseRehashing(d) (d)->pauserehash++
#define dictResumeRehashing(d) (d)->pauserehash--

/* If our unsigned long type can store a 64 bit number, use a 64 bit PRNG. */
#if ULONG_MAX >= 0xffffffffffffffff
#define randomULong() ((unsigned long) genrand64_int64())
#else
#define randomULong() random()
#endif

typedef enum {
    DICT_RESIZE_ENABLE,
    DICT_RESIZE_AVOID,
    DICT_RESIZE_FORBID,
} dictResizeEnable;

/* API */
dict *dictCreate(dictType *type);
int dictExpand(dict *d, unsigned long size);
int dictTryExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);
dictEntry *dictAddOrFind(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
int dictDelete(dict *d, const void *key);
dictEntry *dictUnlink(dict *d, const void *key);
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);
void dictRelease(dict *d);
dictEntry * dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
dictEntry *dictGetFairRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictGetStats(char *buf, size_t bufsize, dict *d);
uint64_t dictGenHashFunction(const void *key, size_t len);
uint64_t dictGenCaseHashFunction(const unsigned char *buf, size_t len);
void dictEmpty(dict *d, void(callback)(dict*));
void dictSetResizeEnabled(dictResizeEnable enable);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(uint8_t *seed);
uint8_t *dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata);
uint64_t dictGetHash(dict *d, const void *key);
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);

#ifdef REDIS_TEST
int dictTest(int argc, char *argv[], int flags);
#endif

#endif /* __DICT_H */
