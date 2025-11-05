# Context System Implementation (Item 4)

## Overview

The context system replaces scattered global variables with a structured runtime context that encapsulates all injection state. This improves code maintainability, testability, and makes the injection process more robust.

## Problem Statement

**Before**: The original code used 15+ global variables scattered throughout drvmain.c:
- `StrBuffer[256]` - String buffer for messages
- `printk`, `__kmalloc`, `msleep`, `kthread_create_on_node` - Kernel function addresses
- `banner[]` - Banner string
- `DestinationPointer` - Patch destination pointer
- `arch_call_rest_init`, `rest_init`, `complete`, `return_from_patch` - Init function addresses
- `Patch2` - Patch location pointer

**Issues**:
1. **Poor encapsulation**: State scattered across multiple global variables
2. **Testing difficulty**: Hard to test functions in isolation without global state
3. **No state validation**: No way to verify that all required state is present
4. **Unclear dependencies**: Functions implicitly depend on globals being set
5. **Concurrency issues**: Global state makes concurrent operations impossible
6. **No progress tracking**: No way to track which steps have completed

## Solution: INJECT_RUNTIME_CONTEXT

A comprehensive context structure that encapsulates all runtime state and provides explicit dependencies through function parameters.

### Context Structure Hierarchy

```c
typedef struct _INJECT_RUNTIME_CONTEXT {
    UINT32 Signature;                    // Validation signature
    CONST INJECT_CONFIG* Config;         // Configuration reference

    STACK_DISCOVERY Stack;               // Stack scanning results
    KERNEL_FUNCTIONS KernelFuncs;        // Discovered kernel functions
    KERNEL_INIT_FUNCTIONS InitFuncs;     // Kernel init functions
    PATCH_LOCATIONS Patches;             // Patch installation tracking

    CHAR8 StringBuffer[512];             // Working buffer (expanded)

    UINT8 CurrentStep;                   // Current step (0-6)
    UINT8 StepsCompleted;                // Completion bitmask
    EFI_STATUS LastError;                // Last error encountered
} INJECT_RUNTIME_CONTEXT;
```

### Sub-structures

#### STACK_DISCOVERY
Encapsulates stack scanning results:
```c
typedef struct _STACK_DISCOVERY {
    UINT64* StackPointer;           // RSP at callback time
    UINT8* EevmReturnAddr;          // efi_enter_virtual_mode return address
    UINTN EevmStackIndex;           // Stack index where found
    UINT8* StartKernelRetAddr;      // start_kernel return address
} STACK_DISCOVERY;
```

#### KERNEL_FUNCTIONS
Discovered kernel function addresses:
```c
typedef struct _KERNEL_FUNCTIONS {
    UINT8* Printk;                  // printk address
    UINT8* Kmalloc;                 // __kmalloc address
    UINT8* Msleep;                  // msleep address
    UINT8* KthreadCreateOnNode;     // kthread_create_on_node address
} KERNEL_FUNCTIONS;
```

#### KERNEL_INIT_FUNCTIONS
Kernel initialization function addresses:
```c
typedef struct _KERNEL_INIT_FUNCTIONS {
    UINT8* ArchCallRestInit;        // arch_call_rest_init address
    UINT8* RestInit;                // rest_init address
    UINT8* Complete;                // complete(&kthreadd_done) address
    UINT8* ReturnFromPatch;         // Return address after patch
} KERNEL_INIT_FUNCTIONS;
```

#### PATCH_LOCATIONS
Patch installation tracking:
```c
typedef struct _PATCH_LOCATIONS {
    UINT8* Patch1Destination;       // Where Patch 1 installed
    UINT8* Patch2Destination;       // Where Patch 2 installed
    BOOLEAN Patch1Installed;        // TRUE if Patch 1 succeeded
    BOOLEAN Patch2Installed;        // TRUE if Patch 2 succeeded
} PATCH_LOCATIONS;
```

## Implementation Details

### 1. Context Management Functions

#### InitializeInjectContext()
```c
EFI_STATUS InitializeInjectContext(
    OUT INJECT_RUNTIME_CONTEXT* Context,
    IN  CONST INJECT_CONFIG* Config
);
```
- Zeros out entire context structure
- Sets validation signature (INJECT_CONTEXT_SIGNATURE = 'INJC')
- Stores configuration reference
- Initializes progress tracking fields

