# Parquet Metadata Cache Tests

This directory contains comprehensive tests for the Parquet metadata cache feature, which allows DuckDB to cache Parquet file metadata to improve performance when reading the same files multiple times.

## Test Files

### C++ Unit Tests

**test_parquet_metadata_cache.cpp**
- Tests the ParquetMetadataCache integration with ObjectCache
- Verifies lazy cache creation
- Tests cache persistence across queries
- Tests cache sharing across connections
- Tests memory limit enforcement
- Tests cache enable/disable functionality
- Tests configuration options

### SQL Functional Tests

**parquet_metadata_cache.test**
- Basic cache functionality
- Multiple file caching
- Cache enable/disable behavior
- Cache size configuration
- Different data types caching
- Compressed data caching
- NULL values handling
- Large files with multiple row groups
- Filtered queries with metadata statistics
- Wildcard file patterns
- Repeated query performance

**parquet_metadata_cache_memory.test**
- Memory management and LRU eviction
- Small cache size forcing eviction
- Large cache size handling
- LRU (Least Recently Used) behavior
- Minimum and maximum cache sizes
- Complex schema metadata
- Dynamic cache size modification

## Running the Tests

### C++ Unit Tests

The C++ tests are compiled into the main test runner. To run them:

```bash
cd cmake-build-debug
./test/unittest "[parquet]"
```

Or run specific tests:

```bash
./test/unittest "Test ParquetMetadataCache via ObjectCache"
./test/unittest "Test ParquetMetadataCache configuration"
./test/unittest "Test ParquetMetadataCache validation"
```

### SQL Tests

SQL tests can be run using the test runner:

```bash
# Run all parquet tests
make test_parquet

# Or run specific tests
build/release/test/unittest test/parquet/parquet_metadata_cache.test
build/release/test/unittest test/parquet/parquet_metadata_cache_memory.test
```

## Architecture Overview

The Parquet metadata cache implementation uses a **two-level cache architecture**:

```
ObjectCache (core)
    └── "parquet_metadata_cache" → ParquetMetadataCache (extension, implements ObjectCacheEntry)
            └── Internal LRU cache: map<file_path, ParquetFileMetadataCache>
                    ├── "file1.parquet" → ParquetFileMetadataCache
                    ├── "file2.parquet" → ParquetFileMetadataCache
                    └── "file3.parquet" → ParquetFileMetadataCache
```

### Key Benefits:
1. **No core changes needed** - Uses existing `ObjectCache` infrastructure
2. **No parquet knowledge in core** - Core only sees a generic `ObjectCacheEntry`
3. **Centralized lifetime management** - ObjectCache handles cleanup
4. **Memory management in extension** - `ParquetMetadataCache` handles LRU eviction
5. **Configuration stays in extension** - Extension-specific settings

## Configuration Options

### parquet_metadata_cache (BOOLEAN)
- **Default**: `false`
- **Description**: Enable or disable the Parquet metadata cache
- **Usage**: `SET parquet_metadata_cache=true`

### parquet_metadata_cache_size (UBIGINT)
- **Default**: `268435456` (256 MB)
- **Description**: Maximum memory size in bytes for the Parquet metadata cache
- **Usage**: `SET parquet_metadata_cache_size=1073741824` (1 GB)

## Test Coverage

### Core Functionality
- ✅ Cache creation and initialization
- ✅ Cache entry storage and retrieval
- ✅ Cache persistence across queries
- ✅ Cache sharing across connections
- ✅ Cache invalidation on file modification

### Memory Management
- ✅ LRU eviction when memory limit exceeded
- ✅ Memory usage estimation
- ✅ Dynamic cache size adjustment
- ✅ Cache behavior with various memory limits

### Configuration
- ✅ Default configuration values
- ✅ Custom configuration at startup
- ✅ Runtime configuration changes
- ✅ Enable/disable functionality

### Data Types & Scenarios
- ✅ Various data types (integers, strings, dates, booleans, etc.)
- ✅ NULL values
- ✅ Compressed data (ZSTD, GZIP, etc.)
- ✅ Large files with multiple row groups
- ✅ Multiple files and wildcard patterns
- ✅ Complex schemas with nested types

### Performance
- ✅ Repeated query performance
- ✅ Filter pushdown with cached statistics
- ✅ Multi-file scans

## Future Enhancements

Potential areas for additional testing:

1. **Concurrency**: Multi-threaded access to cache
2. **Cloud Storage**: S3/HTTP file caching and validation
3. **Statistics**: Detailed cache hit/miss metrics
4. **Encryption**: Encrypted Parquet file metadata caching
5. **Partitioned Datasets**: Hive-style partitioning with cache
6. **Schema Evolution**: Handling schema changes in cached files

## Contributing

When adding new cache features, please:

1. Add corresponding C++ unit tests in `test_parquet_metadata_cache.cpp`
2. Add SQL functional tests in `parquet_metadata_cache.test` or create a new test file
3. Document the feature in this README
4. Ensure all tests pass before submitting

## References

- [ObjectCache Implementation](/src/include/duckdb/storage/object_cache.hpp)
- [ParquetMetadataCache Header](/extension/parquet/include/parquet_metadata_cache.hpp)
- [ParquetFileMetadataCache Header](/extension/parquet/include/parquet_file_metadata_cache.hpp)

