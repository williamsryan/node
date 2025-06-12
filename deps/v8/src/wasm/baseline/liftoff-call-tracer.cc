// File: deps/v8/src/wasm/baseline/liftoff-call-tracer.cc
#include "liftoff-call-tracer.h"

namespace v8::internal::wasm {
namespace liftoff {

// Static member definitions
thread_local std::vector<std::string> CallTracer::call_stack_;
std::unordered_map<uintptr_t, std::string> CallTracer::function_names_;
std::unordered_map<uint32_t, std::string> CallTracer::function_index_to_name_;

bool CallTracer::ShouldTrace() {
  static bool checked = false;
  static bool should_trace = false;
  if (!checked) {
    const char* env = std::getenv("NODE_WASM_FUNCTION_TRACE");
    should_trace = env && std::strcmp(env, "1") == 0;
    checked = true;
    if (should_trace) {
      std::cout << "[WASM_TRACE] Function call tracing enabled" << std::endl;
    }
  }
  return should_trace;
}

void CallTracer::RegisterFunction(uint32_t index, const std::string& name, uintptr_t target) {
  if (!ShouldTrace()) return;

  // std::cout << "[DEBUG] RegisterFunction: index=" << index << ", name=" << name << std::endl;
  
  function_names_[target] = name;
  function_index_to_name_[index] = name;
  std::cout << "[WASM_TRACE] Registered function: " << name << " (index: " << index 
            << ", target: 0x" << std::hex << target << std::dec << ")" << std::endl;
}

void CallTracer::TraceRuntimeCall(uintptr_t target) {
  if (!ShouldTrace()) return;

  std::cout << "[WASM_TRACE] Runtime call to target: 0x" << std::hex << target << std::dec << std::endl;
  
  auto it = function_names_.find(target);
  if (it != function_names_.end()) {
    TraceRuntimeCall(it->second);
  } else {
    TraceRuntimeCall("unknown_0x" + std::to_string(target));
  }
}

void CallTracer::TraceRuntimeCall(const std::string& function_name) {
  if (!ShouldTrace()) return;

  std::cout << "[WASM_TRACE] Runtime call: " << function_name << std::endl;
  
  std::string caller = call_stack_.empty() ? "ENTRY" : call_stack_.back();
  std::cout << caller << " → " << function_name << std::endl;
  
  call_stack_.push_back(function_name);
}

void CallTracer::TraceImportCall(const std::string& import_name) {
  if (!ShouldTrace()) return;
  
  std::string caller = call_stack_.empty() ? "ENTRY" : call_stack_.back();
  std::cout << caller << " → " << import_name << std::endl;
}

void CallTracer::TraceFunctionEntry(const std::string& function_name) {
  TraceRuntimeCall(function_name);
}

void CallTracer::TraceFunctionExit(const std::string& function_name) {
  if (!ShouldTrace()) return;
  
  if (!call_stack_.empty() && call_stack_.back() == function_name) {
    call_stack_.pop_back();
  }
}

std::string CallTracer::GetCurrentFunction() {
  return call_stack_.empty() ? "NONE" : call_stack_.back();
}

void CallTracer::PrintCallStack() {
  if (!ShouldTrace()) return;
  
  std::cout << "[WASM_TRACE] Call stack:";
  for (const auto& func : call_stack_) {
    std::cout << " → " << func;
  }
  std::cout << std::endl;
}

// Additional compatibility methods
void CallTracer::LogFunctionCall(const std::string& function_name) {
  if (!ShouldTrace()) return;
  std::cout << "[WASM_TRACE] Function call: " << function_name << std::endl;
}

void CallTracer::TrackFunctionEntry(const std::string& function_name, uint32_t function_index) {
  if (!ShouldTrace()) return;
  
  std::cout << "[WASM_TRACE] Function entry: " << function_name 
            << " (index: " << function_index << ")" << std::endl;
  call_stack_.push_back(function_name);
}

void CallTracer::TrackFunctionExit(const std::string& function_name) {
  TraceFunctionExit(function_name);
}

}  // namespace liftoff
}  // namespace v8::internal::wasm
