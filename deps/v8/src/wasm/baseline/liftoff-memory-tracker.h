// File: deps/v8/src/wasm/baseline/liftoff-memory-tracker.h
#ifndef V8_WASM_BASELINE_LIFTOFF_MEMORY_TRACKER_H_
#define V8_WASM_BASELINE_LIFTOFF_MEMORY_TRACKER_H_

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace v8::internal::wasm {
namespace liftoff {

// Enhanced memory access information structure
struct MemoryAccessDetails {
  uintptr_t base_address = 0;
  int64_t offset = 0;
  uintptr_t effective_address = 0;
  uint32_t access_size = 0;  // Size in bytes (1, 2, 4, 8)
  std::string access_type;   // "Load", "Store", "LoadSigned", "LoadZeroExt"
  int64_t value = 0;
  uint32_t instruction_offset = 0;  // Offset within WASM function
  std::string register_info;        // Register used for addressing
  std::string instruction_name;     // WASM instruction name
  uint32_t function_index = 0;      // WASM function index
  bool is_static_access = false;    // Whether address is known at compile time
};

// WASM-specific memory operation structure
struct WasmMemoryOperation {
  std::string operation_type;    // "Load", "Store", "LoadSigned", etc.
  uint64_t immediate_offset;     // Offset from WASM instruction immediate
  uint64_t stack_index;          // Base index popped from stack
  uint64_t effective_address;    // Final calculated address
  uint32_t access_size;          // Size in bytes (1, 2, 4, 8)
  int64_t value;                 // Value loaded/stored
  uint32_t function_index;       // WASM function index
  uint32_t bytecode_offset;      // Offset within function bytecode
  std::string instruction_name;  // WASM instruction (i32.load, etc.)
  uint64_t timestamp;            // When the operation occurred
  bool is_static_access;         // Whether access is statically known
  std::string register_info;     // Register information
};

struct OffsetPattern {
  uint64_t immediate_offset;
  uint32_t frequency;
  std::vector<uint32_t> access_sizes;
  std::vector<uint32_t> functions_using;
  std::string most_common_instruction;
  uint32_t load_count = 0;
  uint32_t store_count = 0;
  uint32_t static_access_count = 0;
  uint32_t dynamic_access_count = 0;
};

class MemoryTracker {
 public:
  // Core tracking control
  static bool ShouldTrackMemory();

  // === SIMPLE INTEGRATION FUNCTIONS FOR LIFTOFF-COMPILER.CC ===
  // These are the only functions you need to call from liftoff-compiler.cc

  // Track WASM load operations - call this in LoadMem() function
  static void TrackWasmLoad(
      uint64_t immediate_offset,  // imm.offset from MemoryAccessImmediate
      uint32_t access_size,       // type.size() from LoadType
      uint32_t function_index,    // Current function being compiled
      uint32_t bytecode_offset,   // decoder->position()
      bool is_static = false,  // Whether IndexStaticallyInBounds returned true
      uint64_t static_index = 0  // Known static index (if is_static == true)
  );

  // Track WASM store operations - call this in StoreMem() function
  static void TrackWasmStore(
      uint64_t immediate_offset,  // imm.offset from MemoryAccessImmediate
      uint32_t access_size,       // type.size() from StoreType
      uint32_t function_index,    // Current function being compiled
      uint32_t bytecode_offset,   // decoder->position()
      bool is_static = false,  // Whether IndexStaticallyInBounds returned true
      uint64_t static_index = 0  // Known static index (if is_static == true)
  );

  // === UTILITY FUNCTIONS ===
  // Helper functions to get instruction names from V8's existing types

  static std::string GetLoadInstructionName(int load_type_value);
  static std::string GetStoreInstructionName(int store_type_value);

  // === LEGACY COMPATIBILITY FUNCTIONS ===
  // Keep existing functionality working

  static void LogMemoryAccess(uintptr_t address, const std::string& type,
                              int value);
  static void TrackMemoryLoad(uintptr_t effective_addr, int loaded_value);
  static void TrackMemoryStore(uintptr_t effective_addr, int stored_value);

  // Enhanced memory tracking functions
  static void TrackMemoryLoadDetailed(uintptr_t base_addr, int64_t offset,
                                      uintptr_t effective_addr, uint32_t size,
                                      int64_t loaded_value,
                                      uint32_t instr_offset = 0,
                                      const std::string& reg_info = "");

  static void TrackMemoryStoreDetailed(uintptr_t base_addr, int64_t offset,
                                       uintptr_t effective_addr, uint32_t size,
                                       int64_t stored_value,
                                       uint32_t instr_offset = 0,
                                       const std::string& reg_info = "");

  // === ADVANCED TRACKING (OPTIONAL) ===
  // Enhanced tracking functions for more detailed analysis

  static void TrackCompleteMemoryAccess(const WasmMemoryOperation& operation);

  // === FILE NAMING AND MODULE TRACKING ===
  // Similar to CallTracer infrastructure

  // Set/get trace file prefix for consistent naming across files
  static void SetTraceFilePrefix(const std::string& prefix);
  static std::string GetTraceFilePrefix();

  // Module tracking for better context
  static void SetCurrentModule(const std::string& module_name);
  static std::string GetCurrentModule();

  // === ANALYSIS AND REPORTING FUNCTIONS ===

  // Offset pattern analysis (primary focus for your project)
  static void AnalyzeOffsetPatterns();
  static void DumpOffsetStatistics(
      const std::string& filename = "");  // Empty string = auto-generate
  static void PrintTopOffsets(uint32_t top_n = 20);
  static void FindOffsetClusters();

  // General memory statistics
  static void DumpMemoryAccessStats(
      const std::string& filename = "");  // Empty string = auto-generate
  static void PrintMemoryStats();
  static void PrintMemoryHotspots(uint32_t top_n = 10);
  static void ClearMemoryStats();

  // Comprehensive analysis
  static void AnalyzeAccessPatterns();
  static void GenerateMemoryReport(
      const std::string& filename = "");  // Empty string = auto-generate

  // === EXPORT FUNCTIONS (like CallTracer) ===
  // These automatically generate filenames with proper prefixes

  static void ExportOffsetAnalysisToJSON(const std::string& filename = "");
  static void ExportMemoryStatsToJSON(const std::string& filename = "");
  static void ExportToCSV(const std::string& filename = "");
  static void ExportToText(const std::string& filename = "");

  // Print final statistics (call from env.cc)
  static void PrintStatistics();
  static void PrintHotOffsets(uint32_t top_n = 10);

  // === IMPORT/SYSCALL TRACKING ===
  static void LogWasmImport(const std::string& module_name,
                            const std::string& function_name);
  static void TrackSystemCall(const std::string& syscall_name,
                              const std::string& args);
  static void TrackWasiCall(const std::string& wasi_function,
                            const std::string& args);

  // === FUNCTION CONTEXT TRACKING ===
  static void SetCurrentFunction(const std::string& function_name,
                                 uint32_t function_index);
  static void ClearCurrentFunction();

  // === UTILITY AND CONTROL ===
  static void Reset();
  static void EnableDetailedLogging(bool enable);

 private:
  // Enhanced memory access tracking
  struct EnhancedMemoryAccessInfo {
    uint32_t num_accesses = 0;
    std::optional<int64_t> constant_value = std::nullopt;
    bool constant = true;
    std::vector<WasmMemoryOperation> access_history;
    uint64_t total_bytes_accessed = 0;
    std::unordered_map<int64_t, uint32_t> offset_frequency;
    std::unordered_map<uint32_t, uint32_t> size_frequency;
    uint64_t first_access_time = 0;
    uint64_t last_access_time = 0;
  };

  // === PRIVATE STATE ===
  static std::map<uint64_t, OffsetPattern> offset_patterns_;
  static std::vector<WasmMemoryOperation> wasm_operations_;
  static std::unordered_map<uintptr_t, EnhancedMemoryAccessInfo>
      memory_accesses_;
  static std::map<uint32_t, std::vector<WasmMemoryOperation>>
      function_operations_;
  static std::string current_function_name_;
  static uint32_t current_function_index_;
  static bool detailed_logging_enabled_;

  // File naming and module tracking (similar to CallTracer)
  static std::string trace_file_prefix_;
  static std::string current_module_name_;

  // === PRIVATE HELPER FUNCTIONS ===
  static uint64_t GetCurrentTimestamp();
  static std::string GenerateFilename(const std::string& base_name,
                                      const std::string& extension,
                                      const std::string& custom_filename = "");
};

}  // namespace liftoff
}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_LIFTOFF_MEMORY_TRACKER_H_
