/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "ae.h"
#include "anet.h"
#include "redisassert.h"

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "zmalloc.h"
#include "config.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. 
 * 程序在编译的时候自动选择系统中性能最高的I/O多路复用函数库来作为Redis底层多路复用的实现
 */
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        #include "ae_kqueue.c"
        #else
        #include "ae_select.c"
        #endif
    #endif
#endif


aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

    monotonicInit();    /* just in case the calling app didn't initialize */

    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    eventLoop->setsize = setsize;
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;
    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
    eventLoop->aftersleep = NULL;
    eventLoop->flags = 0;
    if (aeApiCreate(eventLoop) == -1) goto err;
    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return eventLoop;

err:
    if (eventLoop) {
        zfree(eventLoop->events);
        zfree(eventLoop->fired);
        zfree(eventLoop);
    }
    return NULL;
}

/* Return the current set size. */
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/* Tells the next iteration/s of the event processing to set timeout of 0. */
void aeSetDontWait(aeEventLoop *eventLoop, int noWait) {
    if (noWait)
        eventLoop->flags |= AE_DONT_WAIT;
    else
        eventLoop->flags &= ~AE_DONT_WAIT;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

    if (setsize == eventLoop->setsize) return AE_OK;
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return AE_OK;
}

void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    zfree(eventLoop->events);
    zfree(eventLoop->fired);

    /* Free the time events list. */
    aeTimeEvent *next_te, *te = eventLoop->timeEventHead;
    while (te) {
        next_te = te->next;
        zfree(te);
        te = next_te;
    }
    zfree(eventLoop);
}

void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

/**
 * 向事件循环 eventLoop 中添加一个文件事件
 * fd：事件的描述符
 * mask: 要监听的事件类型，可以是 AE_READABLE 或 AE_WRITABLE，也可以是两者的位或操作。
 * aeFileProc *proc: 一个函数指针，指向处理事件的回调函数。当指定的事件发生时，事件循环将调用这个回调函数来处理事件。比如说 acceptTcpHandler，在有新的redis-cli建立连接的时候便会回调此函数。
 * clientData: 可选的客户端数据，在回调函数中使用。
*/
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{

    /** 判断当前需要注册的 fd 是否超出了事件循环的最大范围，超出则报错 */
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }

    /** 从事件循环中获取到对应的文件事件结构实例 */
    aeFileEvent *fe = &eventLoop->events[fd];

    /** 这一步将文件描述符添加到事件监听机制中，epoll中直接调用 epoll_ctl */
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;

    /** 将 mask 更新进文件事件结构实例中，表示此文件描述符对传入的事件是感兴趣的 */
    fe->mask |= mask;

    /** 接着判断是否是可读事件，若是则更新可读事件的回调函数为传入的 proc */
    if (mask & AE_READABLE) fe->rfileProc = proc;

    /** 接着判断是否是可写事件，若是则更新可写事件的回调函数为传入的 proc */
    if (mask & AE_WRITABLE) fe->wfileProc = proc;

    /** 接着把 clientData 封装进去，用于在回调函数中传递客户端发送过来的数据 */
    fe->clientData = clientData;

    /** 接着判断目前的 fd 是否大于 maxfd（当前已注册的最高文件描述符的值），如果大于的话，则更新此 maxfd */
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;
    return AE_OK;
}

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;
    aeFileEvent *fe = &eventLoop->events[fd];
    if (fe->mask == AE_NONE) return;

    /* We want to always remove AE_BARRIER if set when AE_WRITABLE
     * is removed. */
    if (mask & AE_WRITABLE) mask |= AE_BARRIER;

    aeApiDelEvent(eventLoop, fd, mask);
    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }
}

void *aeGetFileClientData(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return NULL;
    aeFileEvent *fe = &eventLoop->events[fd];
    if (fe->mask == AE_NONE) return NULL;

    return fe->clientData;
}

int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/**
 * 在事件循环中创建一个时间事件
 * 
 * 
*/
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{   
    /** 为时间事件生成的 id 字段，用来唯一标识时间事件 */
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

    /** 给时间事件结构体分配内存，并判断是否分配成功 */
    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    
    /** 将唯一 ID 设置进去 */
    te->id = id;

    /** 设置时间事件的触发时间，当前时间 us 加上 milliseconds 乘以 1000，假如传入的是 1，则触发时间是当前时间后一毫秒 */
    te->when = getMonotonicUs() + milliseconds * 1000;

    /** 设置时间事件的触发函数，server.c 中 initServer() 函数中传入的是 serverCron 函数 */
    te->timeProc = proc;

    /** 设置最终执行的函数，用来处理时间事件被删除时的清理工作 */
    te->finalizerProc = finalizerProc;

    /** 回调函数中使用的客户端数据 */
    te->clientData = clientData;

    /** 初始化前驱节点为 NULL */
    te->prev = NULL;

    /** 初始化后继节点为原始的链表头结点 timeEventHead */
    te->next = eventLoop->timeEventHead;

    /** 初始化引用计数为 0 */
    te->refcount = 0;

    /** 如果后继节点存在的话，将后继结点的 prev 指针指向当前节点，链表头插的基操 */
    if (te->next)
        te->next->prev = te;

    /** 接着将当前节点加到 timeEventHead 链表的头结点中 */
    eventLoop->timeEventHead = te;

    /** 返回时间事件的ID */
    return id;
}

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te = eventLoop->timeEventHead;
    while(te) {
        if (te->id == id) {
            te->id = AE_DELETED_EVENT_ID;
            return AE_OK;
        }
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */
}

