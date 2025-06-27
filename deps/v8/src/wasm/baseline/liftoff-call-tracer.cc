// File: deps/v8/src/wasm/baseline/liftoff-call-tracer.cc
#include "liftoff-call-tracer.h"

#include <fstream>

namespace v8::internal::wasm {
namespace liftoff {

// Thread-local static member definitions
thread_local std::vector<std::string> CallTracer::call_stack_;
thread_local std::vector<CallInfo> CallTracer::call_history_;
thread_local std::vector<uint32_t> CallTracer::depth_stack_;
thread_local uint64_t CallTracer::next_call_id_ = 0;
thread_local std::chrono::high_resolution_clock::time_point
    CallTracer::trace_start_time_;
thread_local bool CallTracer::timing_enabled_ = true;
thread_local bool CallTracer::depth_visualization_enabled_ = true;
thread_local uint32_t CallTracer::max_depth_ = 10000;  // UINT32_MAX
thread_local std::ofstream CallTracer::trace_output_file_;

// Global static member definitions (ENHANCED)
std::unordered_map<uintptr_t, std::string> CallTracer::function_names_;
std::unordered_map<uint32_t, std::string> CallTracer::function_index_to_name_;
std::unordered_map<uint32_t, std::vector<uint32_t>> CallTracer::call_graph_;
std::unordered_map<uint32_t, uint32_t> CallTracer::call_counts_;

// NEW: Enhanced module and import tracking
std::unordered_map<std::string, ModuleInfo> CallTracer::module_info_;
std::string CallTracer::current_module_name_;
std::unordered_map<uint32_t, bool> CallTracer::is_import_function_;
std::unordered_map<uint32_t, std::string> CallTracer::import_module_names_;
std::unordered_map<uint32_t, std::string> CallTracer::import_field_names_;

const void* CallTracer::current_module_ = nullptr;
const void* CallTracer::current_wire_bytes_ = nullptr;
bool CallTracer::module_names_extracted_ = false;
std::string CallTracer::trace_file_prefix_ = "wasm";

bool CallTracer::ShouldTrace() {
  static bool checked = false;
  static bool should_trace = false;
  if (!checked) {
    const char* env = std::getenv("NODE_WASM_FUNCTION_TRACE");
    should_trace = env && std::strcmp(env, "1") == 0;
    checked = true;
    if (should_trace) {
      std::cout << "[WASM_TRACE] Function call tracing enabled" << std::endl;
      trace_start_time_ = std::chrono::high_resolution_clock::now();

      // Check for custom trace file prefix from environment
      const char* prefix_env = std::getenv("NODE_WASM_TRACE_PREFIX");
      if (prefix_env) {
        trace_file_prefix_ = std::string(prefix_env);
        std::cout << "[WASM_TRACE] Using custom trace prefix from env: "
                  << trace_file_prefix_ << std::endl;
      }

      trace_output_file_.open(trace_file_prefix_ + "_trace.txt",
                              std::ios::out | std::ios::trunc);
      if (!trace_output_file_.is_open()) {
        std::cerr << "[WASM_TRACE] Failed to open " << trace_file_prefix_
                  << "_trace.txt for writing" << std::endl;
      }
    }
  }
  return should_trace;
}

// ===== NEW: Enhanced Function Registration Methods =====

void CallTracer::RegisterFunctionName(uint32_t function_index,
                                      const std::string& name) {
  if (!ShouldTrace()) return;

  function_index_to_name_[function_index] = name;

  // Add to current module's available functions
  if (!current_module_name_.empty()) {
    auto& module = module_info_[current_module_name_];
    module.available_functions.insert(function_index);
    module.total_functions =
        std::max(module.total_functions, function_index + 1);
  }

  // std::cout << "[WASM_TRACE] Registered function " << function_index << " as
  // '"
  //           << name << "'" << std::endl;
}

void CallTracer::RegisterImportFunction(uint32_t function_index,
                                        const std::string& module_name,
                                        const std::string& field_name) {
  if (!ShouldTrace()) return;

  std::string import_name;
  if (!module_name.empty() && !field_name.empty()) {
    import_name = module_name + "." + field_name;
  } else if (!field_name.empty()) {
    import_name = field_name;
  } else {
    import_name = "import_" + std::to_string(function_index);
  }

  // Register the import function
  function_index_to_name_[function_index] = import_name;
  is_import_function_[function_index] = true;
  import_module_names_[function_index] = module_name;
  import_field_names_[function_index] = field_name;

  // Add to current module info
  if (!current_module_name_.empty()) {
    auto& module = module_info_[current_module_name_];
    module.available_functions.insert(function_index);
    module.imported_functions++;
    module.total_functions =
        std::max(module.total_functions, function_index + 1);
  }

  std::cout << "[WASM_TRACE] Registered import function " << function_index
            << ": " << import_name << " (module: " << module_name << ")"
            << std::endl;
}

void CallTracer::SetCurrentModule(const std::string& module_name) {
  if (!ShouldTrace()) return;

  current_module_name_ = module_name;
  if (module_info_.find(module_name) == module_info_.end()) {
    ModuleInfo info;
    info.module_name = module_name;
    info.total_functions = 0;
    info.imported_functions = 0;
    info.module_functions = 0;
    module_info_[module_name] = info;
  }

  std::cout << "[WASM_TRACE] Set current module to: " << module_name
            << std::endl;
}

void CallTracer::LogFunctionInventory(const std::string& module_name,
                                      uint32_t total_functions) {
  if (!ShouldTrace()) return;

  auto it = module_info_.find(module_name);
  if (it != module_info_.end()) {
    it->second.total_functions = total_functions;
  }

  std::cout << "[WASM_TRACE] === FUNCTION INVENTORY: " << module_name
            << " ===" << std::endl;
  std::cout << "[WASM_TRACE] {" << std::endl;
  std::cout << "[WASM_TRACE]   \"module\": \"" << module_name << "\","
            << std::endl;
  std::cout << "[WASM_TRACE]   \"total_functions\": " << total_functions << ","
            << std::endl;

  if (it != module_info_.end()) {
    std::cout << "[WASM_TRACE]   \"imported_functions\": "
              << it->second.imported_functions << "," << std::endl;
    std::cout << "[WASM_TRACE]   \"module_functions\": "
              << (total_functions - it->second.imported_functions) << ","
              << std::endl;
  }

  std::cout << "[WASM_TRACE]   \"functions\": {" << std::endl;

  for (uint32_t i = 0; i < total_functions; ++i) {
    std::string func_name = GetFunctionName(i);
    bool is_import = is_import_function_[i];

    std::cout << "[WASM_TRACE]     \"" << i << "\": \"" << func_name;
    if (is_import) {
      std::cout << " [IMPORT]";
    }
    std::cout << "\"";
    if (i < total_functions - 1) {
      std::cout << ",";
    }
    std::cout << std::endl;
  }

  std::cout << "[WASM_TRACE]   }" << std::endl;
  std::cout << "[WASM_TRACE] }" << std::endl;
}

void CallTracer::LogExecutionCoverage(
    const std::string& module_name,
    const std::unordered_set<uint32_t>& executed_functions) {
  if (!ShouldTrace()) return;

  auto it = module_info_.find(module_name);
  if (it == module_info_.end()) return;

  const ModuleInfo& module = it->second;

  std::cout << "[WASM_TRACE] === EXECUTION COVERAGE: " << module_name
            << " ===" << std::endl;
  std::cout << "[WASM_TRACE] Total functions: "
            << module.available_functions.size() << std::endl;
  std::cout << "[WASM_TRACE] Executed functions: " << executed_functions.size()
            << std::endl;

  if (!module.available_functions.empty()) {
    double coverage = static_cast<double>(executed_functions.size()) /
                      module.available_functions.size() * 100.0;
    std::cout << "[WASM_TRACE] Coverage: " << std::fixed << std::setprecision(1)
              << coverage << "%" << std::endl;
  }

  // Log functions that were never called
  std::cout << "[WASM_TRACE] Unexecuted functions: ";
  bool first = true;
  for (uint32_t func_idx : module.available_functions) {
    if (executed_functions.find(func_idx) == executed_functions.end()) {
      if (!first) std::cout << ", ";
      std::cout << func_idx << "(" << GetFunctionName(func_idx) << ")";
      first = false;
    }
  }
  std::cout << std::endl;
}

std::unordered_set<uint32_t> CallTracer::GetModuleFunctions(
    const std::string& module_name) {
  auto it = module_info_.find(module_name);
  if (it != module_info_.end()) {
    return it->second.available_functions;
  }
  return std::unordered_set<uint32_t>();
}

std::unordered_set<uint32_t> CallTracer::GetExecutedFunctions(
    const std::string& module_name) {
  auto it = module_info_.find(module_name);
  if (it != module_info_.end()) {
    return it->second.executed_functions;
  }
  return std::unordered_set<uint32_t>();
}

void CallTracer::MarkFunctionExecuted(uint32_t function_index) {
  if (!current_module_name_.empty()) {
    module_info_[current_module_name_].executed_functions.insert(
        function_index);
  }
}

// ===== Enhanced Function Name Resolution =====

std::string CallTracer::GetFunctionName(uint32_t function_index) {
  auto it = function_index_to_name_.find(function_index);
  if (it != function_index_to_name_.end()) {
    return it->second;
  }

  // Fallback to generic name
  return "func_" + std::to_string(function_index);
}

std::string CallTracer::ResolveFunctionName(uint32_t function_index) {
  return GetFunctionName(function_index);  // Alias for compatibility
}

// ===== Enhanced Tracing Methods =====

void CallTracer::TraceCallWithIndex(uint32_t function_index) {
  if (!ShouldTrace()) return;

  std::string resolved_name = GetFunctionName(function_index);
  bool is_import = is_import_function_[function_index];

  MarkFunctionExecuted(function_index);

  if (is_import) {
    TraceImportCall(resolved_name);
  } else {
    TraceRuntimeCall(resolved_name);
  }
}

void CallTracer::TraceImportCallWithIndex(uint32_t function_index) {
  if (!ShouldTrace()) return;

  std::string import_name = GetFunctionName(function_index);
  MarkFunctionExecuted(function_index);
  TraceImportCall(import_name);
}

void CallTracer::LogFunctionCallWithIndex(uint32_t function_index,
                                          bool is_import, uint64_t timestamp_us,
                                          uint32_t depth, bool is_entry) {
  if (!ShouldTrace()) return;

  std::string func_name = GetFunctionName(function_index);

  std::string prefix = "[" + std::to_string(timestamp_us / 1000.0) + "ms] ";
  prefix += std::string(depth * 2, ' ');  // Indentation based on depth

  if (is_entry) {
    prefix += "ENTRY → ";
  }

  if (is_import) {
    prefix += "[IMPORT] ";
  }

  std::cout << prefix << func_name << std::endl;
}

// ===== Existing Methods (Updated to use enhanced resolution) =====

void CallTracer::SetTraceFilePrefix(const std::string& prefix) {
  if (!prefix.empty()) {
    trace_file_prefix_ = ExtractBasename(prefix);
    std::cout << "[WASM_TRACE] Set trace file prefix to: " << trace_file_prefix_
              << std::endl;
  }
}

std::string CallTracer::GetTraceFilePrefix() { return trace_file_prefix_; }

std::string CallTracer::ExtractBasename(const std::string& filepath) {
  // Find the last path separator
  size_t last_slash = filepath.find_last_of("/\\");
  std::string filename = (last_slash == std::string::npos)
                             ? filepath
                             : filepath.substr(last_slash + 1);

  // Remove file extension
  size_t last_dot = filename.find_last_of('.');
  if (last_dot != std::string::npos) {
    filename = filename.substr(0, last_dot);
  }

  return filename;
}

void CallTracer::RegisterFunction(uint32_t index, const std::string& name,
                                  uintptr_t target) {
  if (!ShouldTrace()) return;

  function_names_[target] = name;
  RegisterFunctionName(index, name);
}

void CallTracer::SetModuleInfo(const void* module, const void* wire_bytes) {
  current_module_ = module;
  current_wire_bytes_ = wire_bytes;
}

void CallTracer::TraceRuntimeCall(uintptr_t target) {
  if (!ShouldTrace()) return;

  auto it = function_names_.find(target);
  if (it != function_names_.end()) {
    TraceRuntimeCall(it->second);
  } else {
    TraceRuntimeCall("unknown_0x" + std::to_string(target));
  }
}

bool CallTracer::IsImportFunction(uint32_t function_index) {
  auto it = is_import_function_.find(function_index);
  return it != is_import_function_.end() && it->second;
}

void CallTracer::TraceRuntimeCall(const std::string& function_name) {
  if (!ShouldTrace()) return;

  auto now = std::chrono::high_resolution_clock::now();
  uint32_t depth = call_stack_.size();
  std::string caller = call_stack_.empty() ? "ENTRY" : call_stack_.back();

  // Check max depth limit
  if (depth >= max_depth_) {
    std::cout << "[WASM_TRACE] Max depth reached (" << max_depth_
              << "), skipping deeper calls" << std::endl;
    return;
  }

  // Resolve function names using enhanced resolution
  uint32_t func_index = ExtractFunctionIndex(function_name);
  std::string resolved_name = GetFunctionName(func_index);

  // If resolution gave us back a generic name but we had a specific name,
  // prefer the specific one
  if (resolved_name.substr(0, 5) == "func_" &&
      function_name.substr(0, 5) != "func_") {
    resolved_name = function_name;
  }

  bool is_import = IsImportFunction(func_index);

  // Record the call with the resolved name
  RecordFunctionCall(resolved_name, is_import);

  // Create call info
  CallInfo call_info;
  call_info.function_index = func_index;
  call_info.function_name = resolved_name;
  call_info.start_time = now;
  call_info.depth = depth;
  call_info.call_id = next_call_id_++;
  call_info.is_import = is_import;
  call_info.completed = false;

  // Mark function as executed
  MarkFunctionExecuted(func_index);

  // Print with optional timing and depth visualization
  if (timing_enabled_) {
    double elapsed = GetElapsedMs(trace_start_time_, now);
    std::cout << "[" << std::fixed << std::setprecision(3) << elapsed << "ms] ";
  }

  if (depth_visualization_enabled_) {
    std::cout << GetIndentation(depth);
  }

  // Resolve the caller name if it's not ENTRY
  std::string resolved_caller = caller;
  if (caller != "ENTRY") {
    uint32_t caller_index = ExtractFunctionIndex(caller);
    std::string temp_resolved = GetFunctionName(caller_index);
    // If we got a better resolution, use it
    if (temp_resolved.substr(0, 5) != "func_" ||
        caller.substr(0, 5) == "func_") {
      resolved_caller = temp_resolved;
    }
  }

  std::cout << resolved_caller << " → " << resolved_name << std::endl;

  // Update call graph
  if (!call_stack_.empty()) {
    uint32_t caller_index = ExtractFunctionIndex(caller);
    UpdateCallGraph(caller_index, func_index);
  }

  call_stack_.push_back(resolved_name);
  depth_stack_.push_back(func_index);
  call_history_.push_back(call_info);
}

void CallTracer::TraceImportCall(const std::string& import_name) {
  if (!ShouldTrace()) return;

  RecordFunctionCall(import_name, true);

  auto now = std::chrono::high_resolution_clock::now();
  uint32_t depth = call_stack_.size();
  std::string caller = call_stack_.empty() ? "ENTRY" : call_stack_.back();

  // Extract function index for imports (if available)
  uint32_t func_index = ExtractFunctionIndex(import_name);

  // Mark as executed
  MarkFunctionExecuted(func_index);

  // Create call info for import
  CallInfo call_info;
  call_info.function_index = func_index;
  call_info.function_name = import_name;
  call_info.start_time = now;
  call_info.end_time = now;  // Imports complete immediately
  call_info.depth = depth;
  call_info.call_id = next_call_id_++;
  call_info.is_import = true;
  call_info.completed = true;

  // Print import call
  if (timing_enabled_) {
    double elapsed = GetElapsedMs(trace_start_time_, now);
    std::cout << "[" << std::fixed << std::setprecision(3) << elapsed << "ms] ";
  }

  if (depth_visualization_enabled_) {
    std::cout << GetIndentation(depth);
  }

  // Resolve caller name for consistency
  std::string resolved_caller = caller;
  if (caller != "ENTRY") {
    uint32_t caller_index = ExtractFunctionIndex(caller);
    std::string temp_resolved = GetFunctionName(caller_index);
    if (temp_resolved.substr(0, 5) != "func_" ||
        caller.substr(0, 5) == "func_") {
      resolved_caller = temp_resolved;
    }
  }

  std::cout << "[IMPORT] " << resolved_caller << " → " << import_name
            << std::endl;

  call_history_.push_back(call_info);
}

void CallTracer::TraceFunctionEntry(const std::string& function_name) {
  TraceRuntimeCall(function_name);
}

void CallTracer::TraceFunctionExit(const std::string& function_name) {
  if (!ShouldTrace()) return;

  if (!call_stack_.empty() && !call_history_.empty()) {
    auto now = std::chrono::high_resolution_clock::now();

    // Resolve the function name for consistency
    uint32_t func_index = ExtractFunctionIndex(function_name);
    std::string resolved_name = GetFunctionName(func_index);

    // If resolution gave us back a generic name but we had a specific name,
    // prefer the specific one
    if (resolved_name.substr(0, 5) == "func_" &&
        function_name.substr(0, 5) != "func_") {
      resolved_name = function_name;
    }

    // Find the corresponding entry in call_history
    for (auto it = call_history_.rbegin(); it != call_history_.rend(); ++it) {
      // Match by either the original name or resolved name
      if (!it->completed && (it->function_name == function_name ||
                             it->function_name == resolved_name)) {
        it->end_time = now;
        it->completed = true;

        double duration = GetElapsedMs(it->start_time, now);

        // Print exit timing if enabled and duration is significant
        if (timing_enabled_ &&
            duration > 0.1) {  // Only show timing for functions > 0.1ms
          if (depth_visualization_enabled_) {
            std::cout << GetIndentation(it->depth);
          }
          std::cout << "↳ " << it->function_name << " completed in "
                    << std::fixed << std::setprecision(3) << duration << "ms"
                    << std::endl;
        }
        break;
      }
    }

    // Remove from call stack - check both resolved and original names
    if (call_stack_.back() == function_name ||
        call_stack_.back() == resolved_name) {
      call_stack_.pop_back();
      if (!depth_stack_.empty()) {
        depth_stack_.pop_back();
      }
    }
  }
}

uint32_t CallTracer::GetCurrentDepth() { return call_stack_.size(); }

std::string CallTracer::GetCurrentFunction() {
  return call_stack_.empty() ? "NONE" : call_stack_.back();
}

std::string CallTracer::GetIndentation(uint32_t depth) {
  std::string indent;
  for (uint32_t i = 0; i < depth; ++i) {
    indent += "  ";
  }
  return indent;
}

void CallTracer::PrintCallStack() {
  if (!ShouldTrace()) return;

  std::cout << "[WASM_TRACE] Current call stack:" << std::endl;
  for (size_t i = 0; i < call_stack_.size(); ++i) {
    std::cout << GetIndentation(i) << "→ " << call_stack_[i] << std::endl;
  }
}

void CallTracer::DebugPrintRegistrations() {
  if (!ShouldTrace()) return;

  std::cout << "[WASM_TRACE] === Registered Function Names ===" << std::endl;
  for (const auto& [index, name] : function_index_to_name_) {
    bool is_import = is_import_function_[index];
    std::cout << "  Index " << index << " → " << name;
    if (is_import) {
      std::cout << " [IMPORT: " << import_module_names_[index] << "."
                << import_field_names_[index] << "]";
    }
    std::cout << std::endl;
  }
  std::cout << "==============================" << std::endl;
}

double CallTracer::GetElapsedMs(
    std::chrono::high_resolution_clock::time_point start,
    std::chrono::high_resolution_clock::time_point end) {
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  return duration.count() / 1000.0;
}

void CallTracer::ExportToJSON(const std::string& filename) {
  if (!ShouldTrace()) return;

  // Use provided filename or construct from prefix
  std::string output_filename =
      filename.empty() ? (trace_file_prefix_ + "_trace.json") : filename;

  std::ofstream file(output_filename);
  if (!file.is_open()) {
    std::cout << "[WASM_TRACE] Failed to open " << output_filename
              << " for writing" << std::endl;
    return;
  }

  // Calculate comprehensive statistics
  std::unordered_set<uint32_t> executed_functions_set;
  std::unordered_set<uint32_t> executed_imports;
  std::unordered_set<uint32_t> executed_module_functions;

  for (const auto& call : call_history_) {
    executed_functions_set.insert(call.function_index);
    if (call.is_import || is_import_function_[call.function_index]) {
      executed_imports.insert(call.function_index);
    } else {
      executed_module_functions.insert(call.function_index);
    }
  }

  uint32_t max_depth = 0;
  uint32_t import_call_count = 0;
  uint32_t module_call_count = 0;
  double total_execution_time = 0.0;
  uint32_t completed_calls = 0;

  for (const auto& call : call_history_) {
    max_depth = std::max(max_depth, call.depth);
    if (call.is_import || is_import_function_[call.function_index]) {
      import_call_count++;
    } else {
      module_call_count++;
    }

    if (call.completed) {
      total_execution_time += GetElapsedMs(call.start_time, call.end_time);
      completed_calls++;
    }
  }

  // Count available functions by type
  uint32_t total_available_imports = 0;
  uint32_t total_available_module_functions = 0;

  for (const auto& [index, name] : function_index_to_name_) {
    if (is_import_function_[index]) {
      total_available_imports++;
    } else {
      total_available_module_functions++;
    }
  }

  uint32_t total_available_functions = function_index_to_name_.size();
  uint32_t total_executed_functions = executed_functions_set.size();

  double overall_coverage =
      total_available_functions > 0
          ? static_cast<double>(total_executed_functions) /
                total_available_functions * 100.0
          : 0.0;
  double import_coverage = total_available_imports > 0
                               ? static_cast<double>(executed_imports.size()) /
                                     total_available_imports * 100.0
                               : 0.0;
  double module_coverage =
      total_available_module_functions > 0
          ? static_cast<double>(executed_module_functions.size()) /
                total_available_module_functions * 100.0
          : 0.0;

  file << "{\n";

  // Enhanced trace_info with comprehensive statistics
  file << "  \"trace_info\": {\n";
  file << "    \"module_name\": \"" << EscapeJSON(current_module_name_)
       << "\",\n";
  file << "    \"total_calls\": " << call_history_.size() << ",\n";
  file << "    \"unique_functions_executed\": " << total_executed_functions
       << ",\n";
  file << "    \"max_depth\": " << max_depth << ",\n";
  file << "    \"total_execution_time_ms\": " << std::fixed
       << std::setprecision(3) << total_execution_time << ",\n";
  file << "    \"average_call_duration_ms\": " << std::fixed
       << std::setprecision(3)
       << (completed_calls > 0 ? total_execution_time / completed_calls : 0.0)
       << "\n";
  file << "  },\n";

  // NEW: Module statistics section
  file << "  \"module_stats\": {\n";
  file << "    \"total_available_functions\": " << total_available_functions
       << ",\n";
  file << "    \"total_executed_functions\": " << total_executed_functions
       << ",\n";
  file << "    \"overall_coverage_percent\": " << std::fixed
       << std::setprecision(1) << overall_coverage << ",\n";
  file << "    \"import_functions\": {\n";
  file << "      \"total_available\": " << total_available_imports << ",\n";
  file << "      \"executed\": " << executed_imports.size() << ",\n";
  file << "      \"coverage_percent\": " << std::fixed << std::setprecision(1)
       << import_coverage << ",\n";
  file << "      \"total_calls\": " << import_call_count << "\n";
  file << "    },\n";
  file << "    \"module_functions\": {\n";
  file << "      \"total_available\": " << total_available_module_functions
       << ",\n";
  file << "      \"executed\": " << executed_module_functions.size() << ",\n";
  file << "      \"coverage_percent\": " << std::fixed << std::setprecision(1)
       << module_coverage << ",\n";
  file << "      \"total_calls\": " << module_call_count << "\n";
  file << "    }\n";
  file << "  },\n";

  // NEW: All available functions (not just executed ones)
  file << "  \"functions\": {\n";
  bool first_func = true;

  // Create a sorted list of ALL function indices for consistent output
  std::vector<uint32_t> all_function_indices;
  for (const auto& [index, name] : function_index_to_name_) {
    all_function_indices.push_back(index);
  }
  std::sort(all_function_indices.begin(), all_function_indices.end());

  for (uint32_t index : all_function_indices) {
    if (!first_func) file << ",\n";

    std::string func_name = function_index_to_name_[index];
    bool is_import = is_import_function_[index];
    bool was_executed =
        executed_functions_set.find(index) != executed_functions_set.end();
    uint32_t call_count = call_counts_[index];

    file << "    \"" << index << "\": {\n";
    file << "      \"name\": \"" << EscapeJSON(func_name) << "\",\n";
    file << "      \"type\": \"" << (is_import ? "import" : "module")
         << "\",\n";
    file << "      \"executed\": " << (was_executed ? "true" : "false")
         << ",\n";
    file << "      \"call_count\": " << call_count;

    // Add import-specific information
    if (is_import) {
      auto module_it = import_module_names_.find(index);
      auto field_it = import_field_names_.find(index);
      if (module_it != import_module_names_.end() ||
          field_it != import_field_names_.end()) {
        file << ",\n      \"import_info\": {\n";
        if (module_it != import_module_names_.end()) {
          file << "        \"module\": \"" << EscapeJSON(module_it->second)
               << "\"";
        }
        if (field_it != import_field_names_.end()) {
          if (module_it != import_module_names_.end()) file << ",\n";
          file << "        \"field\": \"" << EscapeJSON(field_it->second)
               << "\"";
        }
        file << "\n      }";
      }
    }

    file << "\n    }";
    first_func = false;
  }
  file << "\n  },\n";

  // Enhanced execution summary
  file << "  \"execution_summary\": {\n";
  file << "    \"executed_functions\": [";
  bool first_exec = true;
  std::vector<uint32_t> executed_sorted(executed_functions_set.begin(),
                                        executed_functions_set.end());
  std::sort(executed_sorted.begin(), executed_sorted.end());
  for (uint32_t func_idx : executed_sorted) {
    if (!first_exec) file << ", ";
    file << func_idx;
    first_exec = false;
  }
  file << "],\n";

  file << "    \"unexecuted_functions\": [";
  bool first_unexec = true;
  for (uint32_t index : all_function_indices) {
    if (executed_functions_set.find(index) == executed_functions_set.end()) {
      if (!first_unexec) file << ", ";
      file << index;
      first_unexec = false;
    }
  }
  file << "]\n";
  file << "  },\n";

  // Existing call_history section (unchanged)
  file << "  \"call_history\": [\n";
  bool first_call = true;
  for (const auto& call : call_history_) {
    if (!first_call) file << ",\n";
    file << "    {\n";
    file << "      \"call_id\": " << call.call_id << ",\n";
    file << "      \"function_index\": " << call.function_index << ",\n";
    file << "      \"function_name\": \"" << EscapeJSON(call.function_name)
         << "\",\n";
    file << "      \"depth\": " << call.depth << ",\n";
    file << "      \"is_import\": " << (call.is_import ? "true" : "false")
         << ",\n";
    file << "      \"start_time_us\": "
         << std::chrono::duration_cast<std::chrono::microseconds>(
                call.start_time - trace_start_time_)
                .count();
    if (call.completed) {
      file << ",\n      \"end_time_us\": "
           << std::chrono::duration_cast<std::chrono::microseconds>(
                  call.end_time - trace_start_time_)
                  .count();
      file << ",\n      \"duration_us\": "
           << std::chrono::duration_cast<std::chrono::microseconds>(
                  call.end_time - call.start_time)
                  .count();
    }
    file << "\n    }";
    first_call = false;
  }
  file << "\n  ],\n";

  // Existing call_graph section (unchanged)
  file << "  \"call_graph\": {\n";
  bool first_graph = true;
  for (const auto& [caller, callees] : call_graph_) {
    if (!first_graph) file << ",\n";
    file << "    \"" << caller << "\": [";
    bool first_callee = true;
    for (uint32_t callee : callees) {
      if (!first_callee) file << ", ";
      file << callee;
      first_callee = false;
    }
    file << "]";
    first_graph = false;
  }
  file << "\n  },\n";

  // Existing call_counts section (but now includes ALL functions)
  file << "  \"call_counts\": {\n";
  bool first_count = true;
  for (uint32_t index : all_function_indices) {
    if (!first_count) file << ",\n";
    uint32_t count = call_counts_[index];  // Will be 0 for unexecuted functions
    file << "    \"" << index << "\": " << count;
    first_count = false;
  }
  file << "\n  }\n";

  file << "}\n";
  file.close();

  std::cout << "[WASM_TRACE] Exported trace to " << output_filename
            << std::endl;
  std::cout << "[WASM_TRACE] Coverage: " << std::fixed << std::setprecision(1)
            << overall_coverage << "% (" << total_executed_functions << "/"
            << total_available_functions << " functions)" << std::endl;
}

void CallTracer::ExportToGraphViz(const std::string& filename) {
  if (!ShouldTrace()) return;

  // Use provided filename or construct from prefix
  std::string output_filename =
      filename.empty() ? (trace_file_prefix_ + "_calls.dot") : filename;

  std::ofstream file(output_filename);
  if (!file.is_open()) {
    std::cout << "[WASM_TRACE] Failed to open " << output_filename
              << " for writing" << std::endl;
    return;
  }

  file << "digraph WasmCallGraph {\n";
  file << "  rankdir=TB;\n";
  file << "  node [shape=box, style=filled];\n";
  file << "  edge [color=darkblue];\n\n";

  // Add nodes with call frequency coloring
  for (const auto& [index, name] : function_index_to_name_) {
    uint32_t call_count = call_counts_[index];
    std::string color = "lightblue";
    if (call_count > 10) color = "yellow";
    if (call_count > 50) color = "orange";
    if (call_count > 100) color = "red";

    // Add import indicator
    std::string shape = is_import_function_[index] ? "ellipse" : "box";

    file << "  func_" << index << " [label=\"" << name << "\\n(index: " << index
         << ", calls: " << call_count << ")\", fillcolor=" << color
         << ", shape=" << shape << "];\n";
  }

  file << "\n";

  // Add edges with weights
  for (const auto& [caller, callees] : call_graph_) {
    for (uint32_t callee : callees) {
      file << "  func_" << caller << " -> func_" << callee << ";\n";
    }
  }

  file << "}\n";
  file.close();

  std::cout << "[WASM_TRACE] Exported call graph to " << output_filename
            << " (use 'dot -Tpng " << output_filename
            << " -o graph.png' to visualize)" << std::endl;
}

void CallTracer::ExportToCSV(const std::string& filename) {
  if (!ShouldTrace()) return;

  // Use provided filename or construct from prefix
  std::string output_filename =
      filename.empty() ? (trace_file_prefix_ + "_calls.csv") : filename;

  std::ofstream file(output_filename);
  if (!file.is_open()) {
    std::cout << "[WASM_TRACE] Failed to open " << output_filename
              << " for writing" << std::endl;
    return;
  }

  // CSV header
  file << "call_id,function_name,function_index,depth,is_import,start_time_us,"
          "end_time_us,duration_us\n";

  // CSV data
  for (const auto& call : call_history_) {
    file << call.call_id << "," << EscapeCSV(call.function_name) << ","
         << call.function_index << "," << call.depth << ","
         << (call.is_import ? "true" : "false") << ","
         << std::chrono::duration_cast<std::chrono::microseconds>(
                call.start_time - trace_start_time_)
                .count()
         << ",";

    if (call.completed) {
      file << std::chrono::duration_cast<std::chrono::microseconds>(
                  call.end_time - trace_start_time_)
                  .count()
           << ","
           << std::chrono::duration_cast<std::chrono::microseconds>(
                  call.end_time - call.start_time)
                  .count();
    } else {
      file << ",";  // Empty end_time and duration for incomplete calls
    }
    file << "\n";
  }

  file.close();
  std::cout << "[WASM_TRACE] Exported trace to " << output_filename
            << std::endl;
}

void CallTracer::ExportToText(const std::string& filename) {
  if (!ShouldTrace()) return;

  // Use provided filename or construct from prefix
  std::string output_filename =
      filename.empty() ? (trace_file_prefix_ + "_trace.txt") : filename;

  std::ofstream file(output_filename);
  if (!file.is_open()) {
    std::cerr << "[WASM_TRACE] Failed to open " << output_filename
              << " for writing" << std::endl;
    return;
  }

  for (const auto& call : call_history_) {
    double elapsed = GetElapsedMs(trace_start_time_, call.start_time);
    std::string indent = GetIndentation(call.depth);
    std::string line;

    if (call.is_import) {
      line = "[IMPORT] ";
    }

    line += "[" + std::to_string(elapsed) + "ms] ";
    line += indent;

    // Determine caller if possible
    std::string caller = "ENTRY";
    for (auto it = call_history_.rbegin(); it != call_history_.rend(); ++it) {
      if (it->call_id < call.call_id && it->depth + 1 == call.depth) {
        caller = it->function_name;
        break;
      }
    }

    line += caller + " → " + call.function_name;
    file << line << std::endl;
  }

  file.close();
  std::cout << "[WASM_TRACE] Exported textual trace to " << output_filename
            << std::endl;
}

void CallTracer::PrintStatistics() {
  if (!ShouldTrace()) return;

  std::cout << "\n=== Execution Statistics ===" << std::endl;
  std::cout << "Total function calls: " << call_history_.size() << std::endl;
  std::cout << "Unique functions called: " << function_index_to_name_.size()
            << std::endl;
  std::cout << "Import calls: " << [&]() {
    uint32_t import_count = 0;
    for (const auto& call : call_history_) {
      if (call.is_import) import_count++;
    }
    return import_count;
  }() << std::endl;

  uint32_t max_depth = 0;
  double total_time = 0;
  uint32_t completed_calls = 0;

  for (const auto& call : call_history_) {
    max_depth = std::max(max_depth, call.depth);
    if (call.completed) {
      total_time += GetElapsedMs(call.start_time, call.end_time);
      completed_calls++;
    }
  }

  std::cout << "Maximum call depth: " << max_depth << std::endl;
  std::cout << "Total execution time: " << std::fixed << std::setprecision(3)
            << total_time << "ms" << std::endl;
  std::cout << "Average call duration: " << std::fixed << std::setprecision(3)
            << (completed_calls > 0 ? total_time / completed_calls : 0) << "ms"
            << std::endl;

  std::cout << "==============================\n" << std::endl;
}

void CallTracer::PrintHotFunctions(int top_n) {
  if (!ShouldTrace()) return;

  std::cout << "\n=== Top " << top_n
            << " Most Called Functions ===" << std::endl;

  std::vector<std::pair<uint32_t, uint32_t>> sorted_calls(call_counts_.begin(),
                                                          call_counts_.end());
  std::sort(sorted_calls.begin(), sorted_calls.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  for (int i = 0; i < std::min(top_n, static_cast<int>(sorted_calls.size()));
       ++i) {
    uint32_t func_idx = sorted_calls[i].first;
    uint32_t count = sorted_calls[i].second;
    std::cout << "  " << (i + 1) << ". " << GetFunctionName(func_idx) << ": "
              << count << " calls" << std::endl;
  }

  std::cout << "==============================\n" << std::endl;
}

std::string CallTracer::EscapeJSON(const std::string& str) {
  std::string escaped;
  for (char c : str) {
    switch (c) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

std::string CallTracer::EscapeCSV(const std::string& str) {
  if (str.find(',') != std::string::npos ||
      str.find('"') != std::string::npos) {
    std::string escaped = "\"";
    for (char c : str) {
      if (c == '"')
        escaped += "\"\"";
      else
        escaped += c;
    }
    escaped += "\"";
    return escaped;
  }
  return str;
}

uint32_t CallTracer::ExtractFunctionIndex(const std::string& function_name) {
  if (function_name.substr(0, 5) == "func_") {
    std::string number_part = function_name.substr(5);

    // Check if the string contains only digits
    if (!number_part.empty() &&
        std::all_of(number_part.begin(), number_part.end(), ::isdigit)) {
      // Safe to convert since we validated it's all digits
      return static_cast<uint32_t>(std::stoul(number_part));
    }
    return 0;  // Invalid format
  }

  // Try to find by name in reverse lookup
  for (const auto& [index, name] : function_index_to_name_) {
    if (name == function_name) {
      return index;
    }
  }

  return 0;
}

void CallTracer::UpdateCallGraph(uint32_t caller_index, uint32_t callee_index) {
  auto& callees = call_graph_[caller_index];
  if (std::find(callees.begin(), callees.end(), callee_index) ==
      callees.end()) {
    callees.push_back(callee_index);
  }
}

void CallTracer::RecordFunctionCall(const std::string& function_name,
                                    bool is_import) {
  if (!is_import) {
    uint32_t func_index = ExtractFunctionIndex(function_name);
    call_counts_[func_index]++;
  }
}

// Utility and control methods
void CallTracer::Reset() {
  call_stack_.clear();
  call_history_.clear();
  depth_stack_.clear();
  next_call_id_ = 0;
  call_counts_.clear();
  call_graph_.clear();
  module_info_.clear();
  current_module_name_.clear();
  if (trace_output_file_.is_open()) {
    trace_output_file_.close();
  }
}

void CallTracer::EnableTiming(bool enable) { timing_enabled_ = enable; }

void CallTracer::EnableDepthVisualization(bool enable) {
  depth_visualization_enabled_ = enable;
}

void CallTracer::SetMaxDepth(uint32_t max_depth) { max_depth_ = max_depth; }

// Compatibility methods for existing V8 integration
void CallTracer::LogFunctionCall(const std::string& function_name) {
  if (!ShouldTrace()) return;
  std::cout << "[WASM_TRACE] Function call: " << function_name << std::endl;
}

void CallTracer::TrackFunctionEntry(const std::string& function_name,
                                    uint32_t function_index) {
  if (!ShouldTrace()) return;

  std::cout << "[WASM_TRACE] Function entry: " << function_name
            << " (index: " << function_index << ")" << std::endl;
  TraceFunctionEntry(function_name);
}

void CallTracer::TrackFunctionExit(const std::string& function_name) {
  TraceFunctionExit(function_name);
}

void CallTracer::PrintCallTree() {
  // TODO: Implement tree visualization
  PrintCallStack();
}

void CallTracer::ExportToTrace(const std::string& filename) {
  // TODO: Implement Chrome trace format export
  ExportToJSON(filename);
}

void CallTracer::PrintCallFrequency() { PrintHotFunctions(10); }

}  // namespace liftoff
}  // namespace v8::internal::wasm
