<h1>
  <img src="./docs/imgs/swarmkv_logo.svg" align="left" height="40px" alt="swarmkv logo"/>
  <span>SwarmKV</span>
</h1>


**SwarmKV is a embedded and distributed key-CRDT store with peer-to-peer networking .** 

Main Features

- Cluster
- Subscription-based cache syncronization
- Strong Eventual Consistency (in the language of [CAP](https://en.wikipedia.org/wiki/CAP_theorem), this system prioritizes availability and partition tolerance).

**Why not Redis?** Because SwarmKV is

- Embedded, **no** additional servers.
- [CRDT](https://en.wikipedia.org/wiki/Conflict-free_replicated_data_type) native
- Salable for trillions of operations per second, billions of keys, and thousands of parallel operators

**Why not SQLite, rqlite, or dqlite?** Because Swarm KV is not designed to be a relational database and extremely fast.

<img src="./docs/imgs/cluster.png" alt="swamrkv-cluster" /> 



[TOC]



Following are terminologies used in Swarm KV.

Vocabularies

| Name      | Explained                                                  |
| --------- | ------------------------------------------------------------ |
| KV        | Key-value pair                                               |
| Node      | Instances in a Swarm KV cluster. A swarmkv node is identified with its IP, P2P port and UUID. |
| Slot      | KVs are partitioned in different slots, and keys of one table are scattered in many slots.  Each slot is maintained by one node. |
| Key Owner | A key ONLY has one owner.                                    |
| Replica   | A CRDT object is replicated in many nodes, which are replicas. |
| RPC       | Remote Procedure Call, used for sending command and receiving reply. |

SwarmKV Data Types

- String
- Integer
- Set
- Hash Table [todo]
- Bitmap [todo]
- Token Bucket

# COMMANDS

## Quick List

| Command                | Arguments                            | Response                                                     |
| ---------------------- | ------------------------------------ | ------------------------------------------------------------ |
| GET                    | key                                  | Bulk string reply: the value of key, or nil when key does not exist. |
| SET                    | key value                            | Simple string reply: OK if SET was executed correctly.       |
| DEL                    | key                                  | Integer reply: The number of keys that were removed.         |
| INCRYBY                | key increment                        | Integer reply: the value of key after the increment          |
| EXPIRE                 | key seconds                          | Integer reply, specifically: 1 if the timeout was set. 0 if the timeout was not set. e.g. key doesn't exist |
| TTL                    | key                                  | Integer reply: TTL in seconds, in case of error:   -2 if the key does not exist;  -1 if the key exists but has no associated expire. |
| PERSIST                | key                                  |                                                              |
| SADD                   | key member [member ...]              | Integer reply: the number of elements that were added to the set, not including all the elements already present in the set. |
| SREM                   | key member [member ...]              | Integer reply: the number of members that were removed from the set, not including non existing members. The key will NOT be deleted if all members are removed. |
| SCARD                  | key                                  | Integer reply: the cardinality (number of elements) of the set, or 0 if key does not exist. |
| SISMEMBER              | key member                           | Integer reply, specifically:<br/><br/>1 if the element is a member of the set.<br/>0 if the element is not a member of the set, or if key does not exist. |
| SMEMBERS               | key                                  | Array reply: all elements of the set.                        |
| TCFG                   | key capacity rate                    | Simple String Reply: OK if the token bucket was configured. Create the token bucket if key does not exist. |
| TCONSUM                | key tokens [NORMAL\|FORCE\|FLEXIBLE] | Integer reply： the number of tokens that were allow to consume, or -1 if key does not exist. |
| TINFO                  | key                                  | Array Reply: <br/>1) "Capacity"<br/>2) (integer) 1000000<br/>3) "Rate"<br/>4) (integer) 1000<br/>5) "Consumed"<br/>6) (integer) 5239123<br/>7) "Available"<br/>8) (integer) 3113<br/> |
| HGET                   |                                      |                                                              |
| KEYSPACE RADD          | key IP port                          | Add replica to the key, create the key if key does not exist. |
| KEYSPACE XRADD         | key IP Port                          | Add replica to the key, return NULL if the key does not exist. |
| KEYSPACE RLIST         | key                                  | Node Array reply:<br/>1) "192.168.1.200:5211"<br/>2) "192.168.1.201:5211"<br/>3) "[2001:db8::1]:5211"<br/> |
| KEYSPACE DEL           | key                                  |                                                              |
| KEYSPACE SETSLOT       | see source code                      |                                                              |
| KEYSPACE GETKEYSINSLOT | IP port slot                         |                                                              |
| KEYSPACE ADDKEYSTOSLOT | IP port slot blob                    |                                                              |
| KEYSPACE DELSLOTKEYS   | IP port slot                         |                                                              |
| KEYSPACE KEYS          | IP port pattern                      |                                                              |
| CRDT DEL               | key                                  | Integer reply: The number of cached keys that were removed.  |
| CRDT PUSH              | key blob                             | Simple string reply: OK if PUSH was executed correctly.      |
| CRDT PULL              | key                                  | Blog reply: a blob of state-based CRDT                       |
| CLUSTER KEYS           | pattern                              | Array reply: list of keys matching pattern.                  |
| CLUSTER KEYSLOT        | keyslot                              | Integer reply: The hash slot the specified key hashes to.    |
| CLUSTER NODES          |                                      |                                                              |
| CLUSTER SLOTS          |                                      |                                                              |
| CLUSTER ADDNODE        | IP port                              |                                                              |

