//===----------------------------------------------------------------------===//
//                         DuckDB
//
// parquet_file_metadata_cache.hpp
//
//
//===----------------------------------------------------------------------===//
#pragma once

#include "duckdb.hpp"
#include "geo_parquet.hpp"
#include "parquet_types.h"

namespace duckdb {
struct CachingFileHandle;

enum class ParquetCacheValidity { VALID, INVALID, UNKNOWN };

class ParquetFileMetadataCache {
public:
	ParquetFileMetadataCache(unique_ptr<duckdb_parquet::FileMetaData> file_metadata, CachingFileHandle &handle,
	                         unique_ptr<GeoParquetFileMetadata> geo_metadata, idx_t footer_size);
	~ParquetFileMetadataCache() = default;

	//! Parquet file metadata
	unique_ptr<const duckdb_parquet::FileMetaData> metadata;

	//! GeoParquet metadata
	unique_ptr<GeoParquetFileMetadata> geo_metadata;

	//! Parquet footer size
	idx_t footer_size;

public:
	bool IsValid(CachingFileHandle &new_handle) const;
	//! Check if a cache entry is valid based ONLY on the OpenFileInfo (without doing any file system calls)
	//! If the OpenFileInfo does not have enough information this can return UNKNOWN
	ParquetCacheValidity IsValid(const OpenFileInfo &info) const;

	//! Estimate memory usage of this cache entry
	idx_t GetMemoryUsage() const;

private:
	//! Estimate memory usage (conservative approach with 1.2x multiplier)
	idx_t EstimateMemoryUsage() const;

	//! Estimate memory usage of GeoParquet metadata
	idx_t EstimateGeoMetadataSize() const;

private:
	bool validate;
	timestamp_t last_modified;
	string version_tag;

	//! Cached memory size (computed once)
	mutable idx_t cached_memory_size;
};

} // namespace duckdb
