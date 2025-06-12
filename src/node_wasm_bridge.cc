// File: src/node_wasm_bridge.cc (new file in Node.js src directory)
#include "node_options.h"
#include "env-inl.h"

// Bridge functions to expose Node.js flags to V8 WASM code
// These are called from V8 WASM tracer code
extern "C" {

bool node_wasm_should_trace_functions() {
  // Try to get current Node.js environment
  v8::Isolate* isolate = v8::Isolate::TryGetCurrent();
  if (!isolate) {
    return false;
  }
  
  node::Environment* env = node::Environment::GetCurrent(isolate);
  if (env && env->options()) {
    return env->options()->enable_wasm_function_trace;
  }
  
  return false;
}

bool node_wasm_should_trace_memory() {
  // Try to get current Node.js environment
  v8::Isolate* isolate = v8::Isolate::TryGetCurrent();
  if (!isolate) {
    return false;
  }
  
  node::Environment* env = node::Environment::GetCurrent(isolate);
  if (env && env->options()) {
    return env->options()->enable_wasm_memory_hooks;
  }
  
  return false;
}

}  // extern "C"
