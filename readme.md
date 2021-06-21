# Swarm KV Store

## 简介   

 Swarm KV是一个去中心化的、嵌入式（embeded, in-process）KV存储，支持集群，具有无阻塞、低延迟、高速写入、大数据量、可弹性伸缩、可诊断的特点。主要用于高性能网络功能（Network Function，NF）的状态共享。

## 术语

| 英文     | 中文   | 说明                                       |
| -------- | ------ | ------------------------------------------ |
| Node     | 节点   | 通常是一个进程。                           |
| Cluster  | 集群   | 一组需要协同工作的节点（Node）             |
| KV       | 键值对 | 由Key和Value组成                           |
| Instance | 实例   | 一个Swarm KV句柄                           |
| Table    | 表     | Key的命名空间，一个Instance可以有多个Table |



## 基础设施

Msgpack https://msgpack.org/index.html  https://github.com/msgpack/msgpack-c/tree/c_master

Nanomsg NG https://github.com/nanomsg/nng

uthash https://troydhanson.github.io/uthash/userguide.html