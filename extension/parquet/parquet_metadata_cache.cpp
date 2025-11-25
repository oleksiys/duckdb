#include "parquet_metadata_cache.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/object_cache.hpp"

namespace duckdb {

ParquetMetadataCache::ParquetMetadataCache(idx_t max_memory_bytes)
    : current_memory(0), max_memory(max_memory_bytes), enabled(true), total_hits(0), total_misses(0),
      total_evictions(0) {
}

shared_ptr<ParquetMetadataCache> ParquetMetadataCache::Get(DatabaseInstance &db) {
	auto &object_cache = db.GetObjectCache();

	// Try to get existing cache from ObjectCache
	auto cache = object_cache.Get<ParquetMetadataCache>(ObjectType());
	if (cache) {
		return cache;
	}

	// Lazy initialization - create the cache on first access
	// Read configuration from database config
	idx_t max_size = 256ULL * 1024ULL * 1024ULL; // Default 256MB
	bool cache_enabled = false; // Default disabled

	// Try to get configuration values
	Value result;
	auto lookup_result = db.TryGetCurrentSetting("parquet_metadata_cache_size", result);
	if (lookup_result) {
		max_size = UBigIntValue::Get(result);
	}
	lookup_result = db.TryGetCurrentSetting("parquet_metadata_cache", result);
	if (lookup_result) {
		cache_enabled = BooleanValue::Get(result);
	}

	// Create and register new cache
	auto new_cache = make_shared_ptr<ParquetMetadataCache>(max_size);
	new_cache->SetEnabled(cache_enabled);
	object_cache.Put(ObjectType(), new_cache);

	return new_cache;
}

shared_ptr<ParquetMetadataCache> ParquetMetadataCache::Get(ClientContext &context) {
	return Get(DatabaseInstance::GetDatabase(context));
}

shared_ptr<ParquetFileMetadataCache> ParquetMetadataCache::Get(const string &key) {
	if (!enabled.load()) {
		return nullptr;
	}

	lock_guard<mutex> guard(lock);

	auto entry = cache.find(key);
	if (entry == cache.end()) {
		total_misses++;
		return nullptr;
	}

	// Cache hit - update LRU
	total_hits++;
	TouchEntry(key);
	return entry->second;
}

void ParquetMetadataCache::Put(const string &key, const shared_ptr<ParquetFileMetadataCache> &entry) {
	if (!enabled.load()) {
		return;
	}

	auto memory_size = entry->GetMemoryUsage();

	lock_guard<mutex> guard(lock);

	// Check if entry already exists
	auto existing = cache.find(key);
	if (existing != cache.end()) {
		// Update existing entry
		auto old_size = existing->second->GetMemoryUsage();
		current_memory -= old_size;

		// Remove from LRU list
		lru_list.erase(lru_map[key]);
		lru_map.erase(key);
	}

	// Evict entries if needed to make space
	EvictIfNeeded(memory_size);

	// Add new entry
	cache[key] = entry;
	lru_list.push_front(key);
	lru_map[key] = lru_list.begin();
	current_memory += memory_size;
}

void ParquetMetadataCache::Delete(const string &key) {
	lock_guard<mutex> guard(lock);
	EvictEntryInternal(key);
}

void ParquetMetadataCache::Clear() {
	lock_guard<mutex> guard(lock);

	cache.clear();
	lru_list.clear();
	lru_map.clear();
	current_memory = 0;
}

idx_t ParquetMetadataCache::GetCurrentMemory() const {
	return current_memory.load();
}

idx_t ParquetMetadataCache::GetMaxMemory() const {
	return max_memory.load();
}

void ParquetMetadataCache::SetMaxMemory(idx_t max_memory_bytes) {
	max_memory = max_memory_bytes;

	lock_guard<mutex> guard(lock);
	// Evict if current memory exceeds new limit
	EvictIfNeeded(0);
}

void ParquetMetadataCache::SetEnabled(bool enabled_p) {
	enabled = enabled_p;
	if (!enabled_p) {
		// Clear cache when disabled
		Clear();
	}
}

bool ParquetMetadataCache::IsEnabled() const {
	return enabled.load();
}

ParquetMetadataCache::CacheStats ParquetMetadataCache::GetStats() const {
	lock_guard<mutex> guard(lock);

	CacheStats stats;
	stats.entry_count = cache.size();
	stats.current_memory = current_memory.load();
	stats.max_memory = max_memory.load();
	stats.total_hits = total_hits.load();
	stats.total_misses = total_misses.load();
	stats.total_evictions = total_evictions.load();

	return stats;
}

void ParquetMetadataCache::EvictIfNeeded(idx_t incoming_size) {
	// Caller must hold lock
	auto max_mem = max_memory.load();

	while (!lru_list.empty() && current_memory.load() + incoming_size > max_mem) {
		// Evict least recently used entry (back of list)
		auto victim_key = lru_list.back();
		EvictEntryInternal(victim_key);
		total_evictions++;
	}
}

void ParquetMetadataCache::EvictEntryInternal(const string &key) {
	// Caller must hold lock
	auto entry = cache.find(key);
	if (entry == cache.end()) {
		return;
	}

	// Remove from cache and update memory
	auto memory_size = entry->second->GetMemoryUsage();
	current_memory -= memory_size;
	cache.erase(entry);

	// Remove from LRU structures
	auto lru_iter = lru_map.find(key);
	if (lru_iter != lru_map.end()) {
		lru_list.erase(lru_iter->second);
		lru_map.erase(lru_iter);
	}
}

void ParquetMetadataCache::TouchEntry(const string &key) {
	// Caller must hold lock
	auto lru_iter = lru_map.find(key);
	if (lru_iter == lru_map.end()) {
		return;
	}

	// Move to front of LRU list
	lru_list.erase(lru_iter->second);
	lru_list.push_front(key);
	lru_map[key] = lru_list.begin();
}

} // namespace duckdb

