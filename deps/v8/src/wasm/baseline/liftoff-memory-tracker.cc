// File: deps/v8/src/wasm/baseline/liftoff-memory-tracker.cc
#include <fstream>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>

namespace v8::internal::wasm {
namespace liftoff {

struct MemoryAccessInfo {
  uint32_t num_accesses = 0;
  std::optional<int> constant_value = std::nullopt;
  bool constant = true;
};

// Global memory access tracking
static std::unordered_map<uintptr_t, MemoryAccessInfo> memory_accesses;

// Flag check function to control memory tracking
bool ShouldTrackMemory() {
  static bool checked = false;
  static bool should_track = false;
  if (!checked) {
    const char* env = std::getenv("NODE_WASM_MEMORY_HOOKS");
    should_track = env && std::strcmp(env, "1") == 0;
    checked = true;
    if (should_track) {
      std::cout << "[MemHook] Memory tracking enabled via NODE_WASM_MEMORY_HOOKS=1" << std::endl;
    }
  }
  return should_track;
}

// Function to log memory accesses
void LogMemoryAccess(uintptr_t address, const std::string& type, int value) {
  if (!ShouldTrackMemory()) return;
  
  std::cout << "[MemHook] { \"addr\": \"0x" << std::hex << address
            << "\", \"type\": \"" << type << "\", \"value\": " << std::dec << value << " }"
            << std::endl;
}

// Function to track memory loads
void TrackMemoryLoad(uintptr_t effective_addr, int loaded_value) {
  if (!ShouldTrackMemory()) return;
  
  auto& entry = memory_accesses[effective_addr];
  entry.num_accesses++;

  if (!entry.constant_value) {
    entry.constant_value = loaded_value;
  } else if (*entry.constant_value != loaded_value) {
    entry.constant = false;
  }

  LogMemoryAccess(effective_addr, "Load", loaded_value);
}

// Function to track memory stores
void TrackMemoryStore(uintptr_t effective_addr, int stored_value) {
  if (!ShouldTrackMemory()) return;
  
  auto& entry = memory_accesses[effective_addr];
  entry.num_accesses++;

  if (!entry.constant_value) {
    entry.constant_value = stored_value;
  } else if (*entry.constant_value != stored_value) {
    entry.constant = false;
  }

  LogMemoryAccess(effective_addr, "Store", stored_value);
}

// Function to track WASM imports (memory-related imports only)
void LogWasmImport(const std::string& module_name, const std::string& function_name) {
  if (!ShouldTrackMemory()) return;
  std::cout << "[MemHook] WASM Import: " << module_name << "::" << function_name << std::endl;
}

// Function to track system calls (memory-related syscalls only)
void TrackSystemCall(const std::string& syscall_name, const std::string& args) {
  if (!ShouldTrackMemory()) return;
  
  std::cout << "[MemHook] System call: " << syscall_name << "(" << args << ")" << std::endl;
}

// Function to track WASI calls specifically (memory-related WASI calls)
void TrackWasiCall(const std::string& wasi_function, const std::string& args) {
  if (!ShouldTrackMemory()) return;
  
  std::cout << "[MemHook] WASI call: " << wasi_function << "(" << args << ")" << std::endl;
}

// Dumps memory access statistics at the end of execution
void DumpMemoryAccessStats() {
  if (!ShouldTrackMemory()) return;
  
  if (memory_accesses.empty()) {
    std::cout << "[MemHook] No memory accesses to dump" << std::endl;
    return;
  }
  
  std::ofstream file("memory_access_log.json");
  if (!file.is_open()) {
    std::cout << "[MemHook] Failed to open memory_access_log.json for writing" << std::endl;
    return;
  }
  
  file << "{\n  \"memory_accesses\": {\n";
  bool first = true;
  for (const auto& [address, info] : memory_accesses) {
    if (!first) {
      file << ",\n";
    }
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
  file << "  \"total_addresses\": " << memory_accesses.size() << ",\n";
  file << "  \"total_accesses\": " << [&]() {
    uint64_t total = 0;
    for (const auto& [addr, info] : memory_accesses) {
      total += info.num_accesses;
    }
    return total;
  }() << "\n";
  file << "}\n";
  file.close();

  std::cout << "[MemHook] Memory access stats written to memory_access_log.json" << std::endl;
}

// Function to get statistics without dumping to file
void PrintMemoryStats() {
  if (!ShouldTrackMemory()) return;
  
  uint64_t total_accesses = 0;
  uint32_t constant_addresses = 0;
  
  for (const auto& [address, info] : memory_accesses) {
    total_accesses += info.num_accesses;
    if (info.constant) {
      constant_addresses++;
    }
  }
  
  std::cout << "[MemHook] Memory Stats - Addresses: " << memory_accesses.size()
            << ", Total Accesses: " << total_accesses
            << ", Constant Addresses: " << constant_addresses << std::endl;
}

// Function to clear memory tracking data
void ClearMemoryStats() {
  if (!ShouldTrackMemory()) return;
  
  memory_accesses.clear();
  std::cout << "[MemHook] Memory tracking data cleared" << std::endl;
}

}  // namespace liftoff
}  // namespace v8::internal::wasm
