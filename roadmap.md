# Transactions
Read-only transactions always succeed: all reads just need to read and that's it.

Now, non read-only transactions can instead fail: if one transaction has read a unit, and the other has written it, one of the two should be aborted.

We should prioritizing reads instead of writes, since that's the main part of the test workload.

## Multi-versioning
Each transaction keeps its own copy of each unit it accesses. By default, that should be the first segment, with other segments being copied on demand.

Should read-only transactions make a copy?
* No (read from the main copy): then we need to prevent bad things from happening in case some other thread overwrites that memory.
  One thing that could work with this concept is that of (atomically) swapping pointers when merging changes in a non-ro transaction. We need to combine that with some sort of reference counting however.
  Commits that fail should fail as soon as possible (to retry the transaction), meaning we should check asap if it can succeed. To do that, for each unit, keep track of whether or not it has been read by at least one process before. This probably means you just need two copies of each unit: one for read-only processes, one for other  ones.

If we use shared pointers, the ABA problem _cannot_ happen: each transaction holds a reference to the segment they last saw, which means it couldn't have been deallocated in the meantime. Therefore, if a new segment has taken the place of the original one, it will necessarily have a different pointer.


We're probably going to need a lock to update tm_allocs and tm_frees. However, we can be smart about taking that lock, only taking it if we have actually performed a tm_alloc or free at all.

## (Some sort of) Locking
Keep in mind: about 50% of the transactions (almost all transactions that will write something) will read one account and write one account. This would probably (you need to measure this) make it wasteful to copy the entire segment for each transaction.

What we could do instead is some sort of sharded locking mechanism. Locks should NOT be relased after a read/write, but only once the transaction has completed. As an optimization, we could try spinning/sleeping a little bit before failing the transaction altogether.
