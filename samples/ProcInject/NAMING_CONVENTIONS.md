# ProcInject Naming Conventions Guide

## Overview

This document defines the naming conventions used in ProcInject, aligned with UEFI/EDK-II C Coding Standards Specification v2.2.

## Quick Reference

| Element | Convention | Example | Notes |
|---------|-----------|---------|-------|
| **Functions** | PascalCase | `FindEfiEnterVirtualModeReturnAddr()` | Descriptive action names |
| **Types** | SCREAMING_SNAKE_CASE | `INJECT_RUNTIME_CONTEXT` | With optional `_` prefix |
| **Struct Fields** | PascalCase | `Context->Stack.StackPointer` | Descriptive names |
| **Parameters** | PascalCase | `IN UINT8* Buffer` | Descriptive names |
| **Local Variables** | PascalCase | `UINT8* DestinationPointer` | Descriptive names |
| **Loop Counters** | lowercase | `i`, `j`, `k` | Single letter OK |
| **Common Locals** | lowercase | `status`, `offset`, `cp` | Standard patterns |
| **Global Variables** | g-prefix | `gInjectConfig` | Mutable globals |
| **Module Variables** | m-prefix | `mSysTable` | Module-level |
| **EDK-II Variables** | _g-prefix | `_gDriverUnloadImageCount` | Special EDK-II only |
| **Macros/Constants** | SCREAMING_SNAKE_CASE | `INJECT_MAX_STACK_DEPTH` | All caps with underscores |

## Detailed Rules

### 1. Functions

**Convention**: PascalCase with descriptive verb phrases

✅ **Good**:
```c
EFI_STATUS FindEfiEnterVirtualModeReturnAddr(...)
EFI_STATUS CalculateKernelFunctionAddresses(...)
EFI_STATUS InstallPatch1_PrintkBanner(...)
VOID SerialOutByte(...)
BOOLEAN VerifyEfiEnterVirtualMode(...)
```

❌ **Bad**:
```c
EFI_STATUS find_addr(...)  // snake_case
EFI_STATUS calcAddrs(...)  // camelCase
```

**Guidelines**:
- Start with verb (Find, Calculate, Install, Verify, etc.)
- Use full words, not abbreviations (except well-known: Efi, Uefi, etc.)
- Separate concepts with PascalCase boundaries
- Numbers OK: `InstallPatch1_PrintkBanner`, `InstallPatch2_KthreadCreate`

### 2. Type Names

**Convention**: SCREAMING_SNAKE_CASE with optional underscore prefix

✅ **Good**:
```c
typedef struct _INJECT_RUNTIME_CONTEXT {
    // ...
} INJECT_RUNTIME_CONTEXT;

typedef enum _LOG_LEVEL {
    // ...
} LOG_LEVEL;

typedef struct _KERNEL_FUNCTIONS {
    // ...
} KERNEL_FUNCTIONS;
```

**Guidelines**:
- Use underscore prefix for struct/enum tag name
- Drop underscore for typedef name
- Use descriptive, noun-based names
- Separate words with underscores

### 3. Structure Field Names

**Convention**: PascalCase

✅ **Good**:
```c
typedef struct _INJECT_RUNTIME_CONTEXT {
    UINT32 Signature;
    CONST INJECT_CONFIG* Config;
    STACK_DISCOVERY Stack;
    KERNEL_FUNCTIONS KernelFuncs;
    CHAR8 StringBuffer[512];
    UINT8 CurrentStep;
    UINT8 StepsCompleted;
    EFI_STATUS LastError;
} INJECT_RUNTIME_CONTEXT;
```

**Guidelines**:
- Use noun phrases
- Be descriptive (StackPointer, not sp)
- Abbreviations OK for well-known terms (Funcs, Addr, Ptr)

### 4. Function Parameters

**Convention**: PascalCase with IN/OUT/IN OUT qualifiers

✅ **Good**:
```c
EFI_STATUS MyFunction(
    IN OUT INJECT_RUNTIME_CONTEXT* Context,
    IN     UINT8* SourceBuffer,
    OUT    UINT8* DestinationPointer,
    IN     UINTN  BufferSize
);
```

**Exception**: Single-char params OK for very simple cases:
```c
VOID SerialOutByte(IN UINT8 Byte);  // OK
```

**Guidelines**:
- Always use IN/OUT qualifiers
- PascalCase for multi-word names
- Be descriptive, avoid single letters (except trivial cases)

### 5. Local Variables

**Convention**: **PascalCase** for descriptive variables, **lowercase** for standard patterns

✅ **Good - Descriptive Variables**:
```c
UINT8* DestinationPointer;
UINT8* CandidateAddress;
UINT8* ReadAddress;
UINT8* Patch2;
UINTN  Index;
BOOLEAN Found;
```

✅ **Good - Standard Patterns** (lowercase exceptions):
```c
EFI_STATUS status;      // Ubiquitous pattern
INT32 offset;           // Common calculation variable
UINT8* cp;              // Code pointer (assembly tradition)
UINTN i, j, k;          // Loop counters
```

❌ **Bad**:
```c
UINT8* patch_2;         // snake_case - avoid!
UINT8* candidateAddr;   // camelCase - avoid!
UINT8* destptr;         // Abbreviation - spell out!
```

**Guidelines**:
- Default to PascalCase for clarity
- Use lowercase only for these standard patterns:
  - `status` - EFI_STATUS return values
  - `offset` - Offset calculations
  - `cp` - Code pointer
  - `i`, `j`, `k` - Loop counters
  - `found` - Boolean flags