NOTE: 

- Most of SwarmKV commands are identical the  [Redis Commands](https://redis.io/commands/).
- CLUSTER commands are used for maintainance and debugging purpose, and only available in *swarmkv-cli*.

## CLUSTER KEYS

Supported glob-style patterns:

- `h?llo` matches `hello`, `hallo` and `hxllo`
- `h*llo` matches `hllo` and `heeeello`
- `h[ae]llo` matches `hello` and `hallo,` but not `hillo`
- `h[^e]llo` matches `hallo`, `hbllo`, ... but not `hello`
- `h[a-b]llo` matches `hallo` and `hbllo`

Use `\` to escape special characters if you want to match them verbatim.

The pattern is exactly same as Redis https://redis.io/commands/keys/ .

# GET STARTED

Download and unzip swarmkv-xx.zip

- Install Hashicorp Consul，and run consul agent in debug mode
  
  - There is a consul configuration file in swarmkv source directory, copy it to a somewhere you desire.
    - Edit `./swarmkv/test/consul.d/server.hcl` and set the `bind_addr ` as you need

  - `./consul agent -dev -config-dir=./swarmkv/test/consul.d/`
  - Check consul UI via http://localhost:8500/ui
  
- Build swarmkv from source code (requires cmake version > 3.5)

  - `mkdir swarmkv-xxx/build`
  - `cd swarmkv-xxx/build`
  - `cmake .. -DCMAKE_BUILD_TYPE=Debug`
  - `make`

- Create swarmkv cluster with `swarmkv-xxx/build/tools/swarmkv-cli`

  ```
  [zhengchao@centos7-vm-dev tools]$ ./swarmkv-cli --cluster-create swarmkv-basic-test 127.0.0.1 5210
  consul KV init slot table /v1/kv/swarmkv/swarmkv-basic-test/slots.
  OK
  [zhengchao@centos7-vm-dev tools]$ ./swarmkv-cli --cluster-create swarmkv-2-nodes 127.0.0.1 5210 127.0.0.1 5220
  consul KV init slot table /v1/kv/swarmkv/swarmkv-2-nodes/slots.
  OK
  ```

- Run test cases

   -  `cd test/`
   -  `./swarmkv_gtest`

- Play around with `swarmkv-cli`

   ```
   [zhengchao@centos7-vm-dev tools]$ ./swarmkv-cli -n swarmkv-2-nodes
   swarmkv-2-nodes> get id001
   "lisi"
   swarmkv-2-nodes> ttl set1k-56
   (integer) -1
   swarmkv-2-nodes>
   ```

   

# Design

I list design choices here.

SwarmKV uses [HashiCorp Consul](https://www.consul.io/) for cluster management, which includes leader election, health checking, and slot table management.

## DHT-based KV spaces

Key-value pairs are distributed over nodes. As shown in the following picture, the key layer keeps track of the current location of every key-value pair, and the value layer stores the actual binary of key-value. This design is inspired by the NSDI'18 paper [*Elastic scaling of stateful network functions*](https://www.usenix.org/conference/nsdi18/presentation/woo).

<img src="./docs/imgs/nsdi18-s6-dht-based-dso-space.png" alt="nsdi18-s6-dht-based-dso-space" style="zoom: 67%;" />

As shown in the above picture, a KV operation is done by following steps:

1. Node A (requester) hash(key) to slot id, lookup (slot id) in the key routing table and get the key owner, which is Node B, and then send routing request to Node B.
2. The Node B (key owner) lookup (key) in the value routing table to get the object owner, which is Node C, and then a response is sent to node A.
   - if the key does not exist, the object is assigned to Node A and added to the object routing table.
3. Node A sends KV operation request to Node C.

The keyspace consistency is based on [consul](https://www.consul.io/) and its [Raft](https://raft.github.io/) implementation. As [a typical raft cluster goes up to 7 nodes](https://stackoverflow.com/questions/42422111/raft-nodes-count), so only consul server nodes participate in Raft. The consul and its [Gossip protocol](https://en.wikipedia.org/wiki/Gossip_protocol) implementation are used  to track node health.

### Global Key Slot Table

The global slot table is stored in Consul KV, the key is swarmkv/<cluster_name>/slot_table:

```
curl http://localhost:8500/v1/kv/swarmkv/<cluster_name>/slot_table?raw=true
```

The command returns a JSON array.

```json
[
	{
		"slot": "0-1022",
		"owner": {
			"ip": "192.168.0.1",
			"p2p_port": 35210,
			"node_id": "921e25e4-85a4-11ec-a8a3-0242ac120002"
		},
		"replicas": [
			{
				"ip": "192.168.0.2",
				"p2p_port": 35210,
				"node_id": "ebe5a57c-cb33-4b2f-968f-44c4b87ea8d9"
			},
			{
				"ip": "192.168.0.3",
				"p2p_port": 35210,
				"node_id": "a8ba5a68-65bd-4168-af15-1ddee7ecf5cd"
			}
		]
	},
	{
		"slot": "1023",
		"owner": {
			"ip": "192.168.0.2",
			"p2p_port": 35210,
			"node_id": "ebe5a57c-cb33-4b2f-968f-44c4b87ea8d9"
		},
		"replicas": [
			{
				"ip": "192.168.0.1",
				"p2p_port": 35210,
				"node_id": "921e25e4-85a4-11ec-a8a3-0242ac120002"
			}
		]
	}
]
```

### Cluster Leader Election

The slot table is maintained by the cluster leader, which is elected from cluster peers. SwarmKV uses [Consul for leader election](https://learn.hashicorp.com/tutorials/consul/application-leader-elections). 

```
curl http://localhost:8500/v1/kv/swarmkv/<cluster_name>/lead?raw=true
```

```json
{
    "ip":"192.168.0.1",
    "p2p_port": 35210
}
```

Except for node in dry run mode, cluster nodes will watch leadership changes and [run for it](https://learn.hashicorp.com/tutorials/consul/application-leader-elections) when no cluster leader.

1. Watch `swarmkv/<cluster_name>/lead` with [blocking query](https://www.consul.io/api/features/blocking).
2. If there is no session in the result, then run for leader with following steps
   - [Create a session](https://www.consul.io/api-docs/session#create-session).
   - [Update](https://www.consul.io/api-docs/kv#acquire) swarmkv/<cluster_name>/lead with `?acquire=<session>` parameter.
   - If the response is `true`, the node is elected to be a leader.
3. Go to  step 1.

### Hash tags

Like [Redis Cluster](https://redis.io/docs/reference/cluster-spec/), SwarmKV implements hash tags, which can be used to ensure that multiple keys are allocated in the same hash slot. If the key contains a "{...}" pattern only the substring between `{` and `}` is hashed in order to obtain the hash slot. However since it is possible that there are multiple occurrences of `{` or `}` the algorithm is well specified by the following rules:

- IF the key contains a `{` character.
- AND IF there is a `}` character to the right of `{`.
- AND IF there are one or more characters between the first occurrence of `{` and the first occurrence of `}`.

Then instead of hashing the key, only what is between the first occurrence of `{` and the following first occurrence of `}` is hashed.

Examples:

- The two keys `{user1000}.following` and `{user1000}.followers` will hash to the same hash slot since only the substring `user1000` will be hashed in order to compute the hash slot.
- For the key `foo{}{bar}` the whole key will be hashed as usually since the first occurrence of `{` is followed by `}` on the right without characters in the middle.
- For the key `foo{{bar}}zap` the substring `{bar` will be hashed, because it is the substring between the first occurrence of `{` and the first occurrence of `}` on its right.
- For the key `foo{bar}{zap}` the substring `bar` will be hashed, since the algorithm stops at the first valid or invalid (without bytes inside) match of `{` and `}`.
- What follows from the algorithm is that if the key starts with `{}`, it is guaranteed to be hashed as a whole. This is useful when using binary data as key names.

### Keyspace reorganization

The keyspace is sharding to slots and each slot is owned by one node. This information is recorded in the global shared slot table. If any node's health check failed, the cluster leader will choose a randome health node as the new slot owner.

You can use  `CLUSTER ADDNODE` in swarmkv-cli for adding nodes.

When adding a new node, the operation is **Assign slot_id from `original_node`  to `new_node`**, the  `swarmkv-cli` executes the following commands:

- `KEYSPACE SETSLOT new_node_ip new_node_port <slot_id> IMPORTING original_node_ip original_node_port` 
  - The command sets the <slot_id> of new_node to IMPORTING state.
  - When a slot is set as IMPORTING, the node will accept all queries that are about this hash slot, but only if the request is preceded by an ASKING command. If the ASKING command was not given by the client, the query is redirected to the real hash slot owner via a -MOVED redirection error, as would happen normally.

- `KEYSPACE SETSLOT original_node_ip original_node_port <slot_id> MIGRATING new_node_ip new_node_port` 
  - The command sets the <slot_id> of old_node to MIGRATING state.
  - When a slot is set as MIGRATING, the node will accept all queries that are about this hash slot, but only if the key in question exists, otherwise the query is forwarded using a -ASK redirection to the node that is target of the migration.

- `KEYSPACE GETKEYSINSLOT original_node_ip original_node_port <slot_id>`
  - The command gets a blob of all key entries of <slot_Id> from the original_node

- `KEYSPACE ADDKEYSTOSLOT new_node_ip new_node_port <slot_id> blob_of_getkeysinslot  `
  - The command adds all key entries to the <slot_id> of new_node.

- `KEYSPACE DELSLOTKEYS original_node_ip original_node_port <slot_id>`
  - The command erases all key entries of the <slot_id> of the original_node.


After all desired slots are migrated, the `swarmkv-cli` updates the global slot table in Consul.

Known Issues:

- Deleting existing keys and adding replica to existing keys between the `getkeysinslot` and `delslotkeys` could not take effect on the new node. It's better to execute `CLUSTER SANITY heal` after the migration.

- What if someone nodes are failed during the migration? [Todo] `swarmkv-cli` acquires the cluster leadership before the migration.

  - If the new node fails, the original node still owns slot k, and after a timeout without receiving slot table update, it will restore it to a stable state.

  - If the original node fails, the new node aborts this migration and waits for the cluster leader to update the global slot table.

  - If other nodes fail, the cluster leader will receive destination node's notification and update the slot table.


## KV Operation

When a requestor executes a command, it takes the following steps

- Local Execution
  - For key exists in local store, commands are executed locally, except for commands influence keyspace.
- Key Route
  - The requestor hash the key to slot to find out the key owner.
  - The requestor send  `KEYSPACE RADD/RLIST/DEL` command to the key owner.
- The Key owner processes the keyspace command and return the addresses of all replica.
- The requestor receives the key owner's reply, it will
  - Create local replica
  - Send the command to the one of the replica
  - Send `CRDT PULL key` command to some of the replicas

- Other replica executes the command, registers the requestor as one of the replica nodes, and send a reply to the requestor
- The requestor receives the reply and invokes user callback function.

## CRDT Explained

Swarm KV uses state-based CRDT types to store values. 

### String

Swarm KV implements Last-Write-Wins Register (LWW Register) to store string values. Swarm KV didn't implement logical clock such as  [Version Vector](https://en.wikipedia.org/wiki/Version_vector) or [Lamport Clock](https://en.wikipedia.org/wiki/Lamport_timestamp),  instead,  it uses real time (man 3 gettimeofday) for simplicity.

### Integer

Swarm KV uses Positive-Negative Counter (PN-Counter) to store integer values.

### Set and Hash

Swarm KV implements Add-Wins Observed-Remove Set (OR Set) to store set and hash values. OR Set is state-based and has no tombstone. Please referrer [An optimized conflict-free replicated set](https://arxiv.org/pdf/1210.3368.pdf) for more details.

### Token Bucket

Swarm KV implements Observe-Consumed Token Bucket (OC Token Bucket), which has a PN-Counter to track consumed tokens and a Last-write-wins register to track refilled tokens. It is initialized with two parameters:

- CIR: Committed Information Rate
- CBS: Committed Burst Size

After the initialization, the bucket is full, which means it has *CBS* tokens. You can reconfigure token bucket at anytime. Note that the reconfiguration will not refill token bucket to full.

When network partitions and token bucket out-of-sync, each replica have the entire CIR. And after the network heals, replicas share the CIR again. The OC Token Bucket is robust to over consuming, as long as the sync interval is reasonable.

## Fault Tolerance

### High Availability

Replication is not implemented yet.

### Health Check

Each node registers an HTTP service to the Consul, and the Consul agent is responsible for checking node health periodically via HTTP GET.
If a node fails, the cluster leader gets notification and removes the failed node by reassigning its slots to other nodes.

## Wire Protocol

Messages between nodes is start with a fixed length header and a variable length payload. The payload is serialized with [MessagePack](https://msgpack.org/index.html) format.  For readability, we use JSON to describe the request and response here.

Take the following command as an example:

```json
[
  "SADD",
  "zhangsan's friends",
  "李四",
  "Jack"
]
```

Reply message

```json
{
  "reply_type": "elements",
  "element": [
    {
      "reply_type": "string",
      "string": "+86 186 0000 0000"
    },
    {
      "reply_type": "string",
      "string": "House 01, Haidian District, Beijing, China"
    },
    {
      "reply_type": "string",
      "string": "0c 74 65 78 74 20 20 20 20 43 6f 70 79 72 69 67 68 74 20 28 63 29 20 31 39 39 38 20 48 65 77 6c 65 74 74 2d 50 61 63 6b 61 72 64 20 43 6f 6d 70 61 6e 79 20 20 64 65 73 63 20 20 20 20 20 20 20 12 73 52 47 42 20 49 45 43 36 31 39 36 36 2d 32 2e 31 20 20 20 20 20 20 20 20 20 20 20 12 73 52 47 42 20 49 45 43 36 31 39 36 36 2d 32 2e 31 20 20 00"
    }
  ]
}
```



## Node Design (Obsolete)

### Thread Model

A swarmkv instance has one main thread and several (configurable) work threads. Swarmkv uses [libevent](https://github.com/libevent/libevent) as a peer-to-peer communication infrastructure.  The main thread is responsible for:

- Accept TCP connections from new peers, and then load balance new peers to worker threads. 
  - Only This is the main reason for having a 'main' thread.
- Generate ping message periodically, and then let the worker thread which processes the destination peer to send the ping message.

The worker threads are responsible for:

- Send KV command message, which is from swarmkv API caller to destination peers.
- Receive KV command message, and then process it  based on message type:
  - Key routing request (from the requester, object reader/writer): lookup the value owner, and then send key routing response.
  - Object operation request (from the requester): lookup the value, perform the operation and then send value operation response.
  - Key routing response (from the key owner): send the request to the decoded value owner.
  - Object operation response (from object owner): update local cache, and invoke the user-specified callback.
- Receive control message, and then process it based on message type:
  - Ping message:  send a pong message which a has key routing table and node list
  - Pong message: update key routing table and node list
  - Meet message: ?

## Source Code Directory Structure

The source files are organized as follows:

- src/swarmkv.c event loop, orchestration
- src/swarmkv_store.c  kv operation, key hash table is implemented with [uthash](https://troydhanson.github.io/uthash/). Use [sds](https://github.com/antirez/sds) as a dynamic string library.
- src/t_string.c  commands of data type string.
- src/t_set.c commands of data type set.
- src/swarmkv_keyspace.c: The control plane of SwarmKV. Interact with [HashiCorp Consul](https://www.consul.io/) for node discovery, slot assignment, and leader election.
- src/swarmkv_message.c: Message is encoded to [MessagePack](https://msgpack.org/index.html) format by [mpack](https://github.com/ludocode/mpack) library.
- src/swarmkv_net.c: High performance P2P communication via [libevent](https://libevent.org/).
- src/test/ Unit tests.
- src/deps/ Dependencies in source codes format, such as [uthash](https://troydhanson.github.io/uthash/), [sds](https://github.com/antirez/sds), [mpack](https://github.com/ludocode/mpack), [timeout](https://25thandclement.com/~william/projects/timeout.c.html), [cJSON](https://github.com/DaveGamble/cJSON).
- src/vendor/  Dependencies in archive format.
- tools/swarmkv_cli: CLI implementation which is facilitated by [linenoise](https://github.com/antirez/linenoise).

# Similar Projects

[dqlite](https://dqlite.io/) is a C library that implements an embeddable and replicated SQL database engine with high availability and automatic failover.

*[rqlite](https://github.com/rqlite/rqlite)* is an easy-to-use, lightweight, distributed relational database, which uses [SQLite](https://www.sqlite.org/) as its storage engine. 

[Zeppelin](https://github.com/Qihoo360/zeppelin) is a Distributed Key-Value Platform that aims to provide excellent performance, reliability, and scalability.

[Couchbase](https://www.couchbase.com/) is a distributed, elastic, in-memory database on your cloud and at your edge.

[Apache Geode](http://geode.apache.org/) is a data management platform (JAVA) that provides real-time, consistent access to data-intensive applications throughout widely distributed cloud architectures. 

[Apache Ignite](https://ignite.apache.org/) is a distributed database for high-performance computing with in-memory speed.

[Tarantool](https://www.tarantool.io/en/) is an in-memory computing platform consisting of a database and an application server.

[eXtremeDB](https://www.mcobject.com/) is a hybrid persistent and in-memory database management system for edge and cloud.

[vedis](https://vedis.symisc.net/)  is an embeddable datastore C library built with over 70 commands similar in concept to Redis but without the networking layer since Vedis run in the same process of the host application.

[UnQLite](https://github.com/symisc/unqlite) is a in-process software library which implements a self-contained, serverless, zero-configuration, transactional NoSQL database engine. 

[OrbitDB](https://github.com/orbitdb/orbit-db) is a serverless, distributed, peer-to-peer database. OrbitDB uses [IPFS](https://ipfs.io/) as its data storage and [IPFS Pubsub](https://github.com/ipfs/go-ipfs/blob/master/core/commands/pubsub.go#L23) to automatically sync databases with peers. It's implemented in JavaScript.

[Dynomite](https://github.com/Netflix/dynomite), inspired by Dynamo whitepaper, is a thin, distributed dynamo layer for different storage engines and protocols. Currently these include Redis and Memcached. Dynomite supports multi-datacenter replication and is designed for high availability.

[AntidoteDB](https://www.antidotedb.eu/) is a planet scale, highly available, transactional database. It's implemented in Erlang. Research paper: [Cure: Strong semantics meets high availability and low latency](https://hal.inria.fr/hal-01270776v2/document).

[EdgeDB](https://github.com/edgedb/edgedb) is a new kind of database that takes the best parts of relational databases, graph databases, and ORMs. We call it a graph-relational database.

[SwarmDB](https://github.com/gritzko/swarm) is a proof-of-concept key-value RON store.

[Voldemort](https://github.com/voldemort/voldemort) is a distributed key-value storage system.

[Corfu](https://github.com/CorfuDB/CorfuDB) is a consistency platform designed around the abstraction of a shared log.

[Riak KV](https://riak.com/index.html) is a distributed NoSQL key-value database with advanced local and multi-cluster replication that guarantees reads and writes even in the event of hardware failures or network partitions.

# References

Woo, Shinae, et al. "Elastic scaling of stateful network functions." *15th {USENIX} Symposium on Networked Systems Design and Implementation ({NSDI} 18)*. 2018.

Bieniusa, Annette, et al. "[An optimized conflict-free replicated set](https://arxiv.org/pdf/1210.3368.pdf)." *arXiv preprint arXiv:1210.3368* (2012).

[C implementation of the Raft consensus protocol](https://github.com/willemt/raft) by Willem-Hendrik Thiart.

[Redis Cluster Specification](https://redis.io/topics/cluster-spec)

[Conflict-free Replicated Data Type (CRDT)](https://en.wikipedia.org/wiki/Conflict-free_replicated_data_type) 

https://crdt.tech/

https://hermes-protocol.com/

[Redis Active-Active Architecture](https://redis.com/redis-enterprise/technology/active-active-geo-distribution/)

[A fast, fault-tolerant & linearizable replication protocol](https://hermes-protocol.com/)

https://bucket4j.com/8.1.0/toc.html