/* How many microseconds until the first timer should fire.
 * If there are no timers, -1 is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static int64_t usUntilEarliestTimer(aeEventLoop *eventLoop) {
    aeTimeEvent *te = eventLoop->timeEventHead;
    if (te == NULL) return -1;

    aeTimeEvent *earliest = NULL;
    while (te) {
        if (!earliest || te->when < earliest->when)
            earliest = te;
        te = te->next;
    }

    monotime now = getMonotonicUs();
    return (now >= earliest->when) ? 0 : earliest->when - now;
}

/* 处理时间事件 Process time events */
static int processTimeEvents(aeEventLoop *eventLoop) {
    /** 记录已处理的时间事件数量 */
    int processed = 0;
    /** 时间事件结构体  */
    aeTimeEvent *te;
    /** 最大时间事件的 ID */
    long long maxId;

    /** 从事件循环处理器中获取到需要处理的时间事件链表的头结点 */
    te = eventLoop->timeEventHead;
    /** 从事件循环处理器中获取到下一个时间事件的 ID 再减一，便是当前时间事件的最大ID */
    maxId = eventLoop->timeEventNextId-1;
    /** 获取到当前的系统事件 */
    monotime now = getMonotonicUs();

    /** 开始循环遍历时间事件链表，逐个处理时间事件 */
    while(te) {
        long long id;

        /* Remove events scheduled for deletion. */
        /** 
         * 当前 if 分支的逻辑概述：清理已被标记为删除的时间事件节点，确保这些节点不再参与后续的处理，并释放相应的资源。
         * 判断如果当前遍历的时间事件是被删除的 （id = -1 为删除状态） 
         * */
        if (te->id == AE_DELETED_EVENT_ID) {
            /** 直接拿到当前被删除节点的下一个节点 */
            aeTimeEvent *next = te->next;
            /* If a reference exists for this timer event,
             * don't free it. This is currently incremented
             * for recursive timerProc calls */
            /** 判断如果当前被删除的节点是否还被引用，还有引用的话，说明此节点存在递归调用等情况，就先暂时忽略此节点 */
            if (te->refcount) {
                te = next;
                continue;
            }
            /** 如果节点没有被引用，则执行删除操作。先判断这个节点是否存在前驱节点 */
            if (te->prev)
                /** 存在的话，直接将前驱节点的 next 指针指向当前要被删除节点的后继结点，此便是删除当前节点 */
                te->prev->next = te->next;
            else
                /** 如果不存在前驱节点，则说明此节点是链表的头结点，将 timeEventHead 指向当前节点的后继节点便是删除当前节点 */
                eventLoop->timeEventHead = te->next;

            /** 如果当前节点存在后继节点，则直接将后继节点的 prev 指针指向当前节点的前驱节点，此便是删除当前节点。
             * 链表的删除元素的操作，参考：https://visualgo.net/zh/list 中双向链表的可视化演示 */
            if (te->next)
                te->next->prev = te->prev;

            /** 判断当前节点是否存在 finalizerProc 函数，如果存在则执行此函数进行清理操作，例如释放事件所使用的资源等  */
            if (te->finalizerProc) {
                te->finalizerProc(eventLoop, te->clientData);
                now = getMonotonicUs();
            }
            /** 释放当前节点的内存，并将指针 te 指向 next 下一个节点，接着 while 循环中处理下一个时间事件节点 */
            zfree(te);
            te = next;
            continue;
        }

        /* Make sure we don't process time events created by time events in
         * this iteration. Note that this check is currently useless: we always
         * add new timers on the head, however if we change the implementation
         * detail, this check may be useful again: we keep it here for future
         * defense. */
        /**
         * 如果当前正在处理的事件ID大于当前的时间事件最大ID，则跳过当前事件
         * 
         * 分析：时间事件的 ID 是一个自增数值，表示节点的创建顺序。maxId 则是时间事件链表里最大的 ID，用来标记当前迭代过程里最后一个时间事件的ID。
         * 如果在后续有时间事件添加过来了，这个时间事件的 ID 则大于 maxId，说明此时间事件是在 while 迭代的过程中**新添加**进来的。此时这个时间事件
         * 我们不应该立即处理，因为处理的过程中会修改链表结构，可能导致迭代发生问题。这一步操作就保证了当前迭代就只处理当前旧的时间事件，新加进来的
         * 时间事件则在下一次迭代中处理。
         * 
         * 简而言之，就是每一次执行就只处理自己这一批的数据，不要去管后面新加进来的，新加进来的下一次执行时再处理。
        */
        if (te->id > maxId) {
            te = te->next;
            continue;
        }

        /** 判断当前是否到达了时间事件的任务执行时间，就是 now 当前时间大于等于任务的发生时间 */
        if (te->when <= now) {
            int retval;

            /** 获取到当前时间事件的 id */
            id = te->id;
            
            /** 事件的引用数 +1 */
            te->refcount++;

            /**【核心 - 实际执行时间事件的地方】 然后执行时间事件的 timeProc 函数 */
            retval = te->timeProc(eventLoop, id, te->clientData);
            /** 将时间事件的引用数减一 */
            te->refcount--;
            /** 处理的事件数量加一 */
            processed++;

            /** 重置当前时间，确保后续的时间事件触发时间是基于最新的时间 */
            now = getMonotonicUs();
            
            /** 时间事件返回的状态不为 no more 的话，说明还有事件需要执行，则更新 when 参数设置重新触发时间 */
            if (retval != AE_NOMORE) {
                te->when = now + retval * 1000;
            } else {
                /** 此处说明没有更多事件执行了，需要将事件标记为删除状态 */
                te->id = AE_DELETED_EVENT_ID;
            }
        }

        /** 链表迭代下一个时间事件节点 */
        te = te->next;
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set, the function returns ASAP once all
 * the events that can be handled without a wait are processed.
 * if flags has AE_CALL_AFTER_SLEEP set, the aftersleep callback is called.
 * if flags has AE_CALL_BEFORE_SLEEP set, the beforesleep callback is called.
 *
 * The function returns the number of events processed. */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    /** processed 记录处理了多少个事件，numevents 则是当前阻塞获取了多少事件  */
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    /** 判断当前需要处理的事件如果不处于 时间事件 或者 文件事件 范围内的话，则中断执行 */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want to call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    
    /** 
     * eventLoop->maxfd 是当前已注册的最高文件描述符的值，不为 -1 则说明有事件需要进行 IO多路复用 处理 
     * 
     * ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT)) 则判断是否启用了时间事件，并且没有设置 AE_DONT_WAIT 的标记
     * 
     * AE_DONT_WAIT 表示处理事件时不需要进行阻塞等待，此处需要阻塞等待，则做取反计算
     * */
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        struct timeval tv, *tvp;
        int64_t usUntilTimer = -1;

        /** 启用了时间事件，并且没有设置 AE_DONT_WAIT 的标记的话，则获取到之后一次事件的处理时间间隔，单位为微秒 */
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            usUntilTimer = usUntilEarliestTimer(eventLoop);

        /** 如果计算出了时间间隔，表示在阻塞等待的模式下是有任务要执行的，其中将时间转化成秒数和微秒数 */
        if (usUntilTimer >= 0) {
            tv.tv_sec = usUntilTimer / 1000000;
            tv.tv_usec = usUntilTimer % 1000000;
            tvp = &tv;
        } else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            /** 如果是非阻塞模式，则直接将间隔时间置为0，无需等待 */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                /** 没有 AE_DONT_WAIT 的话，时间直接置为 null，无限等待，直到有事件发生 */
                tvp = NULL; /* wait forever */
            }
        }

        /** 如果 事件循环处理器 里设置了无需等待，则一样逻辑将间隔时间置为 0 */
        if (eventLoop->flags & AE_DONT_WAIT) {
            tv.tv_sec = tv.tv_usec = 0;
            tvp = &tv;
        }

        /** 如果前置处理函数存在，并且开启了前置处理的配置，则调用前置处理函数 */
        if (eventLoop->beforesleep != NULL && flags & AE_CALL_BEFORE_SLEEP)
            eventLoop->beforesleep(eventLoop);

        /* Call the multiplexing API, will return only on timeout or when
         * some event fires. */
        /**
         * 【核心逻辑】：调用多路复用的api函数进行事件监听，此函数会阻塞，直到超时了或者有就绪事件触发才会返回
         * 
         *  Linux下，底层便是封装了 epoll 的 epoll_wait 逻辑
        */
        numevents = aeApiPoll(eventLoop, tvp);

        /* After sleep callback. */
        /** 接着判断是否存在后置处理函数，存在并且开启了后置处理的配置，则调用后置处理函数 */
        if (eventLoop->aftersleep != NULL && flags & AE_CALL_AFTER_SLEEP)
            eventLoop->aftersleep(eventLoop);

        /** 接着再来遍历所有的就绪事件 */
        for (j = 0; j < numevents; j++) {
            /** 拿到每个事件的 fd 文件描述符 */
            int fd = eventLoop->fired[j].fd;

            /** 从多路复用的事件中获取到指定的事件，通过 fd 获取 */
            aeFileEvent *fe = &eventLoop->events[fd];

            /** 再从就绪事件中获取到对应的 mask */
            int mask = eventLoop->fired[j].mask;
            int fired = 0; /* Number of events fired for current fd. */

            /* Normally we execute the readable event first, and the writable
             * event later. This is useful as sometimes we may be able
             * to serve the reply of a query immediately after processing the
             * query.
             *
             * However if AE_BARRIER is set in the mask, our application is
             * asking us to do the reverse: never fire the writable event
             * after the readable. In such a case, we invert the calls.
             * This is useful when, for instance, we want to do things
             * in the beforeSleep() hook, like fsyncing a file to disk,
             * before replying to a client. */

            /**
             * 根据事件循环中定义的掩码 mask 中的 AE_BARRIER 标志位判断是否需要改变事件的执行顺序。
             * 
             * 如果 fe->mask 中包含 AE_BARRIER 标志，表示需要执行顺序的反转。在这种情况下，将 invert 变量设置为非零值，表示需要反转执行顺序。
             * 
             * 默认情况下，可读事件会先执行，可写事件会在可读事件之后执行。这样做是为了在处理查询后，能够立即响应查询的回复。但是当应用程序设置
             * 了AE_BARRIER标志时，要求不在可读事件之后触发可写事件。在这种情况下，将调换可读和可写事件的执行顺序，即先执行可写事件，再执行可读事件。
             * 
             * 通过这样的判断和处理，可以灵活地控制事件的执行顺序，以满足特定的应用需求。
            */
            int invert = fe->mask & AE_BARRIER;

            /* Note the "fe->mask & mask & ..." code: maybe an already
             * processed event removed an element that fired and we still
             * didn't processed, so we check if the event is still valid.
             *
             * Fire the readable event if the call sequence is not
             * inverted. */

            /** 通过fe->mask & mask & AE_READABLE的条件判断，判断当前文件描述符是否设置了可读事件的掩码。*/
            if (!invert && fe->mask & mask & AE_READABLE) {
                /** 此处进入命令执行的逻辑，跳转到connection.c的connSocketEventHandler方法 */
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);

                /** 将fired计数加一，表示已经处理了一个事件。 */
                fired++;

                /** 通过fe = &eventLoop->events[fd]语句刷新fe指针，以防止在处理过程中发生事件数组的重新分配导致指针失效。 */
                fe = &eventLoop->events[fd]; /* Refresh in case of resize. */
            }

            /* Fire the writable event. */
            /** 执行写事件函数 */
            if (fe->mask & mask & AE_WRITABLE) {
                if (!fired || fe->wfileProc != fe->rfileProc) {
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
                    fired++;
                }
            }

            /* If we have to invert the call, fire the readable event now
             * after the writable one. */
            /** 改变顺序了的话，可读事件会在此处执行 */
            if (invert) {
                fe = &eventLoop->events[fd]; /* Refresh in case of resize. */
                if ((fe->mask & mask & AE_READABLE) &&
                    (!fired || fe->wfileProc != fe->rfileProc))
                {
                    fe->rfileProc(eventLoop,fd,fe->clientData,mask);
                    fired++;
                }
            }

            /** 处理文件事件 +1 */
            processed++;
        }
    }
    /* Check time events */
    /** 如果是时间事件，那么在此处执行时间事件处理函数 */
    if (flags & AE_TIME_EVENTS)
        /** 处理时间事件，并将 processed 加上处理了时间事件的数量 */
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

/* 【核心】事件循环处理，这一段体现出了Redis的命令是单线程执行的  */
void aeMain(aeEventLoop *eventLoop) {
    /** 此处将eventLoop事件循环的属性编辑为不停止 */
    eventLoop->stop = 0;

    /** 此处进入自旋状态，直到stop状态被置为1的时候才停止自旋 */
    while (!eventLoop->stop) {
        /** 处理事件的函数 */
        aeProcessEvents(eventLoop, AE_ALL_EVENTS|
                                   AE_CALL_BEFORE_SLEEP|
                                   AE_CALL_AFTER_SLEEP);
    }
}

/**
 * 返回当前使用的IO多路复用库的名称
*/
char *aeGetApiName(void) {
    return aeApiName();
}

void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}

void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep) {
    eventLoop->aftersleep = aftersleep;
}
