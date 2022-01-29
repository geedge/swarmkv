# Swarm KV 

**SwarmKV is a embedded and distributed key-value storage library that provides a mapping from string keys to string values.**

**Why not Redis?** Because Redis is

- Not embedded, need additional servers.
- No local cache
- Not scalable for trillions of r/w per second, billions of keys, and thousands of operators



Following are terminologies used in Swarm KV.

| Name          | Explaination                                                 |
| ------------- | ------------------------------------------------------------ |
| KV or KV Pair | Key-value pair                                               |
| Node          | Instances in a Swarm KV cluster                              |
| Slot          | KVs are partitioned in different slots, and keys of one table are scattered in many slots.  Each slot is maintained by one node. |

SwarmKV Data Types

- String
- Integer
- Hash Table
- Rate limiter

## SwarmKV Design

I list design choices here.

### DHT-based KV spaces

Key-value pairs are distributed over nodes. As shown in the following picture, the key layer keeps track of the current location of every key-value pair, and the value layer stores the actual binary of key-value. This design is inspired by the NSDI'18 paper *Elastic scaling of stateful network functions*.

![nsdi18-s6-dht-based-dso-space](./doc/imgs/nsdi18-s6-dht-based-dso-space.png)



使用Raft协议来保证Key Owner的一致性？

### Thread Model

A swarmkv instance has one main thread and many (configurable) work threads. Swarmkv uses Libevent as a peer-to-peer communication infrastructure.  The main thread is responsible for:

- Accept TCP connections from new peers, and then load balance new peers to work threads.
- Generate ping message periodically, and then let the worker thread which processes the destination peer to send the ping message.

The worker threads are responsible for:

- Send KV command message, which is from swarmkv API caller to destination peers.
- Receive KV command message, and then process it  based on message type:
  - Key routing request (from value reader/writer): lookup the value owner, and then send key routing response.
  - Value operation request (from value reader/writer): lookup the value, perform the operation and then send value operation response.
  - Key routing response (from the key owner): send the request to the decoded value owner.
  - Value operation response (from value owner): update local cache, and invoke the user-specified callback.
- Receive control message, and then process it based on message type:
  - Ping message:  send a pong message which a has key routing table and node list
  - Pong message: update key routing tabsel and node list
  - Meet message: ?

### Source Code Directory Structure

The source code are orgnized as follows:

- swarmkv_store.c event loop, kv operation
- swarmkv_message.c message serialization



## References

Woo, Shinae, et al. "Elastic scaling of stateful network functions." *15th {USENIX} Symposium on Networked Systems Design and Implementation ({NSDI} 18)*. 2018.

