#include "parquet_file_metadata_cache.hpp"
#include "duckdb/storage/external_file_cache.hpp"
#include "duckdb/storage/caching_file_system.hpp"

namespace duckdb {

ParquetFileMetadataCache::ParquetFileMetadataCache(unique_ptr<duckdb_parquet::FileMetaData> file_metadata,
                                                   CachingFileHandle &handle,
                                                   unique_ptr<GeoParquetFileMetadata> geo_metadata, idx_t footer_size)
    : metadata(std::move(file_metadata)), geo_metadata(std::move(geo_metadata)), footer_size(footer_size),
      cached_memory_size(0) {
}

bool ParquetFileMetadataCache::IsValid(CachingFileHandle &new_handle) const {
	return ExternalFileCache::IsValid(validate, version_tag, last_modified, new_handle.GetVersionTag(),
	                                  new_handle.GetLastModifiedTime());
}

ParquetCacheValidity ParquetFileMetadataCache::IsValid(const OpenFileInfo &info) const {
	if (!info.extended_info) {
		return ParquetCacheValidity::UNKNOWN;
	}
	auto &open_options = info.extended_info->options;
	const auto validate_entry = open_options.find("validate_external_file_cache");
	if (validate_entry != open_options.end()) {
		// check if always valid - if so just return valid
		if (BooleanValue::Get(validate_entry->second)) {
			return ParquetCacheValidity::VALID;
		}
	}
	const auto lm_entry = open_options.find("last_modified");
	if (lm_entry == open_options.end()) {
		return ParquetCacheValidity::UNKNOWN;
	}
	auto new_last_modified = lm_entry->second.GetValue<timestamp_t>();
	string new_etag;
	const auto etag_entry = open_options.find("etag");
	if (etag_entry != open_options.end()) {
		new_etag = StringValue::Get(etag_entry->second);
	}
	if (ExternalFileCache::IsValid(false, version_tag, last_modified, new_etag, new_last_modified)) {
		return ParquetCacheValidity::VALID;
	}
	return ParquetCacheValidity::INVALID;
}

idx_t ParquetFileMetadataCache::GetMemoryUsage() const {
	if (cached_memory_size == 0) {
		cached_memory_size = EstimateMemoryUsage();
	}
	return cached_memory_size;
}

idx_t ParquetFileMetadataCache::EstimateMemoryUsage() const {
	idx_t total = 0;

	// 1. Base object overhead
	total += sizeof(ParquetFileMetadataCache);

	if (!metadata) {
		// Apply 1.2x safety multiplier and return
		return static_cast<idx_t>(total * 1.2);
	}

	// 2. FileMetaData fixed overhead
	total += sizeof(duckdb_parquet::FileMetaData);

	// 3. Schema elements (vector overhead + elements)
	// Each SchemaElement typically contains strings and nested structures
	// Conservative estimate: 256 bytes per schema element
	total += metadata->schema.size() * (sizeof(duckdb_parquet::SchemaElement) + 256);

	// 4. Row groups (dominant factor)
	// Base size per row group plus estimated column chunks
	for (const auto &row_group : metadata->row_groups) {
		total += sizeof(duckdb_parquet::RowGroup);

		// Each column chunk contains metadata about compression, encoding, and statistics
		// Conservative estimate: 512 bytes per column chunk (includes Statistics objects)
		total += row_group.columns.size() * (sizeof(duckdb_parquet::ColumnChunk) + 512);

		// Account for sorting columns if present
		if (row_group.__isset.sorting_columns) {
			total += row_group.sorting_columns.size() * sizeof(duckdb_parquet::SortingColumn);
		}
	}

	// 5. Key-value metadata
	for (const auto &kv : metadata->key_value_metadata) {
		total += sizeof(duckdb_parquet::KeyValue);
		total += kv.key.size();
		if (kv.__isset.value) {
			total += kv.value.size();
		}
	}

	// 6. Strings
	total += metadata->created_by.size();
	if (metadata->__isset.footer_signing_key_metadata) {
		total += metadata->footer_signing_key_metadata.size();
	}

	// 7. Column orders
	if (metadata->__isset.column_orders) {
		total += metadata->column_orders.size() * sizeof(duckdb_parquet::ColumnOrder);
	}

	// 8. Version tag and validation metadata
	total += version_tag.size();

	// 9. GeoParquet metadata
	if (geo_metadata) {
		total += EstimateGeoMetadataSize();
	}

	// Apply 1.2x safety multiplier to account for:
	// - Allocator overhead (8-16 bytes per allocation)
	// - String internal overhead (SSO, capacity vs size)
	// - Vector capacity overhead
	// - Nested structures we may have underestimated
	return static_cast<idx_t>(total * 1.2);
}

idx_t ParquetFileMetadataCache::EstimateGeoMetadataSize() const {
	if (!geo_metadata) {
		return 0;
	}

	idx_t total = sizeof(GeoParquetFileMetadata);

	// GeoParquet metadata is typically JSON stored as strings
	// Estimate based on the number of columns and their properties
	// Conservative estimate: 1KB base + 500 bytes per geo column
	total += 1024;

	// This is a rough estimate - GeoParquet metadata is usually small (< 50KB)
	// The actual implementation would need to traverse the metadata structure

	return total;
}

} // namespace duckdb
