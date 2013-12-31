/**
 * A bitmap with fast operations up to 64*64 = 4096 bits
 */
#ifndef BIGMAP_H_
#define BIGMAP_H_

#define BIGMAP_MAX_LEN		MAX_NODES
#if MAX_NODES > (64*64)
#error "MAX_NODES too big for bigmap (which supports up to 4096 bits)"
#endif


struct bigmap {
		u64 mask[(BIGMAP_MAX_LEN + 63) / 64];
		u64 summary;
};

static inline void bigmap_init(struct bigmap *map)
{
	map->summary = 0;
	memset(map->mask, 0, sizeof(map->mask));
}

static inline void bigmap_set(struct bigmap *map, uint32_t index)
{
	uint32_t word = index >> 6;
	map->mask[word] |= (1UL << (index & 0x3F));
	map->summary    |= (1UL << word);
}

static inline void bigmap_clear(struct bigmap *map, uint32_t index)
{
	uint32_t word = index >> 6;
	map->mask[word] &= ~(1UL << (index & 0x3F));
	if (map->mask[word] == 0UL)
		map->summary    &= ~(1UL << word);
}

static inline bool bigmap_empty(struct bigmap *map)
{
	return (map->summary == 0);
}

static inline bool bigmap_is_set(struct bigmap *map, uint32_t index)
{
	uint32_t word = index >> 6;
	return map->mask[word] & (1UL << (index & 0x3F));
}

static inline uint32_t bigmap_find(struct bigmap *map)
{
	uint32_t word = __ffs(map->summary);
	uint32_t offset = __ffs(map->mask[word]);
	return (word << 6) + offset;
}

#endif /* BIGMAP_H_ */
