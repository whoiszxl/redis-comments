/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

typedef char *sds;


/**
 * sds结构体有五个，sdshdr5,sdshdr8,sdshdr16,sdshdr32,sdshdr64，分别用来存储不同长度的字符串。
 * 看每个结构体中的变量类型就可知：uint8_t，uint16_t，uint32_t，uint64_t
 * 
*/
/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */

/** 
 * sdshdr5，is never used，从未被使用，但是实际上还是有用过，当Redis的key小于32个字节时，便会使用sdshdr5，因为key是不变的，不会扩容，
 * 所以为了节省内存，key就可以采用sdshdr5来做，其中省去了len和alloc字段。 
 * 
 * 其使用flags字段的高五位存储字符串的长度，低三位存储字符串的类型。
 */
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};

/** 
 * sdshdr8，len、alloc都是uint8_t类型，最大长度为 2^8 -1
 * 
 * Redis采用SDS而不采用字符数组的原因
 * 1. 二进制安全：C语言中字符串以'\0'结尾，如果我们要存储'\0'这个符号的话，就会导致length计算出错。
 * 
 * 2. 提升性能：C语言字符串是一个字符数组，每次获取length都需要遍历数组一遍，复杂度为O(N)，CPU性能消耗大，SDS存了len变量，取出长度的复杂度为O(1)
 * 
 * 3. 字符串扩缩容：C语言字符串的长度需要在创建时确定，追加字符串的时候需要重新申请一个更大的内存空间再将旧的数据迁移
 * 到新的新的数组中，SDS则会预先申请一部分内存空间，此时追加字符串时便不需扩容。
 * 
 * 结构体中的__attribute__ ((__packed__))的作用：是GCC编译器的扩展语法，用于告诉编译器取消结构体的对齐优化，保证此结构体节省空间，并且可以通过指针前后移动获取到对应字段。
 * 结构体中的成员将按照其定义的顺序紧密地排列在一起，没有任何填充字节。这样可以确保结构体的大小与成员变量的总大小一致，但也可能导致访问效率的降低。
 * 
 * 内存对齐填充是因为有些平台读取内存都是从4字节的位置开始读，每次读取四字节，假如说在一段八字节的内存段上，有一个1字节的c和一个2字节的s的话
 * 
 * 未加__attribute__ ((__packed__))的情况
 * | c|  |  s  |
 * |__|__|__|__|__|__|__|__|   c占用一个字节，并且会填充一个字节，s则直接占用两个字节，这样一次读取四个字节的时候就能正确读出两个变量
 * 
 * 加了__attribute__ ((__packed__))的情况
 * | c|  s  |
 * |__|__|__|__|__|__|__|__|   c占用一个字节，不会填充一个字节，s则直接占用两个字节，这样我们在读取sdshdr实例的时候，就可以按照偏移量来获取每个字段
 */
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* buf数组目前的长度 */
    uint8_t alloc; /* buf数组目前已分配的空间长度 */

    /**
     * sds的类型，char有8bit，低3位表示类型，此类型存储了SDS_TYPE_5|SDS_TYPE_8|SDS_TYPE_16|SDS_TYPE_32|SDS_TYPE_64对应的值
     * 
     * 结构体已经标识了类型，此处还有flags标识类型，是为了方便[typedef char *sds;] 这个sds指针获取类型，Redis的sds实际上就是一个
     * 指向buf[]的的指针，如果要获取类型的话，只需要在sds指针上往前取一字节便可获取到flags类型。也就是此种操作：sds[-1]，此操作可见 sdslen 方法。
     * 此处要实现这个效果必须加__attribute__ ((__packed__))避免内存对齐导致填充空白字段，不然sds[-1]往前获取一字节的操作就失效了。
     * 
    */
    unsigned char flags; 
    char buf[]; /* SDS实际的值，字符数组 */
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3

/**
 * 传入T，假设为8，那么 ##T 需要替换为8，则语句如下：
 * 
 * struct sdshdr8 *sh = (void*)((s)-(sizeof(struct sdshdr8)));
 * 
 * 当 s 传入，s 为 sdshdr8 中 buf 的指针，此处让 s 减去 sdshdr8 的大小，则是让 s 指针往前移动 size 位，则会移动到 sdshdr8 的首位上。

 * 
*/
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

static inline size_t sdslen(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

/**
 * 获取 sds 的可用空间
*/
static inline size_t sdsavail(const sds s) {
    /** 通过-1获取到 sds 的类型 */
    unsigned char flags = s[-1];

    switch(flags&SDS_TYPE_MASK) {

        /** 如果是 sdshdr5，直接返回0，进行扩容 */
        case SDS_TYPE_5: {
            return 0;
        }

        /** 如果是 sdshdr8，返回 alloc - len，将已分配的内存大小减去当前已使用的内存大小就得到剩余的空间大小 */
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);
sds sdstrynewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);
sds sdsdup(const sds s);
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdssubstr(sds s, size_t start, size_t len);
void sdsrange(sds s, ssize_t start, ssize_t end);
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);
int sdsneedsrepr(const sds s);

/* Callback for sdstemplate. The function gets called by sdstemplate
 * every time a variable needs to be expanded. The variable name is
 * provided as variable, and the callback is expected to return a
 * substitution value. Returning a NULL indicates an error.
 */
typedef sds (*sdstemplate_callback_t)(const sds variable, void *arg);
sds sdstemplate(const char *template, sdstemplate_callback_t cb_func, void *cb_arg);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
sds sdsMakeRoomForNonGreedy(sds s, size_t addlen);
void sdsIncrLen(sds s, ssize_t incr);
sds sdsRemoveFreeSpace(sds s, int would_regrow);
sds sdsResize(sds s, size_t size, int would_regrow);
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[], int flags);
#endif

#endif
