# Naming Conventions Analysis and Standardization Plan

## Overview

This document analyzes current naming patterns in ProcInject, identifies inconsistencies, and proposes a standardized naming convention aligned with UEFI/EDK-II coding standards.

## Current State Analysis

### 1. Function Names

**Current Pattern**: Mostly consistent PascalCase

✅ **Consistent Examples**:
- `FindEfiEnterVirtualModeReturnAddr()`
- `CalculateKernelFunctionAddresses()`
- `InstallPatch1_PrintkBanner()`
- `InstallPatch2_KthreadCreate()`
- `SerialOutByte()`
- `SerialOutString()`
- `VerifyEfiEnterVirtualMode()`

✅ **UEFI Standard**: Functions use PascalCase - **GOOD**

### 2. Type Names

**Current Pattern**: SCREAMING_SNAKE_CASE with optional underscore prefix

✅ **Consistent Examples**:
```c
typedef struct _INJECT_RUNTIME_CONTEXT { ... } INJECT_RUNTIME_CONTEXT;
typedef struct _KERNEL_FUNCTIONS { ... } KERNEL_FUNCTIONS;
typedef struct _STACK_DISCOVERY { ... } STACK_DISCOVERY;
typedef enum _LOG_LEVEL { ... } LOG_LEVEL;
typedef enum _INJECT_ERROR_CODE { ... } INJECT_ERROR_CODE;
```

✅ **UEFI Standard**: Types use SCREAMING_SNAKE_CASE - **GOOD**

### 3. Global Variables

**Current Pattern**: INCONSISTENT - Mix of 'g', 'm', and '_g' prefixes

❌ **Inconsistent Examples**:
- `gInjectConfig` - 'g' prefix (global, mutable)
- `gEfiCallerBaseName` - 'g' prefix
- `mSysTable` - 'm' prefix (module-level)
- `mVirtMemEvt` - 'm' prefix (module-level)
- `_gDriverUnloadImageCount` - '_g' prefix + const
- `_gUefiDriverRevision` - '_g' prefix + const
- `_gDxeRevision` - '_g' prefix + const

**UEFI Standards**:
- 'g' prefix = global, mutable
- 'm' prefix = module-level, mutable
- 'g' + const = should not use 'g', just const declaration
- Leading underscore for special EDK-II variables

❌ **Problem**: Mixing 'g' and 'm' for similar purposes

### 4. Local Variables and Parameters

**Current Pattern**: INCONSISTENT - Mix of PascalCase and camelCase

❌ **Inconsistent Examples**:

**Parameters**:
- `IN OUT INJECT_RUNTIME_CONTEXT* Context` - PascalCase ✅
- `IN UINT8* cp` - camelCase ❌
- `IN UINT64* Rsp` - PascalCase ✅
- `IN UINT8 c` - lowercase ❌

**Local Variables**:
- `UINTN i;` - lowercase ✅ (loop counter convention)
- `UINT8* candidateAddr;` - camelCase ❌
- `EFI_STATUS status;` - lowercase ✅
- `UINT8* cp;` - lowercase ✅
- `UINT64* Rsp;` - PascalCase ❌
- `INT32 offset;` - lowercase ✅
- `UINT8* patch_2;` - snake_case ❌

**UEFI Standard**: PascalCase for parameters, PascalCase or camelCase for locals

❌ **Problem**: Heavy inconsistency in local variable naming

### 5. Macro and Constant Names

**Current Pattern**: Mostly SCREAMING_SNAKE_CASE

✅ **Consistent Examples**:
```c
#define INJECT_MAX_STACK_SCAN_DEPTH     0x100
#define INJECT_EEVM_SCAN_START          0x28
#define INJECT_MIN_KERNEL_ADDRESS       0xFFFFFFFF80000000ULL
#define COM1_PORT                       0x402
#define LSR_OFFSET                      5
#define LSR_THRE                        0x20
#define PUT_FIXUP(cp, addr)             ...
```

✅ **UEFI Standard**: SCREAMING_SNAKE_CASE for macros - **GOOD**

### 6. Structure Field Names

**Current Pattern**: PascalCase

✅ **Consistent Examples**:
```c
typedef struct _INJECT_RUNTIME_CONTEXT {
    UINT32 Signature;
    CONST INJECT_CONFIG* Config;
    STACK_DISCOVERY Stack;
    KERNEL_FUNCTIONS KernelFuncs;
    // ...
} INJECT_RUNTIME_CONTEXT;
```

