## CRDT Explained

Swarm KV uses state-based CRDT types to store values. 
A Conflict-free Replicated Data Type (CRDT) is a data structure that simplifies distributed data storage systems and multi-user applications. In many systems, copies of some data need to be stored on multiple computers (known as replicas).
### String

Swarm KV implements Last-Write-Wins Register (LWW Register) to store string values. Swarm KV didn't implement logical clock such as  [Version Vector](https://en.wikipedia.org/wiki/Version_vector) or [Lamport Clock](https://en.wikipedia.org/wiki/Lamport_timestamp),  instead,  it uses real time (man 3 gettimeofday) for simplicity.

### Integer

Swarm KV uses Positive-Negative Counter (PN-Counter) to store integer values.

### Set and Hash

Swarm KV implements Add-Wins Observed-Remove Set (OR Set) to store set and hash values. OR Set is state-based and has no tombstone. Please referrer [An optimized conflict-free replicated set](https://arxiv.org/pdf/1210.3368.pdf) for more details.

### Token Bucket

Swarm KV implements a novel Observe-Consumed Token Bucket (OC Token Bucket), which has a PN-Counter to track consumed tokens and a Last-write-wins register to track refilled tokens. It is initialized with two parameters:

- CIR: Committed Information Rate
- CBS: Committed Burst Size

At the very beginning, the bucket is full, which means it has *CBS* tokens. You can reconfigure the token bucket at any time. Note that the reconfiguration will not refill the token bucket to full.

If a network partition happens, the token bucket is out-of-sync, and each replica has the entire CIR. And after the partition heals, replicas share the CIR again. The OC Token Bucket is robust to overuse as long as the sync interval is reasonable, i.e., 200ms.

### Fair Split Token Bucket
Fair Split Token Bucket is implemented with a Count-min Sketch and an OC Token Bucket. It archieves [max-min fairness](https://www.ece.rutgers.edu/~marsic/Teaching/CCN/minmax-fairsh.html) which is defined as follows:
- Resources are allocated in order of increasing demand
- No source gets a resource share larger than its demand
- Sources with unsatisfied demands get an equal share of the resource

# References
Bieniusa, Annette, et al. "[An optimized conflict-free replicated set](https://arxiv.org/pdf/1210.3368.pdf)." *arXiv preprint arXiv:1210.3368* (2012).

[Conflict-free Replicated Data Type (CRDT)](https://en.wikipedia.org/wiki/Conflict-free_replicated_data_type) 

https://crdt.tech/

https://hermes-protocol.com/

[Redis Active-Active Architecture](https://redis.com/redis-enterprise/technology/active-active-geo-distribution/)

[A fast, fault-tolerant & linearizable replication protocol](https://hermes-protocol.com/)

[Bucket4j 8.1.0 Reference](https://bucket4j.com/8.1.0/toc.html)

[An Introduction to Computer Networks: Chapter 24 - Token Bucket Rate Limiting](https://intronetworks.cs.luc.edu/current/html/tokenbucket.html)

[Deficit round robin](https://en.wikipedia.org/wiki/Deficit_round_robin)

[sfq - Stochastic Fairness Queueing](https://man7.org/linux/man-pages/man8/tc-sfq.8.html)

[Max-min fairness](https://www.ece.rutgers.edu/~marsic/Teaching/CCN/minmax-fairsh.html)

Cormode, Graham, and Shan Muthukrishnan. "[An improved data stream summary: the count-min sketch and its applications.](https://twiki.di.uniroma1.it/pub/Ing_algo/WebHome/p14_Cormode_JAl_05.pdf)" Journal of Algorithms 55.1 (2005): 58-75.

Pitel, Guillaume, and Geoffroy Fouquier. "[Count-min-log sketch: Approximately counting with approximate counters.](https://hal.science/hal-01171276/document)" arXiv preprint arXiv:1502.04885 (2015) .

Flajolet, Philippe, et al. "[Hyperloglog: the analysis of a near-optimal cardinality estimation algorithm.](https://hal.science/file/index/docid/406166/filename/FlFuGaMe07.pdf)" Discrete Mathematics and Theoretical Computer Science. Discrete Mathematics and Theoretical Computer Science, 2007.

Cornacchia, Alessandro, et al. "[Staggered HLL: Near-continuous-time cardinality estimation with no overhead.](https://www.sciencedirect.com/science/article/abs/pii/S0140366422002407)" Computer Communications 193 (2022): 168-175.

