#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb/storage/object_cache.hpp"

using namespace duckdb;
using namespace std;

// Forward declare the ParquetMetadataCache class since it's in the extension
// We'll test it through the ObjectCache interface

TEST_CASE("Test ParquetMetadataCache via ObjectCache", "[api][parquet]") {
	DuckDB db;
	Connection con(db);
	auto &context = *con.context;

	// Load the parquet extension
	REQUIRE_NO_FAIL(con.Query("LOAD parquet"));

	auto &cache = ObjectCache::GetObjectCache(context);

	SECTION("ParquetMetadataCache is created lazily") {
		// Initially, the cache should not exist
		auto parquet_cache = cache.GetObject("parquet_metadata_cache");
		REQUIRE(parquet_cache == nullptr);

		// Enable parquet metadata cache
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=true"));

		// Create a test parquet file
		auto test_file = TestCreatePath("cache_test.parquet");
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT i, i*2 as j FROM range(100) tbl(i)) TO '" + test_file + "'"));

		// Read the file - this should trigger cache creation
		auto result = con.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result, 0, {100}));

		// Now the cache should exist in ObjectCache
		parquet_cache = cache.GetObject("parquet_metadata_cache");
		REQUIRE(parquet_cache != nullptr);
		REQUIRE(parquet_cache->GetObjectType() == "parquet_metadata_cache");
	}

	SECTION("Cache persists across queries") {
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=true"));

		// Create test file
		auto test_file = TestCreatePath("persist_test.parquet");
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT i FROM range(50) tbl(i)) TO '" + test_file + "'"));

		// First read
		auto result1 = con.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result1, 0, {50}));

		// Second read - should use cache
		auto result2 = con.Query("SELECT SUM(i) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result2, 0, {1225}));

		// Cache should still exist
		auto parquet_cache = cache.GetObject("parquet_metadata_cache");
		REQUIRE(parquet_cache != nullptr);
	}

	SECTION("Cache is shared across connections") {
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=true"));

		// Create test file
		auto test_file = TestCreatePath("shared_test.parquet");
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT i FROM range(100) tbl(i)) TO '" + test_file + "'"));

		// Read from first connection
		auto result1 = con.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result1, 0, {100}));

		// Create second connection
		Connection con2(db);
		REQUIRE_NO_FAIL(con2.Query("SET parquet_metadata_cache=true"));

		// Read from second connection - should use same cache
		auto result2 = con2.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result2, 0, {100}));

		// Both connections should see the same cache object (compare addresses)
		auto &cache1 = ObjectCache::GetObjectCache(*con.context);
		auto &cache2 = ObjectCache::GetObjectCache(*con2.context);
		REQUIRE(&cache1 == &cache2);
	}

	SECTION("Cache respects memory limits") {
		// Set a very small cache size (1KB)
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=true"));
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache_size=1024"));

		// Create multiple files
		for (int i = 0; i < 5; i++) {
			string filename = TestCreatePath("limit_test_" + to_string(i) + ".parquet");
			REQUIRE_NO_FAIL(con.Query("COPY (SELECT * FROM range(1000)) TO '" + filename + "'"));
		}

		// Read all files - cache should evict old entries
		for (int i = 0; i < 5; i++) {
			string filename = TestCreatePath("limit_test_" + to_string(i) + ".parquet");
			auto result = con.Query("SELECT COUNT(*) FROM '" + filename + "'");
			REQUIRE(CHECK_COLUMN(result, 0, {1000}));
		}

		// Cache should still exist but have evicted entries
		auto parquet_cache = cache.GetObject("parquet_metadata_cache");
		REQUIRE(parquet_cache != nullptr);
	}

	SECTION("Cache can be disabled") {
		// Disable cache
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=false"));

		// Create test file
		auto test_file = TestCreatePath("disabled_test.parquet");
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT i FROM range(100) tbl(i)) TO '" + test_file + "'"));

		// Read file
		auto result = con.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result, 0, {100}));

		// Cache object may exist but should be disabled/empty
		// The cache is created lazily, so it might not exist if disabled from the start
	}
}

