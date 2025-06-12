#ifndef V8_WASM_BASELINE_LIFTOFF_CALL_TRACER_H_
#define V8_WASM_BASELINE_LIFTOFF_CALL_TRACER_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace v8::internal::wasm {
namespace liftoff {

class CallTracer {
public:
  // Move ShouldTrace() here to the public section
  static bool ShouldTrace();
  
  // Register function for runtime tracing
  static void RegisterFunction(uint32_t index, const std::string& name, uintptr_t target);
  
  // Runtime call tracing
  static void TraceRuntimeCall(uintptr_t target);
  static void TraceRuntimeCall(const std::string& function_name);
  static void TraceImportCall(const std::string& import_name);
  
  // Function entry/exit
  static void TraceFunctionEntry(const std::string& function_name);
  static void TraceFunctionExit(const std::string& function_name);
  
  // Utility
  static std::string GetCurrentFunction();
  static void PrintCallStack();
  
  // Additional function tracking (for compatibility with existing V8 code)
  static void LogFunctionCall(const std::string& function_name);
  static void TrackFunctionEntry(const std::string& function_name, uint32_t function_index);
  static void TrackFunctionExit(const std::string& function_name);

private:
  static thread_local std::vector<std::string> call_stack_;
  static std::unordered_map<uintptr_t, std::string> function_names_;
  static std::unordered_map<uint32_t, std::string> function_index_to_name_;
  // Remove ShouldTrace() from here - it's now public above
  
  // Friend declarations
  friend void WasmRuntimeFunctionEntry(uint32_t function_index);
  friend void WasmRuntimeFunctionExit(uint32_t function_index);
};

}  // namespace liftoff
}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_LIFTOFF_CALL_TRACER_H_
