# Swarm KV Store

**SwarmKV is a embed and distributed key-value storage library provides an mapping from string keys to string values.**

Following are terminologies used in Swarm KV.

| Name          | Explaination                                                 |
| ------------- | ------------------------------------------------------------ |
| KV or KV Pair | Key-value pair                                               |
| Node          | Instances in a Swarm KV cluster                              |
| Slot          | KV are partioned in different slot, and keys of one table are scatter in many slots.  Each slot is maintained by one node. |

Value Data Types

- String
- Integer
- Hash Table

