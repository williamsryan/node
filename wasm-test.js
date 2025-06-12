// test-wasm-flags.js
// Simple test to verify WASM flags are working

const fs = require('fs');
const path = require('path');

// Simple WASM that does memory operations and function calls
const wasmCode = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,  // WASM magic + version
  0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f,  // Type section
  0x03, 0x02, 0x01, 0x00,  // Function section
  0x05, 0x03, 0x01, 0x00, 0x01,  // Memory section (1 page)
  0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00,  // Export section
  0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b  // Code section
]);

async function testWasmFlags() {
  console.log('Testing WASM flags...');
  
  try {
    const module = await WebAssembly.compile(wasmCode);
    const instance = await WebAssembly.instantiate(module);
    
    console.log('WASM module loaded successfully');
    
    // Call the exported function multiple times to trigger tracing
    for (let i = 0; i < 5; i++) {
      const result = instance.exports.add(i, i + 1);
      console.log(`add(${i}, ${i + 1}) = ${result}`);
    }
    
    // Access memory to trigger memory hooks
    const memory = instance.exports.memory;
    if (memory) {
      const buffer = new Int32Array(memory.buffer);
      for (let i = 0; i < 10; i++) {
        buffer[i] = i * 2;  // Store operations
        const value = buffer[i];  // Load operations
        console.log(`Memory[${i}] = ${value}`);
      }
    }
    
    console.log('WASM test completed');
  } catch (error) {
    console.error('WASM test failed:', error);
  }
}

testWasmFlags();