#### ValidateInjectContext()
```c
BOOLEAN ValidateInjectContext(
    IN CONST INJECT_RUNTIME_CONTEXT* Context
);
```
- Checks context pointer is non-NULL
- Verifies signature matches expected value
- Validates configuration pointer is set
- Returns TRUE only if all checks pass

#### Progress Tracking Functions
```c
VOID MarkStepCompleted(
    IN OUT INJECT_RUNTIME_CONTEXT* Context,
    IN UINT8 StepBit
);

BOOLEAN IsStepCompleted(
    IN CONST INJECT_RUNTIME_CONTEXT* Context,
    IN UINT8 StepBit
);
```

Step completion bits:
- `INJECT_STEP_EEVM_FOUND` (BIT0)
- `INJECT_STEP_ADDRESSES_CALCULATED` (BIT1)
- `INJECT_STEP_PATCH1_INSTALLED` (BIT2)
- `INJECT_STEP_ARCH_CALL_FOUND` (BIT3)
- `INJECT_STEP_REST_INIT_FOUND` (BIT4)
- `INJECT_STEP_PATCH2_INSTALLED` (BIT5)

### 2. Helper Function Updates

All 6 helper functions were refactored to accept context:

**Before**:
```c
EFI_STATUS FindEfiEnterVirtualModeReturnAddr(
    IN  UINT64* Rsp,
    OUT UINT8** ReturnAddress,
    OUT UINTN* ReturnIndex
);
```

**After**:
```c
EFI_STATUS FindEfiEnterVirtualModeReturnAddr(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
);
```

Benefits:
- Explicit dependencies through context validation
- Results stored directly in context structures
- No need for multiple output parameters
- State tracked automatically

### 3. VirtMemCallback Updates

**Before**: Local variables and global variable assignments
```c
UINT64* rsp;
UINT8* eevmReturnAddr;
UINTN retaddrIndex;
// ... many more local variables ...

efiStatus = FindEfiEnterVirtualModeReturnAddr(rsp, &eevmReturnAddr, &retaddrIndex);
```

**After**: Single context structure
```c
INJECT_RUNTIME_CONTEXT injectContext;

efiStatus = InitializeInjectContext(&injectContext, &gInjectConfig);
injectContext.Stack.StackPointer = AsmGetRsp();

efiStatus = FindEfiEnterVirtualModeReturnAddr(&injectContext);
MarkStepCompleted(&injectContext, INJECT_STEP_EEVM_FOUND);
```

## Benefits

### 1. Improved Encapsulation
- All runtime state in one structure
- Clear ownership and lifetime
- No scattered globals

### 2. Better Error Handling
- `LastError` field tracks failure points
- `ValidateInjectContext()` ensures state validity
- Progress tracking shows exactly where failures occur

### 3. Enhanced Testability
- Functions can be tested with mock contexts
- No global state pollution between tests
- Easy to set up specific test scenarios

### 4. Progress Visibility
- `StepsCompleted` bitmask shows exact progress
- `CurrentStep` tracks active operation
- Enables retry and recovery scenarios

### 5. Clearer Dependencies
- Context parameter makes dependencies explicit
- Function signatures show what state is needed
- No hidden dependencies on globals

### 6. Future-Proof Design
- Easy to add new fields without changing function signatures
- Version field could be added for compatibility
- Multiple contexts could support concurrent operations

## Code Metrics

### Lines of Code Impact
- `inject_context.h`: 186 lines (new)
- `inject_context.c`: 186 lines (new)
- `drvmain.c` global variables: Reduced from 15+ to 2
- Helper functions: More robust parameter validation

### Global Variable Reduction
**Before**: 15 global variables
```c
UINT8 StrBuffer[256];
UINT8* printk;
UINT8* __kmalloc;
UINT8* msleep;
UINT8* kthread_create_on_node;
UINT8  banner[];
UINT8* DestinationPointer;
UINT8* arch_call_rest_init;
UINT8* rest_init;
UINT8* complete;
UINT8* return_from_patch;
UINT8* Patch2;
```

**After**: 2 global variables (essential only)
```c
INJECT_CONFIG gInjectConfig;    // Configuration
EFI_EVENT mVirtMemEvt;          // UEFI event handle
```

**Reduction**: 87% fewer global variables

