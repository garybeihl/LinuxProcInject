# Logging System Integration Guide

## Overview

A comprehensive structured logging system has been added to ProcInject to improve debuggability, production readiness, and error diagnosis.

## Components Added

### 1. New Files

- **`logging.h`**: Logging framework header with:
  - 5 log levels (ERROR, WARNING, INFO, DEBUG, VERBOSE)
  - Structured error codes (0x1000-0xF000 ranges)
  - Logging function declarations
  - Convenience macros

- **`logging.c`**: Logging implementation with:
  - Message formatting with timestamps
  - Log level filtering
  - Error code descriptions
  - Function entry/exit tracing

- **`ProcInject.vcxproj`**: Updated to include logging.c and logging.h

### 2. Modified Files

- **`drvmain.c`**:
  - Added `#include "logging.h"`
  - Initialized logging in UefiMain()
  - Added logging to helper functions (partial - see status below)

## Logging Levels

| Level | Purpose | When to Use |
|-------|---------|-------------|
| **ERROR** | Critical failures | Operation cannot continue, patch failed |
| **WARNING** | Potential issues | Unexpected but recoverable situations |
| **INFO** | Progress milestones | Key steps completed successfully |
| **DEBUG** | Detailed debugging | Internal state, calculated addresses |
| **VERBOSE** | Function tracing | Entry/exit of all functions, detailed traces |

## Error Code Ranges

| Range | Category | Description |
|-------|----------|-------------|
| 0x1000-0x1FFF | Stack Scanning | efi_enter_virtual_mode not found, pattern mismatch |
| 0x2000-0x2FFF | Address Calculation | printk, __kmalloc, msleep calculation failures |
| 0x3000-0x3FFF | Patch 1 | Banner patch installation errors |
| 0x4000-0x4FFF | start_kernel Search | arch_call_rest_init not found, mfence missing |
| 0x5000-0x5FFF | rest_init | rest_init not found, complete() not found |
| 0x6000-0x6FFF | Patch 2 | Kernel thread patch installation errors |
| 0x7000-0x7FFF | Configuration | Invalid config, version mismatch |
| 0xF000-0xFFFF | General | Invalid parameters, out of resources |

## Integration Status

### ✅ Completed

1. **logging.h** - Complete logging framework
2. **logging.c** - Full implementation with error code table
3. **ProcInject.vcxproj** - Added logging files to build
4. **UefiMain()** - Initialized logging system at DEBUG level
5. **FindEfiEnterVirtualModeReturnAddr()** - Full logging integration with:
   - Function entry/exit tracing
   - Error codes on failures
   - Verbose stack scanning output
   - Success/failure logging

6. **CalculateKernelFunctionAddresses()** - Full logging integration with:
   - Function entry/exit tracing
   - Address logging for all calculated functions
   - Template fixup confirmation

7. **InstallPatch1_PrintkBanner()** - Full logging integration with:
   - Function entry/exit tracing
   - Verbose progress logging
   - Address fixup confirmation

### 🔄 Remaining Work

The following functions still need logging integration (following the same pattern):

#### 8. FindArchCallRestInit()

**Add to function:**
```c
EFI_STATUS status;
LOG_FUNCTION_ENTRY();

// At start of function:
if (Rsp == NULL || ArchCallRestInit == NULL || StartKernelRetAddr == NULL) {
    LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
             "Invalid parameters to FindArchCallRestInit");
    status = EFI_INVALID_PARAMETER;
    LOG_FUNCTION_EXIT(status);
    return status;
}

LOG_DEBUG("Searching for start_kernel return address");

// In the loop:
LOG_VERBOSE("Checking stack[0x%x] for call pattern", i);

// On success:
LOG_INFO("Found start_kernel return address: 0x%llx", *StartKernelRetAddr);
LOG_ADDRESS(LOG_LEVEL_INFO, "arch_call_rest_init", *ArchCallRestInit);

// On mfence not found:
LOG_ERROR(INJECT_ERROR_MFENCE_NOT_FOUND, "mfence instruction not found after calls");

// On pattern not found:
LOG_ERROR(INJECT_ERROR_START_KERNEL_NOT_FOUND,
         "start_kernel return address not found in stack");

// At end:
status = (found ? EFI_SUCCESS : EFI_NOT_FOUND);
LOG_FUNCTION_EXIT(status);
return status;
```

**Replace existing SerialOutString calls with appropriate LOG_* calls**

#### 9. FindRestInitCompleteCall()

**Add to function:**
```c
EFI_STATUS status;
LOG_FUNCTION_ENTRY();

// Parameter validation:
if (ArchCallRestInit == NULL || RestInit == NULL ||
    CompleteCall == NULL || ReturnFromPatch == NULL) {
    LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
             "Invalid parameters to FindRestInitCompleteCall");
    status = EFI_INVALID_PARAMETER;
    LOG_FUNCTION_EXIT(status);
    return status;
}

LOG_DEBUG("Analyzing arch_call_rest_init prologue");

// On prologue failure:
LOG_ERROR(INJECT_ERROR_REST_INIT_PROLOGUE_INVALID,
         "arch_call_rest_init prologue does not match expected pattern");

// On success:
LOG_ADDRESS(LOG_LEVEL_INFO, "rest_init", *RestInit);
LOG_ADDRESS(LOG_LEVEL_DEBUG, "complete", *CompleteCall);
LOG_DEBUG("Found complete() call at rest_init+0x%x",
         gInjectConfig.KernelConfig->RestInitToCompleteOffset);

// On invalid instruction:
LOG_ERROR(INJECT_ERROR_COMPLETE_INVALID_INSN,
         "Expected call at rest_init+0x%x, found 0x%x",
         gInjectConfig.KernelConfig->RestInitToCompleteOffset, *cp);

// At end:
status = EFI_SUCCESS;
LOG_FUNCTION_EXIT(status);
return status;
```

