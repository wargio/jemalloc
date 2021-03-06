#ifndef JEMALLOC_INTERNAL_HPA_H
#define JEMALLOC_INTERNAL_HPA_H

#include "jemalloc/internal/exp_grow.h"
#include "jemalloc/internal/hpa_central.h"
#include "jemalloc/internal/pai.h"
#include "jemalloc/internal/psset.h"

typedef struct hpa_s hpa_t;
struct hpa_s {
	/*
	 * We have two mutexes for the central allocator; mtx protects its
	 * state, while grow_mtx protects controls the ability to grow the
	 * backing store.  This prevents race conditions in which the central
	 * allocator has exhausted its memory while mutiple threads are trying
	 * to allocate.  If they all reserved more address space from the OS
	 * without synchronization, we'd end consuming much more than necessary.
	 */
	malloc_mutex_t grow_mtx;
	malloc_mutex_t mtx;
	hpa_central_t central;
	/* The arena ind we're associated with. */
	unsigned ind;
	/*
	 * This edata cache is the global one that we use for new allocations in
	 * growing; practically, it comes from a0.
	 *
	 * We don't use an edata_cache_small in front of this, since we expect a
	 * small finite number of allocations from it.
	 */
	edata_cache_t *edata_cache;
	exp_grow_t exp_grow;
};

/* Used only by CTL; not actually stored here (i.e., all derived). */
typedef struct hpa_shard_stats_s hpa_shard_stats_t;
struct hpa_shard_stats_s {
	psset_bin_stats_t psset_full_slab_stats;
	psset_bin_stats_t psset_slab_stats[PSSET_NPSIZES];
};

typedef struct hpa_shard_s hpa_shard_t;
struct hpa_shard_s {
	/*
	 * pai must be the first member; we cast from a pointer to it to a
	 * pointer to the hpa_shard_t.
	 */
	pai_t pai;
	malloc_mutex_t grow_mtx;
	malloc_mutex_t mtx;
	/*
	 * This edata cache is the one we use when allocating a small extent
	 * from a pageslab.  The pageslab itself comes from the centralized
	 * allocator, and so will use its edata_cache.
	 */
	edata_cache_small_t ecs;
	hpa_t *hpa;
	psset_t psset;

	/*
	 * When we're grabbing a new ps from the central allocator, how big
	 * would we like it to be?  This is mostly about the level of batching
	 * we use in our requests to the centralized allocator.
	 */
	size_t ps_goal;
	/*
	 * What's the maximum size we'll try to allocate out of the psset?  We
	 * don't want this to be too large relative to ps_goal, as a
	 * fragmentation avoidance measure.
	 */
	size_t ps_alloc_max;
	/*
	 * What's the maximum size we'll try to allocate out of the shard at
	 * all?
	 */
	size_t small_max;
	/*
	 * What's the minimum size for which we'll go straight to the global
	 * arena?
	 */
	size_t large_min;

	/* The arena ind we're associated with. */
	unsigned ind;
};

bool hpa_init(hpa_t *hpa, base_t *base, emap_t *emap,
    edata_cache_t *edata_cache);
bool hpa_shard_init(hpa_shard_t *shard, hpa_t *hpa,
    edata_cache_t *edata_cache, unsigned ind, size_t ps_goal,
    size_t ps_alloc_max, size_t small_max, size_t large_min);
/*
 * Notify the shard that we won't use it for allocations much longer.  Due to
 * the possibility of races, we don't actually prevent allocations; just flush
 * and disable the embedded edata_cache_small.
 */
void hpa_shard_disable(tsdn_t *tsdn, hpa_shard_t *shard);
void hpa_shard_destroy(tsdn_t *tsdn, hpa_shard_t *shard);

/*
 * We share the fork ordering with the PA and arena prefork handling; that's why
 * these are 3 and 4 rather than 0 and 1.
 */
void hpa_shard_prefork3(tsdn_t *tsdn, hpa_shard_t *shard);
void hpa_shard_prefork4(tsdn_t *tsdn, hpa_shard_t *shard);
void hpa_shard_postfork_parent(tsdn_t *tsdn, hpa_shard_t *shard);
void hpa_shard_postfork_child(tsdn_t *tsdn, hpa_shard_t *shard);

/*
 * These should be acquired after all the shard locks in phase 4, but before any
 * locks in phase 4.  The central HPA may acquire an edata cache mutex (of a0),
 * so it needs to be lower in the witness ordering, but it's also logically
 * global and not tied to any particular arena.
 */
void hpa_prefork4(tsdn_t *tsdn, hpa_t *hpa);
void hpa_postfork_parent(tsdn_t *tsdn, hpa_t *hpa);
void hpa_postfork_child(tsdn_t *tsdn, hpa_t *hpa);

#endif /* JEMALLOC_INTERNAL_HPA_H */