✅ **UEFI Standard**: PascalCase for struct fields - **GOOD**

## Identified Issues Summary

### High Priority Issues

1. **Local Variable Naming Inconsistency**
   - Mix of PascalCase (Rsp, Context), camelCase (candidateAddr, efiStatus), and snake_case (patch_2)
   - **Impact**: Reduces code readability
   - **Severity**: Medium

2. **Function Parameter Inconsistency**
   - Mix of PascalCase (Context, Rsp) and lowercase/camelCase (cp, c)
   - **Impact**: Makes function signatures look inconsistent
   - **Severity**: Medium

3. **Global Variable Prefix Inconsistency**
   - Mix of 'g' prefix (gInjectConfig) and 'm' prefix (mVirtMemEvt) for similar purposes
   - **Impact**: Unclear variable scope and mutability
   - **Severity**: Low (semantic issue, not functional)

### Low Priority Issues

4. **Mixed Abbreviations**
   - Sometimes full names (candidateAddr), sometimes short (cp, Rsp)
   - **Impact**: Reduces clarity
   - **Severity**: Low

## Recommended Naming Standard

### UEFI/EDK-II Aligned Standard

Based on EDK-II C Coding Standards Specification v2.2:

#### 1. Functions
- **Convention**: PascalCase
- **Example**: `FindEfiEnterVirtualModeReturnAddr()`
- **Status**: ✅ Already compliant

#### 2. Types
- **Convention**: SCREAMING_SNAKE_CASE
- **Example**: `INJECT_RUNTIME_CONTEXT`, `LOG_LEVEL`
- **Status**: ✅ Already compliant

#### 3. Global Variables
- **Convention**:
  - 'g' prefix for global mutable: `gInjectConfig`
  - 'm' prefix for module-level mutable: `mSysTable`
  - No prefix for const, just const keyword: `const UINT32 Version = 1;`
  - '_g' prefix for EDK-II special variables only: `_gDriverUnloadImageCount`
- **Recommendation**: Standardize on 'g' for globals, 'm' for module-level
- **Status**: ❌ Needs standardization

#### 4. Function Parameters
- **Convention**: PascalCase with descriptive names
- **Example**: `IN OUT INJECT_RUNTIME_CONTEXT* Context`
- **Exception**: Single-char params OK for simple cases: `IN UINT8 Byte`
- **Recommendation**: Use PascalCase consistently
- **Status**: ❌ Needs fixes

#### 5. Local Variables
- **Convention**: PascalCase for named variables, lowercase for common patterns
- **Examples**:
  - `EFI_STATUS Status;` (PascalCase)
  - `UINTN Index;` (PascalCase)
  - `UINT8* Buffer;` (PascalCase)
  - **Common exceptions**:
    - Loop counters: `i`, `j`, `k` (lowercase)
    - Status: `status` (lowercase, extremely common pattern)
    - Common temps: `cp` (code pointer), `offset`
- **Recommendation**: PascalCase for descriptive names, lowercase for standard patterns
- **Status**: ❌ Needs standardization

#### 6. Macros and Constants
- **Convention**: SCREAMING_SNAKE_CASE
- **Example**: `INJECT_MAX_STACK_SCAN_DEPTH`
- **Status**: ✅ Already compliant

#### 7. Structure Fields
- **Convention**: PascalCase
- **Example**: `Context->Stack.StackPointer`
- **Status**: ✅ Already compliant

## Proposed Renames

### Category 1: Local Variables (High Impact on Readability)

#### In FindEfiEnterVirtualModeReturnAddr():
- `candidateAddr` → `CandidateAddress` (PascalCase)
- Keep `i` (loop counter exception)
- Keep `status` (common pattern)
- Keep `Rsp` (already PascalCase)

#### In CalculateKernelFunctionAddresses():
- `readAddr` → `ReadAddress` (PascalCase)
- Keep `offset` (common pattern)
- Keep `cp` (code pointer, common pattern)
- Keep `status` (common pattern)

#### In InstallPatch1_PrintkBanner():
- `destptr` → `DestinationPointer` or `DestPtr` (PascalCase)
- Keep `i`, `j` (loop counters)
- Keep `cp` (code pointer)
- Keep `status` (common pattern)

#### In FindArchCallRestInit():
- Keep `i`, `j` (loop counters)
- Keep `cp` (code pointer)
- Keep `offset` (common pattern)
- Keep `status` (common pattern)

