// File: deps/v8/src/wasm/baseline/liftoff-memory-tracker.cc
#include "liftoff-memory-tracker.h"

namespace v8::internal::wasm {
namespace liftoff {

// === STATIC MEMBER DEFINITIONS ===

std::map<uint64_t, OffsetPattern> MemoryTracker::offset_patterns_;
std::vector<WasmMemoryOperation> MemoryTracker::wasm_operations_;
std::unordered_map<uintptr_t, MemoryTracker::EnhancedMemoryAccessInfo>
    MemoryTracker::memory_accesses_;
std::map<uint32_t, std::vector<WasmMemoryOperation>>
    MemoryTracker::function_operations_;
std::string MemoryTracker::current_function_name_ = "";
uint32_t MemoryTracker::current_function_index_ = 0;
bool MemoryTracker::detailed_logging_enabled_ = false;

// File naming and module tracking (similar to CallTracer)
std::string MemoryTracker::trace_file_prefix_ = "wasm_memory";
std::string MemoryTracker::current_module_name_ = "";

// === PRIVATE HELPER FUNCTIONS ===

uint64_t MemoryTracker::GetCurrentTimestamp() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

bool MemoryTracker::ShouldTrackMemory() {
  static bool checked = false;
  static bool should_track = false;
  if (!checked) {
    const char* env = std::getenv("NODE_WASM_MEMORY_HOOKS");
    should_track = env && std::strcmp(env, "1") == 0;

    const char* detailed_env = std::getenv("NODE_WASM_MEMORY_DETAILED");
    detailed_logging_enabled_ =
        detailed_env && std::strcmp(detailed_env, "1") == 0;

    checked = true;
    if (should_track) {
      std::cout
          << "[MemHook] Memory tracking enabled via NODE_WASM_MEMORY_HOOKS=1"
          << std::endl;
      if (detailed_logging_enabled_) {
        std::cout << "[MemHook] Detailed logging enabled via "
                     "NODE_WASM_MEMORY_DETAILED=1"
                  << std::endl;
      }

      // Check for custom memory trace file prefix from environment
      const char* prefix_env = std::getenv("NODE_WASM_MEMORY_PREFIX");
      if (prefix_env) {
        trace_file_prefix_ = std::string(prefix_env);
        std::cout << "[MemHook] Using custom memory trace prefix from env: "
                  << trace_file_prefix_ << std::endl;
      }
    }
  }
  return should_track;
}

// === FILE NAMING AND MODULE TRACKING ===

void MemoryTracker::SetTraceFilePrefix(const std::string& prefix) {
  trace_file_prefix_ = prefix;
}

std::string MemoryTracker::GetTraceFilePrefix() { return trace_file_prefix_; }

void MemoryTracker::SetCurrentModule(const std::string& module_name) {
  current_module_name_ = module_name;
  if (!module_name.empty() && trace_file_prefix_ == "wasm_memory") {
    // Auto-update trace file prefix based on module name
    trace_file_prefix_ = module_name + "_memory";
  }
}

std::string MemoryTracker::GetCurrentModule() { return current_module_name_; }

// Generate filename with proper prefix
std::string MemoryTracker::GenerateFilename(
    const std::string& base_name, const std::string& extension,
    const std::string& custom_filename) {
  if (!custom_filename.empty()) {
    return custom_filename;
  }
  return trace_file_prefix_ + "_" + base_name + "." + extension;
}

// === INSTRUCTION NAME MAPPING ===

std::string MemoryTracker::GetLoadInstructionName(int load_type_value) {
  switch (load_type_value) {
    case 0:
      return "i32.load";
    case 1:
      return "i64.load";
    case 2:
      return "f32.load";
    case 3:
      return "f64.load";
    case 4:
      return "i32.load8_s";
    case 5:
      return "i32.load8_u";
    case 6:
      return "i32.load16_s";
    case 7:
      return "i32.load16_u";
    case 8:
      return "i64.load8_s";
    case 9:
      return "i64.load8_u";
    case 10:
      return "i64.load16_s";
    case 11:
      return "i64.load16_u";
    case 12:
      return "i64.load32_s";
    case 13:
      return "i64.load32_u";
    case 14:
      return "f32.load_f16";
    default:
      return "unknown_load";
  }
}

std::string MemoryTracker::GetStoreInstructionName(int store_type_value) {
  switch (store_type_value) {
    case 0:
      return "i32.store";
    case 1:
      return "i64.store";
    case 2:
      return "f32.store";
    case 3:
      return "f64.store";
    case 4:
      return "i32.store8";
    case 5:
      return "i32.store16";
    case 6:
      return "i64.store8";
    case 7:
      return "i64.store16";
    case 8:
      return "i64.store32";
    case 9:
      return "f32.store_f16";
    default:
      return "unknown_store";
  }
}

// === MAIN TRACKING FUNCTIONS ===

void MemoryTracker::TrackWasmLoad(uint64_t immediate_offset,
                                  uint32_t access_size, uint32_t function_index,
                                  uint32_t bytecode_offset, bool is_static,
                                  uint64_t static_index) {
  if (!ShouldTrackMemory()) return;

  // Update offset patterns
  auto& pattern = offset_patterns_[immediate_offset];
  pattern.immediate_offset = immediate_offset;
  pattern.frequency++;
  pattern.load_count++;
  pattern.access_sizes.push_back(access_size);
  pattern.functions_using.push_back(function_index);
  pattern.most_common_instruction = "load_" + std::to_string(access_size) + "b";

  if (is_static) {
    pattern.static_access_count++;
  } else {
    pattern.dynamic_access_count++;
  }

  // Log the operation
  std::cout << "[MemHook] Load: offset=" << immediate_offset
            << " size=" << access_size << " func=" << function_index
            << " pos=" << bytecode_offset;
  if (is_static) {
    std::cout << " static_idx=" << static_index
              << " effective=" << (static_index + immediate_offset);
  }
  std::cout << std::endl;

  // Store detailed operation if enabled
  if (detailed_logging_enabled_) {
    WasmMemoryOperation op = {"Load",
                              immediate_offset,
                              static_index,
                              is_static ? (static_index + immediate_offset) : 0,
                              access_size,
                              0,  // value filled in later
                              function_index,
                              bytecode_offset,
                              "load_" + std::to_string(access_size) + "b",
                              GetCurrentTimestamp(),
                              is_static,
                              ""};
    wasm_operations_.push_back(op);
    function_operations_[function_index].push_back(op);
  }
}

void MemoryTracker::TrackWasmStore(uint64_t immediate_offset,
                                   uint32_t access_size,
                                   uint32_t function_index,
                                   uint32_t bytecode_offset, bool is_static,
                                   uint64_t static_index) {
  if (!ShouldTrackMemory()) return;

  // Update offset patterns
  auto& pattern = offset_patterns_[immediate_offset];
  pattern.immediate_offset = immediate_offset;
  pattern.frequency++;
  pattern.store_count++;
  pattern.access_sizes.push_back(access_size);
  pattern.functions_using.push_back(function_index);
  pattern.most_common_instruction =
      "store_" + std::to_string(access_size) + "b";

  if (is_static) {
    pattern.static_access_count++;
  } else {
    pattern.dynamic_access_count++;
  }

  // Log the operation
  std::cout << "[MemHook] Store: offset=" << immediate_offset
            << " size=" << access_size << " func=" << function_index
            << " pos=" << bytecode_offset;
  if (is_static) {
    std::cout << " static_idx=" << static_index
              << " effective=" << (static_index + immediate_offset);
  }
  std::cout << std::endl;

  // Store detailed operation if enabled
  if (detailed_logging_enabled_) {
    WasmMemoryOperation op = {"Store",
                              immediate_offset,
                              static_index,
                              is_static ? (static_index + immediate_offset) : 0,
                              access_size,
                              0,  // value filled in later
                              function_index,
                              bytecode_offset,
                              "store_" + std::to_string(access_size) + "b",
                              GetCurrentTimestamp(),
                              is_static,
                              ""};
    wasm_operations_.push_back(op);
    function_operations_[function_index].push_back(op);
  }
}

// === ADVANCED TRACKING ===

void MemoryTracker::TrackCompleteMemoryAccess(
    const WasmMemoryOperation& operation) {
  if (!ShouldTrackMemory()) return;

  wasm_operations_.push_back(operation);
  function_operations_[operation.function_index].push_back(operation);

  auto& pattern = offset_patterns_[operation.immediate_offset];
  pattern.immediate_offset = operation.immediate_offset;
  pattern.frequency++;
  pattern.access_sizes.push_back(operation.access_size);
  pattern.functions_using.push_back(operation.function_index);
  pattern.most_common_instruction = operation.instruction_name;

  if (operation.operation_type == "Load") {
    pattern.load_count++;
  } else {
    pattern.store_count++;
  }

  if (operation.is_static_access) {
    pattern.static_access_count++;
  } else {
    pattern.dynamic_access_count++;
  }

  std::cout << "[MemHook] Complete " << operation.operation_type << ": "
            << operation.instruction_name
            << " offset=" << operation.immediate_offset
            << " size=" << operation.access_size
            << " func=" << operation.function_index << std::endl;
}

// === LEGACY COMPATIBILITY ===

void MemoryTracker::LogMemoryAccess(uintptr_t address, const std::string& type,
                                    int value) {
  if (!ShouldTrackMemory()) return;
  std::cout << "[MemHook] { \"addr\": \"0x" << std::hex << address
            << "\", \"type\": \"" << type << "\", \"value\": " << std::dec
            << value << " }" << std::endl;
}

void MemoryTracker::TrackMemoryLoad(uintptr_t effective_addr,
                                    int loaded_value) {
  if (!ShouldTrackMemory()) return;
  auto& entry = memory_accesses_[effective_addr];
  entry.num_accesses++;
  if (!entry.constant_value.has_value()) {
    entry.constant_value = loaded_value;
  } else if (*entry.constant_value != loaded_value) {
    entry.constant = false;
  }
  LogMemoryAccess(effective_addr, "Load", loaded_value);
}

void MemoryTracker::TrackMemoryStore(uintptr_t effective_addr,
                                     int stored_value) {
  if (!ShouldTrackMemory()) return;
  auto& entry = memory_accesses_[effective_addr];
  entry.num_accesses++;
  if (!entry.constant_value.has_value()) {
    entry.constant_value = stored_value;
  } else if (*entry.constant_value != stored_value) {
    entry.constant = false;
  }
  LogMemoryAccess(effective_addr, "Store", stored_value);
}

void MemoryTracker::TrackMemoryLoadDetailed(uintptr_t base_addr, int64_t offset,
                                            uintptr_t effective_addr,
                                            uint32_t size, int64_t loaded_value,
                                            uint32_t instr_offset,
                                            const std::string& reg_info) {
  if (!ShouldTrackMemory()) return;

  auto& entry = memory_accesses_[effective_addr];
  entry.num_accesses++;
  entry.total_bytes_accessed += size;
  entry.offset_frequency[offset]++;
  entry.size_frequency[size]++;

  uint64_t current_time = GetCurrentTimestamp();
  if (entry.first_access_time == 0) {
    entry.first_access_time = current_time;
  }
  entry.last_access_time = current_time;

  if (!entry.constant_value.has_value()) {
    entry.constant_value = loaded_value;
  } else if (*entry.constant_value != loaded_value) {
    entry.constant = false;
  }

  std::cout << "[MemHook] DetailedLoad: base=0x" << std::hex << base_addr
            << " offset=" << std::dec << offset << " effective=0x" << std::hex
            << effective_addr << std::dec << " size=" << size
            << " value=" << loaded_value << std::endl;
}

void MemoryTracker::TrackMemoryStoreDetailed(
    uintptr_t base_addr, int64_t offset, uintptr_t effective_addr,
    uint32_t size, int64_t stored_value, uint32_t instr_offset,
    const std::string& reg_info) {
  if (!ShouldTrackMemory()) return;

  auto& entry = memory_accesses_[effective_addr];
  entry.num_accesses++;
  entry.total_bytes_accessed += size;
  entry.offset_frequency[offset]++;
  entry.size_frequency[size]++;

  uint64_t current_time = GetCurrentTimestamp();
  if (entry.first_access_time == 0) {
    entry.first_access_time = current_time;
  }
  entry.last_access_time = current_time;

  if (!entry.constant_value.has_value()) {
    entry.constant_value = stored_value;
  } else if (*entry.constant_value != stored_value) {
    entry.constant = false;
  }

  std::cout << "[MemHook] DetailedStore: base=0x" << std::hex << base_addr
            << " offset=" << std::dec << offset << " effective=0x" << std::hex
            << effective_addr << std::dec << " size=" << size
            << " value=" << stored_value << std::endl;
}

// === ANALYSIS FUNCTIONS ===

void MemoryTracker::AnalyzeOffsetPatterns() {
  if (!ShouldTrackMemory()) return;

  std::cout << "[MemHook] === Offset Pattern Analysis ===" << std::endl;

  std::vector<std::pair<uint64_t, OffsetPattern*>> sorted_patterns;
  for (auto& [offset, pattern] : offset_patterns_) {
    sorted_patterns.emplace_back(offset, &pattern);
  }

  std::sort(sorted_patterns.begin(), sorted_patterns.end(),
            [](const auto& a, const auto& b) {
              return a.second->frequency > b.second->frequency;
            });

  std::cout << "Top Offset Patterns:" << std::endl;
  for (size_t i = 0; i < std::min((size_t)10, sorted_patterns.size()); ++i) {
    const auto& [offset, pattern] = sorted_patterns[i];
    std::set<uint32_t> unique_funcs(pattern->functions_using.begin(),
                                    pattern->functions_using.end());

    std::cout << "  Offset " << offset << ": " << pattern->frequency << " uses"
              << " (loads:" << pattern->load_count
              << ", stores:" << pattern->store_count << ")"
              << " across " << unique_funcs.size() << " functions"
              << " (static:" << pattern->static_access_count
              << ", dynamic:" << pattern->dynamic_access_count << ")"
              << std::endl;
  }
}

void MemoryTracker::DumpOffsetStatistics(const std::string& filename) {
  if (!ShouldTrackMemory()) return;

  std::string actual_filename =
      GenerateFilename("offset_analysis", "json", filename);
  std::ofstream file(actual_filename);
  if (!file.is_open()) {
    std::cout << "[MemHook] Failed to open " << actual_filename
              << " for writing" << std::endl;
    return;
  }

  file << "{\n  \"metadata\": {\n";
  file << "    \"module\": \"" << current_module_name_ << "\",\n";
  file << "    \"trace_prefix\": \"" << trace_file_prefix_ << "\",\n";
  file << "    \"timestamp\": " << GetCurrentTimestamp() << "\n";
  file << "  },\n";
  file << "  \"offset_patterns\": {\n";
  bool first = true;
  for (const auto& [offset, pattern] : offset_patterns_) {
    if (!first) file << ",\n";

    file << "    \"" << offset << "\": {\n";
    file << "      \"frequency\": " << pattern.frequency << ",\n";
    file << "      \"load_count\": " << pattern.load_count << ",\n";
    file << "      \"store_count\": " << pattern.store_count << ",\n";
    file << "      \"static_access_count\": " << pattern.static_access_count
         << ",\n";
    file << "      \"dynamic_access_count\": " << pattern.dynamic_access_count
         << ",\n";
    file << "      \"instruction\": \"" << pattern.most_common_instruction
         << "\",\n";

    std::set<uint32_t> unique_funcs(pattern.functions_using.begin(),
                                    pattern.functions_using.end());
    file << "      \"functions_using\": [";
    bool first_func = true;
    for (uint32_t func : unique_funcs) {
      if (!first_func) file << ", ";
      file << func;
      first_func = false;
    }
    file << "],\n";

    std::map<uint32_t, uint32_t> size_freq;
    for (uint32_t size : pattern.access_sizes) {
      size_freq[size]++;
    }
    file << "      \"size_distribution\": {";
    bool first_size = true;
    for (const auto& [size, freq] : size_freq) {
      if (!first_size) file << ", ";
      file << "\"" << size << "\": " << freq;
      first_size = false;
    }
    file << "}\n";
    file << "    }";
    first = false;
  }

  file << "\n  },\n";
  file << "  \"summary\": {\n";
  file << "    \"total_unique_offsets\": " << offset_patterns_.size() << ",\n";
  file << "    \"total_operations\": " << wasm_operations_.size() << ",\n";
  file << "    \"functions_tracked\": " << function_operations_.size() << "\n";
  file << "  }\n";
  file << "}\n";

  file.close();
  std::cout << "[MemHook] Offset statistics written to " << actual_filename
            << std::endl;
}

void MemoryTracker::PrintTopOffsets(uint32_t top_n) {
  if (!ShouldTrackMemory()) return;

  std::vector<std::pair<uint64_t, uint32_t>> offset_usage;
  for (const auto& [offset, pattern] : offset_patterns_) {
    offset_usage.emplace_back(offset, pattern.frequency);
  }

  std::sort(offset_usage.begin(), offset_usage.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::cout << "[MemHook] Top "
            << std::min(top_n, (uint32_t)offset_usage.size())
            << " Offsets:" << std::endl;
  for (size_t i = 0; i < std::min((size_t)top_n, offset_usage.size()); ++i) {
    std::cout << "  " << (i + 1) << ". Offset " << offset_usage[i].first << ": "
              << offset_usage[i].second << " uses" << std::endl;
  }
}

void MemoryTracker::FindOffsetClusters() {
  if (!ShouldTrackMemory()) return;

  std::vector<uint64_t> offsets;
  for (const auto& [offset, pattern] : offset_patterns_) {
    offsets.push_back(offset);
  }

  std::sort(offsets.begin(), offsets.end());

  std::cout << "[MemHook] === Offset Clusters ===" << std::endl;

  std::vector<std::vector<uint64_t>> clusters;
  std::vector<uint64_t> current_cluster;

  for (size_t i = 0; i < offsets.size(); ++i) {
    if (current_cluster.empty() || offsets[i] - current_cluster.back() <= 64) {
      current_cluster.push_back(offsets[i]);
    } else {
      if (current_cluster.size() > 1) {
        clusters.push_back(current_cluster);
      }
      current_cluster = {offsets[i]};
    }
  }

  if (current_cluster.size() > 1) {
    clusters.push_back(current_cluster);
  }

  for (size_t i = 0; i < clusters.size(); ++i) {
    const auto& cluster = clusters[i];
    std::cout << "Cluster " << i << " (range " << cluster.front() << "-"
              << cluster.back() << "): ";
    for (uint64_t offset : cluster) {
      std::cout << offset << " ";
    }
    std::cout << std::endl;
  }
}

void MemoryTracker::DumpMemoryAccessStats(const std::string& filename) {
  if (!ShouldTrackMemory()) return;

  if (memory_accesses_.empty()) {
    std::cout << "[MemHook] No legacy memory accesses to dump" << std::endl;
    return;
  }

  std::string actual_filename =
      GenerateFilename("memory_access_log", "json", filename);
  std::ofstream file(actual_filename);
  if (!file.is_open()) {
    std::cout << "[MemHook] Failed to open " << actual_filename
              << " for writing" << std::endl;
    return;
  }

  file << "{\n  \"metadata\": {\n";
  file << "    \"module\": \"" << current_module_name_ << "\",\n";
  file << "    \"trace_prefix\": \"" << trace_file_prefix_ << "\",\n";
  file << "    \"timestamp\": " << GetCurrentTimestamp() << "\n";
  file << "  },\n";
  file << "  \"memory_accesses\": {\n";
  bool first = true;
  for (const auto& [address, info] : memory_accesses_) {
    if (!first) file << ",\n";
    file << "    \"0x" << std::hex << address << std::dec << "\": {"
         << "\"num_accesses\": " << info.num_accesses << ", "
         << "\"constant\": " << (info.constant ? "true" : "false");
    if (info.constant_value.has_value()) {
      file << ", \"constant_value\": " << info.constant_value.value();
    }
    file << "}";
    first = false;
  }
  file << "\n  },\n";
  file << "  \"total_addresses\": " << memory_accesses_.size() << "\n";
  file << "}\n";
  file.close();

  std::cout << "[MemHook] Legacy memory access stats written to "
            << actual_filename << std::endl;
}

void MemoryTracker::GenerateMemoryReport(const std::string& filename) {
  if (!ShouldTrackMemory()) return;

  std::cout << "[MemHook] Generating memory report..." << std::endl;

  DumpOffsetStatistics("");
  DumpMemoryAccessStats("");

  std::string actual_filename =
      GenerateFilename("memory_report", "txt", filename);
  std::ofstream report(actual_filename);
  if (report.is_open()) {
    report << "=== WASM Memory Access Report ===\n";
    report << "Module: " << current_module_name_ << "\n";
    report << "Trace Prefix: " << trace_file_prefix_ << "\n";
    report << "Timestamp: " << GetCurrentTimestamp() << "\n\n";

    std::streambuf* orig = std::cout.rdbuf();
    std::cout.rdbuf(report.rdbuf());

    AnalyzeAccessPatterns();
    std::cout << "\n";
    FindOffsetClusters();

    std::cout.rdbuf(orig);
    report.close();

    std::cout << "[MemHook] Report written to " << actual_filename << std::endl;
  }
}

// === EXPORT FUNCTIONS ===

void MemoryTracker::ExportOffsetAnalysisToJSON(const std::string& filename) {
  DumpOffsetStatistics(filename);
}

void MemoryTracker::ExportMemoryStatsToJSON(const std::string& filename) {
  DumpMemoryAccessStats(filename);
}

void MemoryTracker::ExportToCSV(const std::string& filename) {
  if (!ShouldTrackMemory()) return;

  std::string actual_filename =
      GenerateFilename("memory_analysis", "csv", filename);
  std::ofstream file(actual_filename);
  if (!file.is_open()) {
    std::cout << "[MemHook] Failed to open " << actual_filename
              << " for writing" << std::endl;
    return;
  }

  file << "offset,frequency,load_count,store_count,static_count,dynamic_count,"
          "unique_functions,common_sizes\n";

  for (const auto& [offset, pattern] : offset_patterns_) {
    std::set<uint32_t> unique_funcs(pattern.functions_using.begin(),
                                    pattern.functions_using.end());
    std::map<uint32_t, uint32_t> size_freq;
    for (uint32_t size : pattern.access_sizes) {
      size_freq[size]++;
    }

    std::string common_sizes;
    for (const auto& [size, freq] : size_freq) {
      if (!common_sizes.empty()) common_sizes += ";";
      common_sizes += std::to_string(size) + ":" + std::to_string(freq);
    }

    file << offset << "," << pattern.frequency << "," << pattern.load_count
         << "," << pattern.store_count << "," << pattern.static_access_count
         << "," << pattern.dynamic_access_count << "," << unique_funcs.size()
         << ","
         << "\"" << common_sizes << "\"\n";
  }

  file.close();
  std::cout << "[MemHook] CSV export written to " << actual_filename
            << std::endl;
}

void MemoryTracker::ExportToText(const std::string& filename) {
  GenerateMemoryReport(filename);
}

void MemoryTracker::PrintStatistics() {
  if (!ShouldTrackMemory()) return;

  std::cout << "[MemHook] === Memory Tracking Statistics ===" << std::endl;
  std::cout << "Module: "
            << (current_module_name_.empty() ? "unknown" : current_module_name_)
            << std::endl;
  std::cout << "Unique offsets tracked: " << offset_patterns_.size()
            << std::endl;
  std::cout << "Total memory operations: " << wasm_operations_.size()
            << std::endl;
  std::cout << "Functions with memory access: " << function_operations_.size()
            << std::endl;

  uint64_t total_loads = 0, total_stores = 0, total_static = 0,
           total_dynamic = 0;
  for (const auto& [offset, pattern] : offset_patterns_) {
    total_loads += pattern.load_count;
    total_stores += pattern.store_count;
    total_static += pattern.static_access_count;
    total_dynamic += pattern.dynamic_access_count;
  }

  std::cout << "Total loads: " << total_loads << std::endl;
  std::cout << "Total stores: " << total_stores << std::endl;
  std::cout << "Static accesses: " << total_static << std::endl;
  std::cout << "Dynamic accesses: " << total_dynamic << std::endl;
}

void MemoryTracker::PrintHotOffsets(uint32_t top_n) {
  if (!ShouldTrackMemory()) return;

  std::vector<std::pair<uint64_t, uint32_t>> offset_usage;
  for (const auto& [offset, pattern] : offset_patterns_) {
    offset_usage.emplace_back(offset, pattern.frequency);
  }

  std::sort(offset_usage.begin(), offset_usage.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::cout << "[MemHook] Top "
            << std::min(top_n, (uint32_t)offset_usage.size())
            << " Hot Offsets:" << std::endl;
  for (size_t i = 0; i < std::min((size_t)top_n, offset_usage.size()); ++i) {
    const auto& pattern = offset_patterns_[offset_usage[i].first];
    std::cout << "  " << (i + 1) << ". Offset " << offset_usage[i].first << ": "
              << offset_usage[i].second << " uses"
              << " (L:" << pattern.load_count << " S:" << pattern.store_count
              << ")" << std::endl;
  }
}

void MemoryTracker::PrintMemoryStats() {
  if (!ShouldTrackMemory()) return;

  uint64_t total_accesses = 0;
  uint32_t constant_addresses = 0;

  for (const auto& [address, info] : memory_accesses_) {
    total_accesses += info.num_accesses;
    if (info.constant) {
      constant_addresses++;
    }
  }

  std::cout << "[MemHook] Memory Stats:" << std::endl;
  std::cout << "  Unique Addresses: " << memory_accesses_.size() << std::endl;
  std::cout << "  Total Accesses: " << total_accesses << std::endl;
  std::cout << "  Constant Addresses: " << constant_addresses << std::endl;
  std::cout << "  WASM Operations: " << wasm_operations_.size() << std::endl;
  std::cout << "  Unique Offsets: " << offset_patterns_.size() << std::endl;
}

void MemoryTracker::PrintMemoryHotspots(uint32_t top_n) {
  if (!ShouldTrackMemory()) return;

  std::vector<std::pair<uintptr_t, uint32_t>> hotspots;
  for (const auto& [addr, info] : memory_accesses_) {
    hotspots.emplace_back(addr, info.num_accesses);
  }

  std::sort(hotspots.begin(), hotspots.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::cout << "[MemHook] Top " << std::min(top_n, (uint32_t)hotspots.size())
            << " Memory Hotspots:" << std::endl;
  for (size_t i = 0; i < std::min((size_t)top_n, hotspots.size()); ++i) {
    std::cout << "  0x" << std::hex << hotspots[i].first << std::dec << ": "
              << hotspots[i].second << " accesses" << std::endl;
  }
}

void MemoryTracker::ClearMemoryStats() {
  if (!ShouldTrackMemory()) return;

  memory_accesses_.clear();
  wasm_operations_.clear();
  offset_patterns_.clear();
  function_operations_.clear();
  current_function_name_ = "";
  current_function_index_ = 0;

  std::cout << "[MemHook] All memory tracking data cleared" << std::endl;
}

void MemoryTracker::AnalyzeAccessPatterns() {
  if (!ShouldTrackMemory()) return;

  PrintMemoryStats();
  std::cout << std::endl;
  AnalyzeOffsetPatterns();
  std::cout << std::endl;
  PrintTopOffsets(10);
}

// === FUNCTION CONTEXT TRACKING ===

void MemoryTracker::SetCurrentFunction(const std::string& function_name,
                                       uint32_t function_index) {
  if (!ShouldTrackMemory()) return;
  current_function_name_ = function_name;
  current_function_index_ = function_index;
}

void MemoryTracker::ClearCurrentFunction() {
  current_function_name_ = "";
  current_function_index_ = 0;
}

// === IMPORT/SYSCALL TRACKING ===

void MemoryTracker::LogWasmImport(const std::string& module_name,
                                  const std::string& function_name) {
  if (!ShouldTrackMemory()) return;
  std::cout << "[MemHook] WASM Import: " << module_name << "::" << function_name
            << std::endl;
}

void MemoryTracker::TrackSystemCall(const std::string& syscall_name,
                                    const std::string& args) {
  if (!ShouldTrackMemory()) return;
  std::cout << "[MemHook] System call: " << syscall_name << "(" << args << ")"
            << std::endl;
}

void MemoryTracker::TrackWasiCall(const std::string& wasi_function,
                                  const std::string& args) {
  if (!ShouldTrackMemory()) return;
  std::cout << "[MemHook] WASI call: " << wasi_function << "(" << args << ")"
            << std::endl;
}

// === UTILITY AND CONTROL ===

void MemoryTracker::Reset() { ClearMemoryStats(); }

void MemoryTracker::EnableDetailedLogging(bool enable) {
  detailed_logging_enabled_ = enable;
}

}  // namespace liftoff
}  // namespace v8::internal::wasm
