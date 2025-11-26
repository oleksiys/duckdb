//===----------------------------------------------------------------------===//
//                         DuckDB
//
// parquet_metadata_cache.hpp
//
//
//===----------------------------------------------------------------------===//
#pragma once

#include "duckdb.hpp"
#include "duckdb/common/atomic.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "parquet_file_metadata_cache.hpp"
#include <list>

namespace duckdb {

class ClientContext;
class DatabaseInstance;

//! Memory-bound cache for Parquet file metadata with LRU eviction
//! This is stored as an ObjectCacheEntry in the database's ObjectCache
class ParquetMetadataCache : public ObjectCacheEntry {
public:
	explicit ParquetMetadataCache(idx_t max_memory_bytes);
	~ParquetMetadataCache() override = default;

public:
	//! ObjectCacheEntry implementation
	static string ObjectType() {
		return "parquet_metadata_cache";
	}
	string GetObjectType() override {
		return ObjectType();
	}

public:
	//! Get the ParquetMetadataCache from the database's ObjectCache
	static shared_ptr<ParquetMetadataCache> Get(DatabaseInstance &db);

	//! Get a cached metadata entry by file path
	shared_ptr<ParquetFileMetadataCache> Get(const string &key);

	//! Store a metadata entry in the cache
	void Put(const string &key, const shared_ptr<ParquetFileMetadataCache> &entry);

	//! Delete a specific cache entry
	void Delete(const string &key);

	//! Clear all cache entries
	void Clear();

	//! Get current memory usage
	idx_t GetCurrentMemory() const;

	//! Get maximum memory limit
	idx_t GetMaxMemory() const;

	//! Set maximum memory limit
	void SetMaxMemory(idx_t max_memory_bytes);


	//! Get cache statistics
	struct CacheStats {
		idx_t entry_count;
		idx_t current_memory;
		idx_t max_memory;
		idx_t total_hits;
		idx_t total_misses;
		idx_t total_evictions;
	};
	CacheStats GetStats() const;

private:
	//! Evict entries until there is enough space for the incoming entry
	void EvictIfNeeded(idx_t incoming_size);

	//! Evict a specific entry (caller must hold lock)
	void EvictEntryInternal(const string &key);

	//! Update LRU list on cache hit (caller must hold lock)
	void TouchEntry(const string &key);

private:

	//! Cache storage: file path -> metadata entry
	unordered_map<string, shared_ptr<ParquetFileMetadataCache>> cache;

	//! LRU list: most recently used at front, least recently used at back
	std::list<string> lru_list;

	//! Map from file path to position in LRU list
	unordered_map<string, std::list<string>::iterator> lru_map;

	//! Current memory usage (in bytes)
	atomic<idx_t> current_memory;

	//! Maximum memory limit (in bytes)
	atomic<idx_t> max_memory;


	//! Cache statistics
	atomic<idx_t> total_hits;
	atomic<idx_t> total_misses;
	atomic<idx_t> total_evictions;

	//! Mutex for cache operations
	mutable mutex lock;
};

} // namespace duckdb

