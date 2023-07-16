# Redis-7.0.11 中文注释版本

此注释版基于 [Redis 7.0.11](https://github.com/redis/redis/tree/7.0.11) 编写了主要流程的中文注释，便于在学习 Redis 的过程中了解底层的设计原理与实现机制。

在掌握了 Redis 基本的原理与应用之后，我们便可将其源码构建起来，通过 Debug 的方式来逐行分析底层的一些设计实现，正本溯源，而不仅仅是浮于表面，只知其一不知其二。

## Redis源码整体架构
![readme_6](assets/readme_6.png)

## Redis源码阅读环境构建
此处我们采用操作系统 [Ubuntu 20.04.4 LTS](https://releases.ubuntu.com/focal/) 来构建 Redis 源码阅读环境，使用其他类 Unix 系统比如 MacOS 也可以按照此操作来进行构建。Windows 系统则建议安装一个 VirtualBox 或者 vmware 虚拟机来构建。

或者通过其他云厂商的云主机来进行构建，如阿里云的[无影云电脑](https://www.aliyun.com/minisite/goods?userCode=u47plryb)，4核8G的云电脑新用户可免费使用3个月。


### 源码clone与编译
进入操作系统，打开 Terminal 终端。

首先确保系统支持C语言的编译，C语言的编译需要有 gcc 的工具支持，gcc 是一种功能强大的C语言编译器，是开发和构建 C 语言项目的重要工具：
```bash
# 查看 gcc 的版本号，如果存在则输出对应信息：gcc version 9.4.0 (Ubuntu 9.4.0-1ubuntu1~20.04.1)
gcc -v

# 如无 gcc 环境，则需安装
sudo apt install gcc
```

创建对应的文件夹来存储 Redis 源代码：
```bash
# 创建 sourcecode 文件夹并进入到文件夹内
mkdir ~/sourcecode && cd ~/sourcecode
```

克隆代码到本地：
```bash
git clone https://github.com/whoiszxl/redis-comments.git
```

进行代码的构建：
```bash
# 进入到 Redis 源码路径下
cd ~/sourcecode/redis-comments/redis-7.0.11/

# 执行 make 命令进行构建，执行后会打包出一批 redis-server、redis-cli 的可执行程序出来
make CFLAGS="-g -O0"
```
make 命令之后，携带了一部分参数，CFLAGS 是一个环境变量，用于指定编译器的编译选项。其中 -g 则表示在编译过程中生成调试信息，-O0 则表示需要关闭优化选项，编译器通常会对代码进行优化，以提高执行速度或减少生成的可执行文件的大小，这样就会导致我们编译出来的 Redis 可执行文件中的代码和 Redis 源码会产生不一致的情况，极大可能会出现代码结构不一致，执行流程不一致的情况。所以，我们需要通过 -O0 参数来关闭优化选项，避免 Debug 时出现一些迷之问题。

倘若是发布 Redis 到生产环境使用，则无需添加这些参数，直接执行 `make` 即可。

**注意事项：**
如果执行 `make CFLAGS="-g -O0"` 后，运行 `./src/redis-server` 后报错：`Segmentation fault (core dumped)`，可以执行 `make distclean` 清理目前的编译结果，再执行 `make CFLAGS="-g -O0" MALLOC=libc` 进行编译即可解决此问题，`MALLOC=libc` 参数为强制指定 `libc` 为 `malloc` 做内存分配。


在命令行中运行编译好的 Redis 服务:
```bash
# 要使用默认配置运行 Redis，只需键入：
cd ~/sourcecode/redis-comments/redis-7.0.11/src
./redis-server

# 使用 redis.conf 配置来运行，则须使用一个额外的参数（配置文件的路径）来运行：
./redis-server ../redis.conf

# 也可以通过命令行直接传递参数作为选项来改变Redis配置：
./redis-server --port 9999 --replicaof 127.0.0.1 6379
./redis-server ../redis.conf --loglevel debug
```

使用 redis-cli 客户端来连接上一步启动的 redis-server：
```bash
# 开启一个新的 Terminal 终端，并进入到 src 目录下
cd ~/sourcecode/redis-comments/redis-7.0.11/src

# 执行 redis-cli 命令进入客户端交互模式
./redis-cli

# 执行 ping 命令测试一下连接是否有效
redis> ping
PONG

# 测试 set get 命令
redis> set name whoiszxl
OK
redis> get name
"whoiszxl"

# 测试 incr 命令
redis> incr mycounter
(integer) 1
redis> incr mycounter
(integer) 2
```

可以在这个链接中找到所有可用的 Redis 命令：[https://redis.io/commands](https://redis.io/commands)


### 在IDE中运行源码
上述步骤已将源码编译，可以正常运行之后便可将此源码运行到 IDE 中。这里我们使用 JetBrains 的 [Clion](https://www.jetbrains.com/clion/) 来运行源码。

点击此链接[https://www.jetbrains.com/clion/download/#section=linux](https://www.jetbrains.com/clion/download/#section=linux)进行下载，或者系统为 Ubuntu 16.04 或以上的话，可以执行此命令 `sudo snap install clion --classic` 来进行安装。

通过下载的方式，解压之后的目录如下：
```
clion-2023.1.4
├── bin
├── build.txt
├── help
├── Install-Linux-tar.txt
├── jbr
├── lib
├── license
├── plugins
└── product-info.json
```

执行命令进行启动：`sh ./bin/clion.sh`


初次打开时，Clion 会提示你加载项目前需要先 `Clean Project`, 此处需要点 OK 来先清理项目，此操作会将之前执行 `make CFLAGS="-g -O0"` 命令编译出来的可执行文件清理掉，执行成功后，下方的 Build 会提示构建完成。
![readme_1](assets/readme_1.png)

提示完成之后，还需重新进入 `~/sourcecode/redis-comments/redis-7.0.11/` 目录下，重新执行 `make CFLAGS="-g -O0"` 命令构建一次。（或者在未编译时通过 Clion 打开源码，便仅需编译一次）

编译完成之后，Clion 右上方的运行配置中，便会有一系列的可执行配置，如下图所示。

![readme_2](assets/readme_2.png)


接着选择到 redis-server 这一项，直接点击 debug 运行按钮，则会弹出一个配置框，此处需要在 Executable 中选择我们编译好的 redis-server 程序，最后，点击最下方的 debug 按钮来运行 redis-server 程序。此方式运行则会采用默认的配置，如需要采用自定义配置，则可在 Program arguments 中填入 redis.conf 的绝对路径。

![readme_3](assets/readme_3.png)


### 断点调试
当环境构建好后，便可打上断点调试代码。此处以执行一个 set 命令来做断点调试。

首先，找到 `t_string.c` 这个代码文件，这个文件专门用来处理 `String` 相关的命令。我们需要在一个 `setCommand` 函数里打上一个断点。

![readme_4](assets/readme_4.png)

然后，我们可以将 `redis-cli` 启动，用以执行 set 命令。我们可以选择执行运行 `redis-cli` 程序，也可以在 `Clion` 中选择上一步 Debug 调试 redis-server 的逻辑一样，通过 Debug 的方式将 `redis-cli` 运行起来。

运行起来之后，便可执行一个 set 命令，如：`set username whoiszxl`，则我们可以看到 `Clion` 中的断点已经打进去了。断点打上去之后，将鼠标指针指向对应实例之后，便可以看到实例的相关信息。

如图所示，将鼠标指针指向 `client` 指针后，我们便可以看到 `client` 实例的相关信息，如 `querybuf` 中可以看到客户端发送过来的 `RESP` 协议的消息体，`cmd` 中可以看到 `set` 命令的相关描述，如 `complexity` 字段中可看到其命令的时间复杂度。
![readme_5](assets/readme_5.png)


## 如何阅读源码
以 Redis 为例，此处则以一个 Web 开发者的角度来阐述阅读一个中间件的源码的通用思路。

### 确定目标
首先第一步是先需要确定目标，需要明确自己想要学习的技术是什么，比如说我们这里就是要学习 `Redis`。我们需要先了解这个技术的基本概念、用途以及特点等等。接着就是需要获取到对应的资源，包括官方文档、书籍、在线教程、视频课程等。这些资料可以帮助你系统地学习技术的各个方面。

从 `Redis` 来说，我们要分析清楚这个技术的基本概念、用途以及特点，只需要从[官网](https://redis.io/)就能得到结果。

官网简介如下：
```
Redis是一个开源的内存数据存储系统，被数百万开发者用作数据库、缓存、流处理引擎和消息中间件。它提供了一种快速、高性能的数据存储和处理解决方案。
```

核心特性如下：
* 内存中的数据结构：Redis被广泛称为“数据结构服务器”，支持字符串、哈希、列表、集合、有序集合、流等多种数据结构。
* 可编程性：Redis支持使用Lua进行服务器端脚本编程，还支持使用Redis Functions进行服务器端存储过程编程。
* 可扩展性：Redis提供了C、C++和Rust的模块API，可以通过编写自定义扩展模块来扩展Redis的功能。
* 持久化：Redis将数据保存在内存中以实现快速访问，但同时可以将所有写操作持久化到永久存储介质上，以便在重启和系统故障时恢复数据。
* 分布式集群：Redis支持基于哈希的分片机制进行水平扩展，可以扩展到数百万个节点，并在集群扩展时自动进行重新分片。
* 高可用性：Redis支持主从复制和自动故障转移，无论是独立部署还是集群部署，都可以实现高可用性。

用途如下：
* Redis是一个实时数据存储系统，它的多功能内存数据结构使得构建实时应用程序的数据基础设施变得容易，这些应用程序需要低延迟和高吞吐量。
* 作为缓存和会话存储，Redis的速度非常快，非常适合用于缓存数据库查询、复杂计算、API调用和会话状态等。通过将热门数据存储在Redis的内存中，可以大大加快数据访问速度，减轻后端数据库的负载。
* Redis的流数据类型使得高速数据摄取、消息传递、事件溯源和通知等场景成为可能。它可以接收和存储高速数据流，并支持按时间顺序获取数据，可以应用于流式数据处理、实时分析和实时通信等应用。

### 深入学习
从官网分析清楚后，我们便可通过获取的 `Redis` 资源深入学习其原理、应用与源码。需要掌握的技能如下：
* 掌握各种数据类型的基本使用，包括字符串（string）、列表（list）、哈希（hash）、集合（set）、有序集合（zset）、位图（bitmap）、HyperLogLog、地理位置（geo）、流（stream）和bitfield。
* 熟悉持久化策略，包括RDB和AOF的使用。了解如何将数据持久化到磁盘，以便在重启或系统故障时恢复数据。
* 理解主从复制和哨兵监控的概念与使用。如何配置和管理Redis的主从复制，以及如何使用哨兵监控Redis实例的健康状态和自动故障转移。
* 学习Redis集群分片的原理和使用。了解如何在Redis集群中分片数据以实现水平扩展和高可用性。
* 第三方应用整合 Redis 应用，如 Java，Golang，Php 应用整合实现一些业务方案，业务方案如：分布式缓存、分布式锁、会话存储、计数器等。
* ......

总而言之在读源码前必须先掌握 Redis 的基础应用。

### 整体分析
在掌握了一个中间件的基础应用后，便可以开始分析中间件的源码了。首先需要获取到相关源代码。以 Redis 为例，我们可以从 [Redis官网](https://redis.io/) 获取，或者从 Github 上获取，Redis 的 Github 地址为：[https://github.com/redis/redis](https://github.com/redis/redis) 。从 Github 获取源代码，我们可以很方便的从分支管理中获取特定的版本，本仓库便是基于 [Redis 7.0.11](https://github.com/redis/redis/tree/7.0.11) 构建。

获取到对应的源码后，首先应该做的便是对源码做一个**整体分析**。读源码最忌讳的便是漫无目的的阅读，所以我们先需要分析一下 redis-7.0.11 这个目录下，有一些什么目录，有一些什么源码文件，得到一个大致的认识之后，我们再去读源码时才会有路可寻。

做整体分析，我们也得掌握一定的方法。首先，最好的方法便是阅读源码中自带的注释或者 README 文件。举例来说，Redis 源码的一级目录下有一个 deps 文件夹，打开之后便有一个 [README.md](https://github.com/whoiszxl/redis-comments/blob/master/redis-7.0.11/deps/README.md) 文件，此文件开头内容如下：
```
This directory contains all Redis dependencies, except for the libc that should be provided by the operating system.

Jemalloc is our memory allocator, used as replacement for libc malloc on Linux by default. It has good performances and excellent fragmentation behavior. This component is upgraded from time to time.
hiredis is the official C client library for Redis. It is used by redis-cli, redis-benchmark and Redis Sentinel. It is part of the Redis official ecosystem but is developed externally from the Redis repository, so we just upgrade it as needed.
linenoise is a readline replacement. It is developed by the same authors of Redis but is managed as a separated project and updated as needed.
lua is Lua 5.1 with minor changes for security and additional libraries.
hdr_histogram Used for per-command latency tracking histograms.
```

翻译如下：
```
该目录包含了 Redis 的所有依赖项，除了操作系统应提供的 libc（C 标准库）。

Jemalloc：作为内存分配器，它替代了 Linux 默认的 libc malloc。Jemalloc 具有良好的性能和优秀的内存碎片处理能力。我们定期升级此组件以获得更好的性能和功能。

Hiredis：这是 Redis 的官方 C 客户端库。它被 redis-cli、redis-benchmark 和 Redis Sentinel 等工具所使用。尽管它是 Redis 生态系统的一部分，但是它是在 Redis 主代码库之外进行开发的。我们根据需要升级它以引入改进和修复 bug。

Linenoise：Linenoise 是一个 readline 的替代品，提供了交互式命令行界面的行编辑功能。它由 Redis 的作者开发，但作为一个独立的项目进行管理，并根据需要进行更新。

Lua：Redis 包含了 Lua 5.1 版本，并进行了一些安全修改和附加库的添加。Lua 是一种强大的脚本语言，用于在 Redis 中进行服务器端脚本编写，实现自定义命令和高级数据处理功能。

hdr_histogram：该依赖项用于跟踪每个命令的延迟，并生成直方图。它帮助测量和分析 Redis 命令的执行时间，对于性能监控和优化非常有用。
```

通过此 `README.md` 文件，我们不难得出 `deps` 文件夹的作用是做什么的。所以，我们要整体分析 Redis 架构的话，通过此种方式便能得出一二。

Redis 大致的源代码分析图可参照此 README 最上方的Redis源码整体架构。

### 寻找入口
清楚了 Redis 源码的整体架构之后，我们便可有选择性的来阅读代码了。要阅读代码的话，最好的办法便是通过 Debug 的方式来执行代码，再根据技术的原理进行相互验证。如若只是在编辑器中观看代码，行之定然不太有效。所以，再此之前，可以按照此 README 前部分来搭建好源码阅读的环境。

搭建好后，便是寻找源码阅读的入口。入口则有多种：
* 入口文件：通常开源中间件的源码会有一个主要的入口文件，Redis 中的入口文件便是 `src/server.c`，其中有一个 `main` 方法，执行后便可启动 `redis-server`，从此文件便可以了解整个服务的启动过程与基本的功能。在阅读其他中间件源码也是类似，像 Nacos、Kafka 等中间件的入口都是通过一个 main 方法启动。
* 核心模块：根据中间件的功能和设计，可以选择阅读其中的核心模块。对于 Redis 来说，核心模块包括各大数据类型、网络通信模块、命令解析与执行模块等。选择其中一个核心模块作为入口开始阅读，可以深入了解该模块的实现原理和关键算法。
* 命令执行：根据一条命令的执行，分析一条命令从 Client 端发送到 Server 端的执行过程。通过这种方式可以从命令解析、网络通信、命令执行、数据类型皆有涉及，当整条链路分析完成之后，其大部分的源码便已是通读了，因为在执行不同的命令时，命令解析、网络通信、命令执行等逻辑都是一致的，不同的仅仅是数据结构上的不同。

此文档中便采用 `命令执行` 的方式来分析如何阅读源码。