#### In InstallPatch2_KthreadCreate():
- `patch_2` → `Patch2Address` or `Patch2` (PascalCase, avoid snake_case)
- Keep `i` (loop counter)
- Keep `cp` (code pointer)
- Keep `status` (common pattern)

#### In VirtMemCallback():
- `rsp` → `Rsp` (PascalCase) - **but this might be intentional for locals vs params**
- `efiStatus` → keep (already good camelCase, or change to `Status`)

### Category 2: Function Parameters (Medium Impact)

#### Global Changes:
- `cp` (as parameter) → `CodePointer` or `CodePtr` (in VerifyEfiEnterVirtualMode)
- `c` (as parameter) → `Byte` (in SerialOutByte)

### Category 3: Global Variables (Low Impact, Semantic Clarity)

**Current State**:
- `gInjectConfig` - global config (keep 'g')
- `mSysTable` - module table (keep 'm')
- `mVirtMemEvt` - module event (keep 'm')
- `_gDriverUnloadImageCount` - EDK-II special (keep '_g')
- `_gUefiDriverRevision` - EDK-II special (keep '_g')
- `_gDxeRevision` - EDK-II special (keep '_g')
- `gEfiCallerBaseName` - global name (keep 'g')

**Recommendation**: No changes needed - current usage is semantically correct

## Implementation Strategy

### Phase 1: High-Impact Local Variables (Priority 1)

Rename inconsistent local variables to improve readability:
- Focus on variables that appear multiple times
- Focus on snake_case → PascalCase conversions
- Preserve common patterns (i, j, k, status, offset, cp)

**Estimated**: ~15 renames across 6 functions

### Phase 2: Function Parameters (Priority 2)

Standardize function parameter names:
- Single-char params → Descriptive names (except very simple cases)
- camelCase params → PascalCase

**Estimated**: ~5 renames

### Phase 3: Documentation Update (Priority 3)

Update documentation to reflect naming standards:
- Add NAMING_CONVENTIONS.md guide
- Update code comments if needed

## Naming Convention Reference Guide

### Quick Reference Table

| Element | Convention | Example | Notes |
|---------|-----------|---------|-------|
| Function | PascalCase | `FindEfiEnterVirtualMode()` | Already compliant |
| Type | SCREAMING_SNAKE_CASE | `INJECT_RUNTIME_CONTEXT` | Already compliant |
| Global Var | g-prefix | `gInjectConfig` | Mutable globals |
| Module Var | m-prefix | `mSysTable` | Module-level |
| EDK-II Var | _g-prefix | `_gDriverUnloadImageCount` | Special EDK-II |
| Parameter | PascalCase | `IN UINT8* Buffer` | Descriptive names |
| Local Var | PascalCase | `UINT8* DestinationPointer` | Descriptive vars |
| Local Var | lowercase | `status`, `offset`, `cp` | Common patterns |
| Loop Var | lowercase | `i`, `j`, `k` | Standard counters |
| Macro | SCREAMING_SNAKE_CASE | `INJECT_MAX_DEPTH` | Already compliant |
| Struct Field | PascalCase | `Context->Stack` | Already compliant |

### Common Patterns (Exceptions to PascalCase)

These lowercase patterns are acceptable for local variables due to widespread usage:

- `status` - EFI_STATUS return value
- `offset` - Offset calculations
- `cp` - Code pointer (assembly tradition)
- `i`, `j`, `k` - Loop counters
- `found` - Boolean flags

## Benefits of Standardization

1. **Improved Readability**: Consistent naming makes code easier to scan
2. **UEFI Compliance**: Aligns with EDK-II coding standards
3. **Onboarding**: Easier for new developers to understand conventions
4. **Maintainability**: Clear patterns reduce cognitive load
5. **Professionalism**: Production-ready code quality for OEM review

## Risk Assessment

**Risk Level**: Low
- Renaming is mechanical and low-risk
- Changes are local within functions
- No API changes (only internal variables)
- Compiler will catch any missed renames

**Testing Required**:
- Compilation test (catches syntax errors)
- Functional test (ensure behavior unchanged)
- Code review (verify naming consistency)

## Conclusion

Current naming is ~70% compliant with UEFI standards. Main issues are:
1. Local variable inconsistency (PascalCase vs camelCase vs snake_case)
2. Some function parameter inconsistency

Recommended approach:
- Fix high-impact local variables (snake_case → PascalCase)
- Standardize descriptive parameter names
- Preserve common patterns (status, offset, cp, i, j, k)
- Document the standard for future development

**Estimated Effort**: ~30-40 renames, low risk, high readability improvement
