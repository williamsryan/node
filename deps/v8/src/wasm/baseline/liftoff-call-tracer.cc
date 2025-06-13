// File: deps/v8/src/wasm/baseline/liftoff-call-tracer.cc
#include "liftoff-call-tracer.h"

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
thread_local uint32_t CallTracer::max_depth_ = 50;

// Global static member definitions
std::unordered_map<uintptr_t, std::string> CallTracer::function_names_;
std::unordered_map<uint32_t, std::string> CallTracer::function_index_to_name_;
std::unordered_map<uint32_t, std::vector<uint32_t>> CallTracer::call_graph_;
std::unordered_map<uint32_t, uint32_t> CallTracer::call_counts_;
const void* CallTracer::current_module_ = nullptr;
const void* CallTracer::current_wire_bytes_ = nullptr;
bool CallTracer::module_names_extracted_ = false;

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
    }
  }
  return should_trace;
}

void CallTracer::RegisterFunction(uint32_t index, const std::string& name,
                                  uintptr_t target) {
  if (!ShouldTrace()) return;

  function_names_[target] = name;
  function_index_to_name_[index] = name;

  // std::cout << "[WASM_TRACE] Registered function: " << name
  //           << " (index: " << index << ", target: 0x" << std::hex << target
  //           << std::dec << ")" << std::endl;
}

std::string CallTracer::ResolveFunctionName(uint32_t function_index) {
  auto it = function_index_to_name_.find(function_index);
  if (it != function_index_to_name_.end()) {
    return it->second;
  }
  return "func_" + std::to_string(function_index);
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

  // CRITICAL FIX: Always try to resolve function names
  uint32_t func_index = ExtractFunctionIndex(function_name);
  std::string resolved_name = ResolveFunctionName(func_index);

  // If resolution gave us back a generic name but we had a specific name,
  // prefer the specific one
  if (resolved_name.substr(0, 5) == "func_" &&
      function_name.substr(0, 5) != "func_") {
    resolved_name = function_name;
  }

  // Record the call with the resolved name
  RecordFunctionCall(resolved_name, false);

  // Create call info
  CallInfo call_info;
  call_info.function_index = func_index;
  call_info.function_name = resolved_name;
  call_info.start_time = now;
  call_info.depth = depth;
  call_info.call_id = next_call_id_++;
  call_info.is_import = false;
  call_info.completed = false;

  // Print with optional timing and depth visualization
  if (timing_enabled_) {
    double elapsed = GetElapsedMs(trace_start_time_, now);
    std::cout << "[" << std::fixed << std::setprecision(3) << elapsed << "ms] ";
  }

  if (depth_visualization_enabled_) {
    std::cout << GetIndentation(depth);
  }

  // ALSO resolve the caller name if it's not ENTRY
  std::string resolved_caller = caller;
  if (caller != "ENTRY") {
    uint32_t caller_index = ExtractFunctionIndex(caller);
    std::string temp_resolved = ResolveFunctionName(caller_index);
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

  // Create call info for import
  CallInfo call_info;
  call_info.function_index =
      0;  // Imports don't have function indices in our numbering
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
    std::string temp_resolved = ResolveFunctionName(caller_index);
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
    std::string resolved_name = ResolveFunctionName(func_index);

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

void CallTracer::TraceCallWithIndex(uint32_t function_index) {
  if (!ShouldTrace()) return;

  std::string resolved_name = ResolveFunctionName(function_index);
  TraceRuntimeCall(resolved_name);
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
    std::cout << "  Index " << index << " → " << name << std::endl;
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

  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cout << "[WASM_TRACE] Failed to open " << filename << " for writing"
              << std::endl;
    return;
  }

  file << "{\n";
  file << "  \"trace_info\": {\n";
  file << "    \"total_calls\": " << call_history_.size() << ",\n";
  file << "    \"unique_functions\": " << function_index_to_name_.size()
       << ",\n";
  file << "    \"max_depth\": " << [&]() {
    uint32_t max_depth = 0;
    for (const auto& call : call_history_) {
      max_depth = std::max(max_depth, call.depth);
    }
    return max_depth;
  }() << "\n";
  file << "  },\n";

  file << "  \"functions\": {\n";
  bool first_func = true;
  for (const auto& [index, name] : function_index_to_name_) {
    if (!first_func) file << ",\n";
    file << "    \"" << index << "\": \"" << EscapeJSON(name) << "\"";
    first_func = false;
  }
  file << "\n  },\n";

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

  file << "  \"call_counts\": {\n";
  bool first_count = true;
  for (const auto& [func_index, count] : call_counts_) {
    if (!first_count) file << ",\n";
    file << "    \"" << func_index << "\": " << count;
    first_count = false;
  }
  file << "\n  }\n";

  file << "}\n";
  file.close();

  std::cout << "[WASM_TRACE] Exported trace to " << filename << std::endl;
}

void CallTracer::ExportToGraphViz(const std::string& filename) {
  if (!ShouldTrace()) return;

  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cout << "[WASM_TRACE] Failed to open " << filename << " for writing"
              << std::endl;
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

    file << "  func_" << index << " [label=\"" << name << "\\n(index: " << index
         << ", calls: " << call_count << ")\", fillcolor=" << color << "];\n";
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

  std::cout << "[WASM_TRACE] Exported call graph to " << filename
            << " (use 'dot -Tpng " << filename << " -o graph.png' to visualize)"
            << std::endl;
}

void CallTracer::ExportToCSV(const std::string& filename) {
  if (!ShouldTrace()) return;

  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cout << "[WASM_TRACE] Failed to open " << filename << " for writing"
              << std::endl;
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
  std::cout << "[WASM_TRACE] Exported trace to " << filename << std::endl;
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
    std::cout << "  " << (i + 1) << ". " << ResolveFunctionName(func_idx)
              << ": " << count << " calls" << std::endl;
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