**Replace existing SerialOutString calls**

#### 10. InstallPatch2_KthreadCreate()

**Add to function:**
```c
EFI_STATUS status;
LOG_FUNCTION_ENTRY();

// Parameter validation:
if (StartKernelRetAddr == NULL || ReturnFromPatch == NULL || CompleteCall == NULL) {
    LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
             "Invalid parameters to InstallPatch2_KthreadCreate");
    status = EFI_INVALID_PARAMETER;
    LOG_FUNCTION_EXIT(status);
    return status;
}

LOG_DEBUG("Installing Patch 2 at 0x%llx", patch_2);
LOG_VERBOSE("Copying proc_template (%d bytes)", sizeof(proc_template));
LOG_VERBOSE("Copying patch_code_2 (%d bytes)", sizeof(patch_code_2));

// After each fixup:
LOG_VERBOSE("Fixed up __kmalloc call");
LOG_VERBOSE("Fixed up kthread_create_on_node call");
LOG_VERBOSE("Fixed up complete() call");
LOG_VERBOSE("Fixed up return jump");

// On success:
LOG_INFO("Patch 2 installed successfully at 0x%llx", patch_2);

// At end:
status = EFI_SUCCESS;
LOG_FUNCTION_EXIT(status);
return status;
```

**Replace existing SerialOutString call**

#### 11. VirtMemCallback()

**Already has structured logging** from our refactoring, but enhance with:

```c
LOG_INFO("=================================================");
LOG_INFO("VirtMemCallback Started");
LOG_INFO("=================================================");

// After each step:
LOG_INFO("Step 1: Finding efi_enter_virtual_mode return address");
// ... (call function)
if (EFI_ERROR(efiStatus)) {
    LOG_ERROR(INJECT_ERROR_EEVM_NOT_FOUND, "Step 1 failed");
    return;
}
LOG_INFO("Step 1: SUCCESS");

LOG_INFO("Step 2: Calculating kernel function addresses");
// ...etc for each step

// At the end:
LOG_INFO("=================================================");
LOG_INFO("All patches installed successfully!");
LOG_INFO("=================================================");
```

## Usage Examples

### Basic Logging

```c
LOG_INFO("Kernel target: %a", versionString);
LOG_DEBUG("Stack pointer: 0x%llx", rsp);
LOG_VERBOSE("Checking candidate address");
```

### Error Logging

```c
LOG_ERROR(INJECT_ERROR_EEVM_NOT_FOUND,
         "Pattern not found at address 0x%llx", addr);
```

### Function Tracing

```c
EFI_STATUS MyFunction(VOID) {
    EFI_STATUS status;
    LOG_FUNCTION_ENTRY();

    // ... function body ...

    status = EFI_SUCCESS;
    LOG_FUNCTION_EXIT(status);
    return status;
}
```

### Address Logging

```c
LOG_ADDRESS(LOG_LEVEL_INFO, "printk", printkAddr);
```

## Configuration

### Changing Log Level

In `UefiMain()`:
```c
// Production (minimal logging):
LogInitialize(LOG_LEVEL_INFO);

// Development (detailed logging):
LogInitialize(LOG_LEVEL_DEBUG);

// Deep debugging (function tracing):
LogInitialize(LOG_LEVEL_VERBOSE);
```

### Runtime Level Changes

```c
LogSetLevel(LOG_LEVEL_VERBOSE);  // Enable verbose mode
// ... perform detailed operation ...
LogSetLevel(LOG_LEVEL_INFO);     // Return to normal
```

## Output Format

### With Timestamps (default)
```
[0001] [INFO ]  ProcInject v0.7 Starting
[0002] [DEBUG]  Scanning stack for efi_enter_virtual_mode return address
[0003] [TRACE]  --> FindEfiEnterVirtualModeReturnAddr()
[0004] [TRACE]  <-- FindEfiEnterVirtualModeReturnAddr() = 0x0
[0005] [ERROR]  [0x1001] efi_enter_virtual_mode return address not found: Pattern mismatch
```

### Error Messages
```
[0042] [ERROR]  [0x4002] mfence instruction not found: Expected mfence after call sequence
```

## Benefits

1. **Structured Error Reporting**: Error codes provide precise failure points
2. **Configurable Verbosity**: Adjust logging detail without recompiling
3. **Function Tracing**: VERBOSE mode shows complete call flow
4. **Production Ready**: INFO level provides essential progress without noise
5. **Debugging Support**: DEBUG/VERBOSE levels provide detailed diagnostics
6. **Serial Output**: All logs go to QEMU debug console for analysis

## Testing Checklist

- [ ] Build compiles successfully with new logging files
- [ ] Logging initializes correctly in UefiMain()
- [ ] INFO level shows key milestones
- [ ] DEBUG level shows detailed address information
- [ ] VERBOSE level shows function entry/exit
- [ ] Error codes appear correctly on failures
- [ ] Log timestamps increment properly
- [ ] Serial output is readable and well-formatted

## Next Steps

1. Complete logging integration for remaining 3 functions
2. Test at all log levels (INFO, DEBUG, VERBOSE)
3. Verify error codes on failure scenarios
4. Review log output for production readiness
5. Consider adding log level command-line parameter