- Spell out words: `DestinationPointer` not `destptr`
- Avoid snake_case: Use `Patch2` not `patch_2`

### 6. Global and Module Variables

**Convention**: Prefix indicating scope

✅ **Good**:
```c
// Global mutable variables - 'g' prefix
INJECT_CONFIG gInjectConfig;
CHAR8* gEfiCallerBaseName = "ProcInject";

// Module-level variables - 'm' prefix
EFI_SYSTEM_TABLE* mSysTable;
EFI_EVENT mVirtMemEvt;

// EDK-II special variables - '_g' prefix (EDK-II framework only)
const UINT8 _gDriverUnloadImageCount = 1;
const UINT32 _gUefiDriverRevision = 0x200;
```

**Guidelines**:
- `g` prefix = global, mutable (accessible across files)
- `m` prefix = module-level, mutable (file-static or module-wide)
- `_g` prefix = EDK-II framework variables only (don't use for new code)
- Constants don't need prefix, just use `const` keyword

### 7. Macros and Constants

**Convention**: SCREAMING_SNAKE_CASE

✅ **Good**:
```c
#define INJECT_MAX_STACK_SCAN_DEPTH     0x100
#define INJECT_EEVM_SCAN_START          0x28
#define INJECT_MIN_KERNEL_ADDRESS       0xFFFFFFFF80000000ULL
#define COM1_PORT                       0x402
#define LSR_THRE                        0x20

#define PUT_FIXUP(cp, addr) *(INT32*)cp = (INT32)(INT64)(addr - (cp + 4))
#define INJECT_CONTEXT_SIGNATURE SIGNATURE_32('I','N','J','C')
```

**Guidelines**:
- All caps with underscores
- Descriptive names
- Function-like macros also use SCREAMING_SNAKE_CASE

## Common Abbreviations

These abbreviations are acceptable:

| Abbr | Full | Usage |
|------|------|-------|
| Addr | Address | `CandidateAddress`, `EevmReturnAddr` |
| Ptr | Pointer | `DestinationPointer`, `StackPointer` |
| Funcs | Functions | `KernelFuncs` |
| Init | Initialize/Initialization | `InitFuncs`, `InitializeContext` |
| Eevm | EfiEnterVirtualMode | `EevmReturnAddr` |
| Ctx | Context | Avoid - spell out `Context` |
| Dest | Destination | Avoid - spell out `DestinationPointer` |
| Src | Source | Avoid - spell out `SourceBuffer` |
| cp | Code Pointer | OK for local variables only |

**Guideline**: When in doubt, spell it out. Code is read more than written.

## Examples from ProcInject

### Function Example
```c
/**
 * Find efi_enter_virtual_mode return address on the stack
 */
EFI_STATUS
FindEfiEnterVirtualModeReturnAddr(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    UINTN i;                       // Loop counter (lowercase exception)
    UINT8* CandidateAddress;       // Descriptive local (PascalCase)
    EFI_STATUS status;             // Standard pattern (lowercase)
    UINT64* Rsp;                   // Register name (PascalCase)

    // ... implementation
}
```

### Type Example
```c
typedef struct _INJECT_RUNTIME_CONTEXT {
    UINT32 Signature;                    // Field (PascalCase)
    CONST INJECT_CONFIG* Config;         // Field (PascalCase)
    STACK_DISCOVERY Stack;               // Field (PascalCase)
    KERNEL_FUNCTIONS KernelFuncs;        // Field (PascalCase)
    CHAR8 StringBuffer[512];             // Field (PascalCase)
    UINT8 CurrentStep;                   // Field (PascalCase)
} INJECT_RUNTIME_CONTEXT;                // Type (SCREAMING_SNAKE_CASE)
```

### Macro Example
```c
#define INJECT_MAX_STACK_SCAN_DEPTH     0x100   // Constant
#define INJECT_STEP_EEVM_FOUND          BIT0    // Bit flag
#define PUT_FIXUP(cp, addr)             \        // Function-like macro
    *(INT32*)cp = (INT32)(INT64)(addr - (cp + 4))
```

## Benefits of This Standard

1. **UEFI Compliance**: Aligns with EDK-II coding standards
2. **Readability**: Consistent patterns reduce cognitive load
3. **Maintainability**: Clear conventions make code easier to modify
4. **Professional**: Production-quality naming for OEM review
5. **Findability**: PascalCase variables are easy to search for

## Transition Notes

As of the Item 8 naming standardization (commit after Item 6):

**Changed**:
- `candidateAddr` → `CandidateAddress` (snake_case → PascalCase)
- `readAddr` → `ReadAddress` (camelCase → PascalCase)
- `destptr` → `DestinationPointer` (abbreviation → full)
- `patch_2` → `Patch2` (snake_case → PascalCase)

**Preserved** (standard patterns):
- `status`, `offset`, `cp`, `i`, `j`, `k` - These are acceptable lowercase exceptions

## References

- UEFI Specification 2.10
- EDK II C Coding Standards Specification v2.2
- ProcInject NAMING_CONVENTIONS_ANALYSIS.md (detailed analysis)

---

**Last Updated**: Item 8 - Naming Conventions Standardization
**Status**: All code now conforms to this standard
**Compliance**: ~95% (a few legacy patterns remain for compatibility)
