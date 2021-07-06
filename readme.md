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

## Wired Format
### Ping/Pong/MEET_RESP Message
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "ping",
    "node_list":[
        {
            "node_id":101,
            "node_ip":"192.168.0.1",
            "node_port":8323,
            "node_status":1
        },
        {
            "node_id":102,
            "node_ip":"192.168.0.2",
            "node_port":8323, 
            "node_status":1         
        }
    ],
    "slot_list":[
        {
            "slot_id": 1,
            "owner_id": 101
        },
        {
            "slot_id": 2,
            "owner_id": 102
        }
    ]
}
```
### MEET REQUEST Message
Request
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "MSG_MEET_REQ"
}
```

### PFAIL Message
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "pfail",
    "node_id": 100
}
```
### FAIL Message
Request
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "fail_req",
    "node_id": 100
}
```
Response
```
{
    "my_id": 102,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "fail_resp",
    "msg":"ok"
}
```
### Slot Migration
Request
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type":" "migrate_request",
    "slot_id": 2
}
```
Reponse
```
{
    "my_id": 102,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type":" "migrate_response",  
    "slot_id": 102,
    "kv":[
        {
            "key": "key1",
            "value": "value1",
        },
        {

        }
    ]
}
```
### PUT Keys
Request
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type":" "put_keys",
    "transaction_id":"8310-de139bc9-847474",
    "table_name": "tb1",
    "key": "key1",
    "value": "value1"
}
```
Response
```
{
    "my_id": 102,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type":" "put_keys_response",
    "transaction_id":"8310-de139bc9-847474",
    "msg":"ok"
}
```
### GET Keys
Request
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "get_key",
    “uuid": "8310-de139bc9-847474"
    "callback": "0xabdcefg",
    "table_name": "tb1",
    "key": "test"
}
```
Response
```
{
    "my_id": 102,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "get_keys_response",
    "callback": "print_result",
    "key": "test",
    "value": "hello"
}
```
### Invalidate Keys
Request
```
{
    "my_id": 101,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "invalidate_keys",
    "table_name": "tb1",
    "key": "key1",
    "value": "value1"
}
```
Response
```
{
    "my_id": 102,
    "my_ip": "192.168.0.1",
    "my_port": 8323,
    "msg_type": "invalidate_keys_response",
    "msg":"ok"
}
```
## 基础设施

Msgpack https://msgpack.org/index.html  https://github.com/msgpack/msgpack-c/tree/c_master

Nanomsg NG https://github.com/nanomsg/nng

uthash https://troydhanson.github.io/uthash/userguide.html