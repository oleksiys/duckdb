#include "parquet_extension.hpp"
#include "parquet_metadata_cache.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

struct ParquetCacheStatsData : GlobalTableFunctionState {
	ParquetCacheStatsData() : finished(false), stats() {
	}

	bool finished;
	ParquetMetadataCache::CacheStats stats;
};

static unique_ptr<FunctionData> ParquetCacheStatsBind(ClientContext &context, TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types, vector<string> &names) {
	names.emplace_back("cache_enabled");
	return_types.emplace_back(LogicalType::BOOLEAN);

	names.emplace_back("max_memory_bytes");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("current_memory_bytes");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("memory_usage_pct");
	return_types.emplace_back(LogicalType::DOUBLE);

	names.emplace_back("entry_count");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("total_hits");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("total_misses");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("hit_rate_pct");
	return_types.emplace_back(LogicalType::DOUBLE);

	names.emplace_back("total_evictions");
	return_types.emplace_back(LogicalType::UBIGINT);

	return nullptr;
}

unique_ptr<GlobalTableFunctionState> ParquetCacheStatsInit(ClientContext &context, TableFunctionInitInput &input) {
	auto result = make_uniq<ParquetCacheStatsData>();

	// Check if cache is enabled for this context
	Value cache_enabled_value = false;
	context.TryGetCurrentSetting("parquet_metadata_cache", cache_enabled_value);
	bool cache_enabled = cache_enabled_value.GetValue<bool>();

	if (cache_enabled) {
		// Get the cache and its stats
		auto &db = DatabaseInstance::GetDatabase(context);
		auto cache = ParquetMetadataCache::Get(db);
		if (cache) {
			result->stats = cache->GetStats();
		}
	}

	return std::move(result);
}

void ParquetCacheStatsImpl(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = data_p.global_state->Cast<ParquetCacheStatsData>();
	if (data.finished) {
		return;
	}

	// Check if cache is enabled
	Value cache_enabled_value = false;
	context.TryGetCurrentSetting("parquet_metadata_cache", cache_enabled_value);
	bool cache_enabled = cache_enabled_value.GetValue<bool>();

	// Return a single row with stats
	idx_t col = 0;

	// cache_enabled (BOOLEAN)
	output.SetValue(col++, 0, Value::BOOLEAN(cache_enabled));

	if (cache_enabled && data.stats.max_memory > 0) {
		// max_memory_bytes (UBIGINT)
		output.SetValue(col++, 0, Value::UBIGINT(data.stats.max_memory));

		// current_memory_bytes (UBIGINT)
		output.SetValue(col++, 0, Value::UBIGINT(data.stats.current_memory));

		// memory_usage_pct (DOUBLE)
		double memory_pct = data.stats.max_memory > 0
		                    ? (static_cast<double>(data.stats.current_memory) / data.stats.max_memory) * 100.0
		                    : 0.0;
		output.SetValue(col++, 0, Value::DOUBLE(memory_pct));

		// entry_count (UBIGINT)
		output.SetValue(col++, 0, Value::UBIGINT(data.stats.entry_count));

		// total_hits (UBIGINT)
		output.SetValue(col++, 0, Value::UBIGINT(data.stats.total_hits));

		// total_misses (UBIGINT)
		output.SetValue(col++, 0, Value::UBIGINT(data.stats.total_misses));

		// hit_rate_pct (DOUBLE)
		idx_t total_accesses = data.stats.total_hits + data.stats.total_misses;
		double hit_rate = total_accesses > 0
		                  ? (static_cast<double>(data.stats.total_hits) / total_accesses) * 100.0
		                  : 0.0;
		output.SetValue(col++, 0, Value::DOUBLE(hit_rate));

		// total_evictions (UBIGINT)
		output.SetValue(col++, 0, Value::UBIGINT(data.stats.total_evictions));
	} else {
		// Cache disabled or not initialized - return NULL for stats
		output.SetValue(col++, 0, Value(LogicalType::UBIGINT));  // max_memory_bytes
		output.SetValue(col++, 0, Value(LogicalType::UBIGINT));  // current_memory_bytes
		output.SetValue(col++, 0, Value(LogicalType::DOUBLE));   // memory_usage_pct
		output.SetValue(col++, 0, Value(LogicalType::UBIGINT));  // entry_count
		output.SetValue(col++, 0, Value(LogicalType::UBIGINT));  // total_hits
		output.SetValue(col++, 0, Value(LogicalType::UBIGINT));  // total_misses
		output.SetValue(col++, 0, Value(LogicalType::DOUBLE));   // hit_rate_pct
		output.SetValue(col++, 0, Value(LogicalType::UBIGINT));  // total_evictions
	}

	output.SetCardinality(1);
	data.finished = true;
}

void RegisterParquetCacheStatsFunction(ExtensionLoader &loader) {
	TableFunction cache_stats_fun("parquet_cache_stats", {}, ParquetCacheStatsImpl, ParquetCacheStatsBind,
	                              ParquetCacheStatsInit);
	loader.RegisterFunction(cache_stats_fun);
}

} // namespace duckdb
