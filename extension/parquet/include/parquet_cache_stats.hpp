//===----------------------------------------------------------------------===//
//                         DuckDB
//
// parquet_cache_stats.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

namespace duckdb {

class ExtensionLoader;

void RegisterParquetCacheStatsFunction(ExtensionLoader &loader);

} // namespace duckdb
