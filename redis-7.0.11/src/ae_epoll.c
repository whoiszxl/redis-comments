/* Linux epoll(2) based ae.c module
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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


#include <sys/epoll.h>

typedef struct aeApiState {
    int epfd; /* epoll监听的内核注册表的文件描述符，这个文件描述符可以实现事件的注册、修改、删除等操作 */
    struct epoll_event *events; /* 存储发生事件的详细信息 */
} aeApiState;

/**
 * 创建并初始化事件处理器
*/
static int aeApiCreate(aeEventLoop *eventLoop) {
    /** 通过调用 zmalloc 函数为 aeApiState 结构分配内存，并检查分配是否成功。  */
    aeApiState *state = zmalloc(sizeof(aeApiState));
    if (!state) return -1;

    /** 
     * 使用 eventLoop->setsize 乘以 epoll_event 结构的大小为 state->events 分配内存，用于存储事件状态。
     * 同样它也会检查内存分配是否成功。 
     * */
    state->events = zmalloc(sizeof(struct epoll_event)*eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }

    /**
     * 此处调用 epoll_create 来创建一个 epoll 实例，并将返回的文件描述符存储到 state->epfd 中。
    */
    state->epfd = epoll_create(1024); /* 1024 is just a hint for the kernel */

    /** 如果创建失败，则释放之前分配的内存，并返回 -1 表示我创建失败了。 */
    if (state->epfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }

    /** 设置 state->epfd 为关闭时自动释放资源 */
    anetCloexec(state->epfd);

    /** 接着将 state 赋值给 eventLoop->apidata */
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    state->events = zrealloc(state->events, sizeof(struct epoll_event)*setsize);
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->epfd);
    zfree(state->events);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    /** 获取到对应的IO多路复用事件处理机制，此处获取的为 epoll */
    aeApiState *state = eventLoop->apidata;

    /** 创建一个 struct epoll_event 类型的变量 ee，用于向 epoll 实例中添加或修改事件时使用。 */
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    /** 检查文件描述符 fd 是否已经被监听，如果是，则将操作类型 op 设置为 EPOLL_CTL_MOD，表示要修改监听的事件；
     * 如果不是，则将操作类型 op 设置为 EPOLL_CTL_ADD，表示要添加新的监听事件 */
    int op = eventLoop->events[fd].mask == AE_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    /** epoll_event events 初始化为 0 */
    ee.events = 0;

    /** 将原始时间的 mask 与 新的 mask 合并 */
    mask |= eventLoop->events[fd].mask; /* Merge old events */

    /** 判断新的 mask 是否是读写事件，若是则将 epoll 对应的状态 EPOLLIN EPOLLOUT 通过位或操作新增到 ee.events 中 */
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;

    /** 将需要添加的事件的 fd 添加到 ee.data.fd 中 */
    ee.data.fd = fd;

    /** 最终将 ee 加入 epoll_ctl 的参数中，通过此方式便可将一个文件事件注册到文件事件中 */
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    int mask = eventLoop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}

/**
 * Redis 对 epoll 的封装实现，阻塞获取事件
*/
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    /** 从事件循环处理器中拿到 epoll 的文件描述符和就绪事件 */
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    /** 
     * 接着我们把 epoll 的文件描述符和就绪事件传入，当阻塞中监听到事件之后，便会将事件写入 state->events 中
     * 
     * eventLoop->setsize 则限制了最大的事件数量
     * 
     * tvp 则是计算阻塞时间的超时时间，如果为空，则为-1，需要无限等待下去；不为空则将之前计算出来的 tv_sec 和 tv_usec 相加得出超时时间
     **/
    retval = epoll_wait(state->epfd,state->events,eventLoop->setsize,
            tvp ? (tvp->tv_sec*1000 + (tvp->tv_usec + 999)/1000) : -1);

    /** 获取的就绪事件大于 0 的话，那么就要开始处理了 */
    if (retval > 0) {
        int j;

        numevents = retval;

        /** 开始遍历所有的就绪事件 */
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            /** 获取到对应的事件 */
            struct epoll_event *e = state->events+j;

            /** 如果e->events中包含EPOLLIN标志，表示该文件描述符可读，将AE_READABLE掩码与mask进行按位或操作，将可读标志加入到掩码中。 */
            if (e->events & EPOLLIN) mask |= AE_READABLE;

            /** 如果e->events中包含EPOLLOUT标志，表示该文件描述符可写，将AE_WRITABLE掩码与mask进行按位或操作，将可写标志加入到掩码中。 */
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;

            /** 如果e->events中包含EPOLLOUT标志，表示该文件描述符可写，将AE_WRITABLE掩码与mask进行按位或操作，将可写标志加入到掩码中。 */
            if (e->events & EPOLLERR) mask |= AE_WRITABLE|AE_READABLE;

            /** 如果e->events中包含EPOLLHUP标志，表示该文件描述符发生了挂起状态，将AE_WRITABLE和AE_READABLE掩码与mask进行按位或操作，将可写和可读标志同时加入到掩码中。 */
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE|AE_READABLE;
            
            /** 接着将事件的文件描述符和mask存储到封装好的时间循环处理器的 fired 就绪数组中 */
            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    } else if (retval == -1 && errno != EINTR) {
        /** 如果没有返回事件还报错了，则调用 panic 打印错误日志 */
        panic("aeApiPoll: epoll_wait, %s", strerror(errno));
    }

    /** 返回就绪事件数量 */
    return numevents;
}

static char *aeApiName(void) {
    return "epoll";
}