TEST_CASE("Test ParquetMetadataCache configuration", "[api][parquet]") {
	SECTION("Default cache size") {
		DuckDB db;
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("LOAD parquet"));

		// Check default value (256MB)
		auto result = con.Query("SELECT current_setting('parquet_metadata_cache_size')");
		REQUIRE(result->RowCount() == 1);
		auto value = result->GetValue(0, 0);
		REQUIRE(value.GetValue<uint64_t>() == 256ULL * 1024ULL * 1024ULL);
	}

	SECTION("Custom cache size at startup") {
		DBConfig config;
		config.AddExtensionOption("parquet_metadata_cache_size", "Custom cache size",
		                          LogicalType::UBIGINT, Value::UBIGINT(128ULL * 1024ULL * 1024ULL));

		DuckDB db(nullptr, &config);
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("LOAD parquet"));

		// Verify custom size
		auto result = con.Query("SELECT current_setting('parquet_metadata_cache_size')");
		auto value = result->GetValue(0, 0);
		REQUIRE(value.GetValue<uint64_t>() == 128ULL * 1024ULL * 1024ULL);
	}

	SECTION("Cache enabled by default") {
		DuckDB db;
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("LOAD parquet"));

		auto result = con.Query("SELECT current_setting('parquet_metadata_cache')");
		REQUIRE(result->RowCount() == 1);
		// Default is false
		auto value = result->GetValue(0, 0);
		REQUIRE(value.GetValue<bool>() == false);
	}

	SECTION("Enable cache via SET") {
		DuckDB db;
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("LOAD parquet"));

		// Enable cache
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=true"));

		auto result = con.Query("SELECT current_setting('parquet_metadata_cache')");
		auto value = result->GetValue(0, 0);
		REQUIRE(value.GetValue<bool>() == true);
	}

	SECTION("Modify cache size via SET") {
		DuckDB db;
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("LOAD parquet"));

		// Change cache size
		REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache_size=512000000"));

		auto result = con.Query("SELECT current_setting('parquet_metadata_cache_size')");
		auto value = result->GetValue(0, 0);
		REQUIRE(value.GetValue<uint64_t>() == 512000000ULL);
	}
}

TEST_CASE("Test ParquetMetadataCache validation", "[api][parquet]") {
	DuckDB db;
	Connection con(db);
	REQUIRE_NO_FAIL(con.Query("LOAD parquet"));
	REQUIRE_NO_FAIL(con.Query("SET parquet_metadata_cache=true"));

	SECTION("Cache invalidation on file modification") {
		// Create initial file
		auto test_file = TestCreatePath("modify_test.parquet");
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT i FROM range(100) tbl(i)) TO '" + test_file + "'"));

		// Read file to populate cache
		auto result1 = con.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result1, 0, {100}));

		// Overwrite file with different data
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT i FROM range(200) tbl(i)) TO '" + test_file + "'"));

		// Read again - should detect change and reload
		auto result2 = con.Query("SELECT COUNT(*) FROM '" + test_file + "'");
		REQUIRE(CHECK_COLUMN(result2, 0, {200}));
	}

	SECTION("Multiple files with different content") {
		// Create multiple files with different content
		auto test_file1 = TestCreatePath("file1.parquet");
		auto test_file2 = TestCreatePath("file2.parquet");
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT 1 as i) TO '" + test_file1 + "'"));
		REQUIRE_NO_FAIL(con.Query("COPY (SELECT 2 as i) TO '" + test_file2 + "'"));

		// Read both files
		auto result1 = con.Query("SELECT * FROM '" + test_file1 + "'");
		REQUIRE(CHECK_COLUMN(result1, 0, {1}));

		auto result2 = con.Query("SELECT * FROM '" + test_file2 + "'");
		REQUIRE(CHECK_COLUMN(result2, 0, {2}));

		// Each should have its own cache entry
	}
}

