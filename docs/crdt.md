# CRDT Explained

Swarm KV uses state-based CRDT types to store values. 
A Conflict-free Replicated Data Type (CRDT) is a data structure that simplifies distributed data storage systems and multi-user applications. In many systems, copies of some data need to be stored on multiple computers (known as replicas).

# Basic CRDT

## String

Swarm KV implements Last-Write-Wins Register (LWW Register) to store string values. Swarm KV didn't implement logical clock such as  [Version Vector](https://en.wikipedia.org/wiki/Version_vector) or [Lamport Clock](https://en.wikipedia.org/wiki/Lamport_timestamp),  instead,  it uses real time (man 3 gettimeofday) for simplicity.

## Integer

Swarm KV uses Positive-Negative Counter ([PN-Counter](https://en.wikipedia.org/wiki/Conflict-free_replicated_data_type#PN-Counter_(Positive-Negative_Counter))) to store integer values.

## Set and Hash

Swarm KV implements Add-Wins Observed-Remove Set (OR Set) to store set and hash values. OR Set is state-based and has no tombstone. Please referrer [An optimized conflict-free replicated set](https://arxiv.org/pdf/1210.3368.pdf) for more information.

# Probabilistic CRDT

## Age-Partioned Bloom Filter
An Age-partitioned Bloom Filter (APBloom) is a Bloom filter variant that supports duplicate detection within a sliding window. The APBloom records elements within a time range from 'now-window' to 'now'. It's a CRDT version of the [Age-partitioned bloom filters](https://arxiv.org/pdf/2001.03147.pdf).

## Staggered HyperLogLog
Staggered HyperLogLog is a HyperLogLog variant that supports near-continuous-time cardinality estimation. Please refere to [Staggered HLL: Near-continuous-time cardinality estimation with no overhead](https://www.sciencedirect.com/science/article/abs/pii/S0140366422002407) for more information.

# Token Bucket CRDT

## Observed-Consumed Token Bucket

Swarm KV implements a novel Observe-Consumed Token Bucket (OC Token Bucket), which has a PN-Counter to track consumed tokens and a Last-write-wins register to track refilled tokens. It is configured with three parameters:

 - rate - The number of tokens to be refilled in each period.
 - period - The time period in second.
 - capacity - The capacity of the token bucket.

For example, consider a token bucket with a capacity of 100, refilling at a rate of 10 tokens every 5 minutes.

At the very beginning, the bucket is full, which means it has *capacity* tokens. You can reconfigure the token bucket at any time. Note that the reconfiguration will not refill the token bucket to full.

If a network partition happens, the token bucket is out-of-sync, and each replica has the entire rate. And after the partition heals, replicas share the rate again. The OC Token Bucket is robust to overuse as long as the sync interval is reasonable, i.e., 200ms.

## Fair Token Bucket
Fair Token Bucket is used to distribute tokens from a single bucket among multiple members. It achieves [max-min fairness](https://www.ece.rutgers.edu/~marsic/Teaching/CCN/minmax-fairsh.html) through the use of Deficit Round Robin (DRR), which is defined as follows:
- Resources are allocated in order of increasing demand
- No source gets a resource share larger than its demand
- Sources with unsatisfied demands get an equal share of the resource

The Fair Token Bucket is implemented using an OC Token Bucket and Staggered HyperLogLog. The Staggered HyperLogLog is used to count the active members within each weight category over the last 5 seconds. This count is then utilized for per-weight quantum estimation. Each member is hashed into a counter of Stochastic Fair Queuing (SFQ). The counter records the consumed tokens of current interval.If the counter exceed the weight * per-weight quantum, the member is rejected to consume the OC token bucket. The length of SFQ is configured by *divisor*. Because multiple members may get hashed to the same bucket, the hashing algorithm is perturbed at configurable intervals so that the unfairness lasts only for a short while.

The underlying principle of the FTB is to reserve quantum for a perturbation interval (10 ms). If the reserved quantum remains unused, it is carried over to the next interval, contributing to an approach towards max-min fairness. The shorter the perturbation interval, the closer the system gets to achieving max-min fairness.

Note that the SFQ is not synchronized across replicas, and an excessive member may consume more tokens than its allotted share.

## Bulk Token Bucket
A bulk version of OC token bucket, which can be used for large amount of token buckets with same configuration. It uses a Grow-Only Counter Array for recording consumed tokens.



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

Shtul, Ariel, Carlos Baquero, and Paulo Sérgio Almeida. "[Age-partitioned bloom filters.](https://arxiv.org/pdf/2001.03147.pdf)" arXiv preprint arXiv:2001.03147 (2020). 

Rodrigues, Ana, et al. "[Time-limited Bloom Filter.](https://arxiv.org/pdf/2306.06742.pdf)" Proceedings of the 38th ACM/SIGAPP Symposium on Applied Computing. 2023.