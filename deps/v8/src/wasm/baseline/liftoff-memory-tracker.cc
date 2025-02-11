#include <fstream>
#include <unordered_map>

#include "src/wasm/baseline/arm64/liftoff-assembler-arm64-inl.h"

namespace v8::internal::wasm {
namespace liftoff {

struct MemoryAccessInfo {
  uint32_t num_accesses = 0;
  std::optional<int> constant_value = std::nullopt;
  bool constant = true;
};

// Global memory access tracking
static std::unordered_map<uintptr_t, MemoryAccessInfo> memory_accesses;

// Function to log memory accesses
void LogMemoryAccess(uintptr_t address, const std::string& type, int value) {
  std::cout << "[MemHook] { \"addr\": \"0x" << std::hex << address
            << "\", \"type\": \"" << type << "\", \"value\": " << value << " }"
            << std::endl;
}

// Function to track memory loads
void TrackMemoryLoad(uintptr_t effective_addr, int loaded_value) {
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
  auto& entry = memory_accesses[effective_addr];
  entry.num_accesses++;

  if (!entry.constant_value) {
    entry.constant_value = stored_value;
  } else if (*entry.constant_value != stored_value) {
    entry.constant = false;
  }

  LogMemoryAccess(effective_addr, "Store", stored_value);
}

// Dumps memory access statistics at the end of execution
void DumpMemoryAccessStats() {
  std::ofstream file("memory_access_log.json");
  file << "{\n  \"memory_accesses\": {\n";
  for (const auto& [address, info] : memory_accesses) {
    file << "    \"0x" << std::hex << address << std::dec << "\": {"
         << "\"num_accesses\": " << info.num_accesses << ", "
         << "\"constant\": " << (info.constant ? "true" : "false") << "},\n";
  }
  file << "  }\n}\n";
  file.close();

  std::cout << "[MemHook] Memory access stats written to memory_access_log.json"
            << std::endl;
}

}  // namespace liftoff
}  // namespace v8::internal::wasm
