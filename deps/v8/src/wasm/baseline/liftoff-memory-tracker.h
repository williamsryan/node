// File: deps/v8/src/wasm/baseline/liftoff-memory-tracker.h
#ifndef V8_WASM_BASELINE_LIFTOFF_MEMORY_TRACKER_H_
#define V8_WASM_BASELINE_LIFTOFF_MEMORY_TRACKER_H_

#include <string>
#include <cstdint>

namespace v8::internal::wasm {
namespace liftoff {

// Memory tracking functions
bool ShouldTrackMemory();
void LogMemoryAccess(uintptr_t address, const std::string& type, int value);
void TrackMemoryLoad(uintptr_t effective_addr, int loaded_value);
void TrackMemoryStore(uintptr_t effective_addr, int stored_value);

// Import/syscall tracking (for memory-related operations)
void LogWasmImport(const std::string& module_name, const std::string& function_name);
void TrackSystemCall(const std::string& syscall_name, const std::string& args);
void TrackWasiCall(const std::string& wasi_function, const std::string& args);

// Statistics functions
void DumpMemoryAccessStats();
void PrintMemoryStats();
void ClearMemoryStats();

}  // namespace liftoff
}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_LIFTOFF_MEMORY_TRACKER_H_