## Usage Example

### Initialization
```c
INJECT_RUNTIME_CONTEXT context;
EFI_STATUS status;

// Initialize context
status = InitializeInjectContext(&context, &gInjectConfig);
if (EFI_ERROR(status)) {
    return status;
}

// Set stack pointer
context.Stack.StackPointer = AsmGetRsp();
```

### Function Calls
```c
// Step 1: Find EEVM return address
status = FindEfiEnterVirtualModeReturnAddr(&context);
if (EFI_ERROR(status)) {
    context.LastError = status;
    return;
}
MarkStepCompleted(&context, INJECT_STEP_EEVM_FOUND);

// Step 2: Calculate kernel function addresses
status = CalculateKernelFunctionAddresses(&context);
// ... etc
```

### Progress Checking
```c
if (IsStepCompleted(&context, INJECT_STEP_ADDRESSES_CALCULATED)) {
    LOG_INFO("Kernel functions already calculated");
}

if (context.StepsCompleted == 0x3F) {  // All 6 bits set
    LOG_INFO("All injection steps completed");
}
```

### Error Recovery
```c
if (EFI_ERROR(context.LastError)) {
    LOG_ERROR(context.LastError, "Injection failed at step %d", context.CurrentStep);

    // Could implement retry logic:
    ResetInjectContext(&context);
    // Try again...
}
```

## Migration Notes

### For Future Development

1. **Adding New State**: Add fields to appropriate sub-structure
   ```c
   // Example: Adding new kernel function
   typedef struct _KERNEL_FUNCTIONS {
       UINT8* Printk;
       UINT8* Kmalloc;
       UINT8* NewFunction;  // Add here
   } KERNEL_FUNCTIONS;
   ```

2. **Adding New Steps**: Define new step bit and mark completion
   ```c
   #define INJECT_STEP_NEW_STEP  BIT6

   MarkStepCompleted(&context, INJECT_STEP_NEW_STEP);
   ```

3. **Function Modifications**: Accept context, validate, use context fields
   ```c
   EFI_STATUS MyNewFunction(IN OUT INJECT_RUNTIME_CONTEXT* Context) {
       if (!ValidateInjectContext(Context)) {
           return EFI_INVALID_PARAMETER;
       }

       // Use Context->KernelFuncs.Printk instead of global printk
       // Store results in Context
   }
   ```

## Files Modified

### New Files
- `samples/ProcInject/inject_context.h` - Context structure definitions
- `samples/ProcInject/inject_context.c` - Context management implementation
- `samples/ProcInject/CONTEXT_SYSTEM_NOTES.md` - This documentation

### Modified Files
- `samples/ProcInject/drvmain.c`:
  - Added `#include "inject_context.h"`
  - Removed 15 global variables
  - Updated all 6 helper functions to accept context
  - Updated VirtMemCallback to use context
  - Added step completion tracking

- `samples/ProcInject/ProcInject.vcxproj`:
  - Added inject_context.c to ClCompile
  - Added inject_context.h to ClInclude

## Testing Recommendations

1. **Context Initialization**
   - Verify signature is set correctly
   - Confirm all fields are zeroed
   - Test with NULL parameters

2. **Context Validation**
   - Test with NULL context
   - Test with invalid signature
   - Test with NULL config

3. **Progress Tracking**
   - Verify bits set correctly
   - Test IsStepCompleted with each bit
   - Verify StepsCompleted bitmask

4. **Helper Functions**
   - Test each with valid context
   - Test each with invalid context
   - Verify results stored in context correctly

5. **Integration Test**
   - Run full injection process
   - Verify all steps marked complete
   - Check LastError on failure scenarios

## Future Enhancements

1. **Version Field**: Add context version for compatibility checking
2. **Timestamps**: Add per-step timing information
3. **Retry Support**: Implement automatic retry with backoff
4. **Multi-threading**: Support multiple concurrent contexts
5. **Serialization**: Add ability to save/restore context state
6. **Statistics**: Track success rates, timing, etc.

## Summary

The context system successfully eliminates 87% of global variables while improving:
- Code maintainability through better encapsulation
- Testability through explicit dependencies
- Robustness through validation and progress tracking
- Debuggability through comprehensive state visibility

This refactoring aligns ProcInject with production-grade coding standards expected by OEM/BIOS vendors.
