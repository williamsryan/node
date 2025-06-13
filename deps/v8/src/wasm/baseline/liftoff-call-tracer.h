// File: deps/v8/src/wasm/baseline/liftoff-call-tracer.h
#ifndef V8_WASM_BASELINE_LIFTOFF_CALL_TRACER_H_
#define V8_WASM_BASELINE_LIFTOFF_CALL_TRACER_H_

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace v8::internal::wasm {
namespace liftoff {

struct CallInfo {
  uint32_t function_index;
  std::string function_name;
  std::chrono::high_resolution_clock::time_point start_time;
  std::chrono::high_resolution_clock::time_point end_time;
  uint32_t depth;
  std::vector<uint32_t> callees;  // Functions called by this function
  uint64_t call_id;               // Unique ID for this call instance
  bool is_import;
  bool completed;
};

class CallTracer {
 public:
  // Core tracing control
  static bool ShouldTrace();

  // Function registration (called during module setup)
  static void RegisterFunction(uint32_t index, const std::string& name,
                               uintptr_t target);

  // Runtime call tracing (called during execution)
  static void TraceRuntimeCall(uintptr_t target);
  static void TraceRuntimeCall(const std::string& function_name);
  static void TraceImportCall(const std::string& import_name);

  // Function entry/exit with timing and depth
  static void TraceFunctionEntry(const std::string& function_name);
  static void TraceFunctionExit(const std::string& function_name);

  // New method for tracing with function index (resolves to name automatically)
  static void TraceCallWithIndex(uint32_t function_index);

  // Function name resolution
  static std::string ResolveFunctionName(uint32_t function_index);
  static void SetModuleInfo(const void* module, const void* wire_bytes);

  // Call stack and depth utilities
  static std::string GetCurrentFunction();
  static uint32_t GetCurrentDepth();
  static void PrintCallStack();

  // Visualization and depth
  static void PrintCallTree();
  static std::string GetIndentation(uint32_t depth);

  // Debug functionality
  static void DebugPrintRegistrations();

  // Export functionality
  static void ExportToJSON(const std::string& filename);
  static void ExportToGraphViz(const std::string& filename);
  static void ExportToTrace(
      const std::string& filename);  // Chrome trace format
  static void ExportToCSV(const std::string& filename);

  // Statistics and analysis
  static void PrintStatistics();
  static void PrintHotFunctions(int top_n = 10);
  static void PrintCallFrequency();

  // Additional compatibility methods for existing V8 integration
  static void LogFunctionCall(const std::string& function_name);
  static void TrackFunctionEntry(const std::string& function_name,
                                 uint32_t function_index);
  static void TrackFunctionExit(const std::string& function_name);

  // Utility and control
  static void Reset();
  static void EnableTiming(bool enable);
  static void EnableDepthVisualization(bool enable);
  static void SetMaxDepth(uint32_t max_depth);

 private:
  // Thread-local execution state
  static thread_local std::vector<std::string> call_stack_;
  static thread_local std::vector<CallInfo> call_history_;
  static thread_local std::vector<uint32_t> depth_stack_;
  static thread_local uint64_t next_call_id_;
  static thread_local std::chrono::high_resolution_clock::time_point
      trace_start_time_;
  static thread_local bool timing_enabled_;
  static thread_local bool depth_visualization_enabled_;
  static thread_local uint32_t max_depth_;

  // Global function metadata
  static std::unordered_map<uintptr_t, std::string> function_names_;
  static std::unordered_map<uint32_t, std::string> function_index_to_name_;
  static std::unordered_map<uint32_t, std::vector<uint32_t>>
      call_graph_;  // caller -> callees
  static std::unordered_map<uint32_t, uint32_t>
      call_counts_;  // function_index -> call_count

  // Module information for name resolution
  static const void* current_module_;
  static const void* current_wire_bytes_;
  static bool module_names_extracted_;

  // Helper functions
  static double GetElapsedMs(
      std::chrono::high_resolution_clock::time_point start,
      std::chrono::high_resolution_clock::time_point end);
  static std::string EscapeJSON(const std::string& str);
  static std::string EscapeCSV(const std::string& str);
  static uint32_t ExtractFunctionIndex(const std::string& function_name);
  static void UpdateCallGraph(uint32_t caller_index, uint32_t callee_index);
  static void RecordFunctionCall(const std::string& function_name,
                                 bool is_import = false);

  // Friend declarations for V8 integration
  friend void WasmRuntimeFunctionEntry(uint32_t function_index);
  friend void WasmRuntimeFunctionExit(uint32_t function_index);
};

}  // namespace liftoff
}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_LIFTOFF_CALL_TRACER_H_
