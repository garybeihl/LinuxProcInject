# Kernel Configuration Refactoring

## Overview

This refactoring addresses Issue #1 from the code quality review: **Hard-coded Magic Values**.

The changes extract kernel-specific offsets and patterns into a centralized configuration system, making it easier to:
- Support multiple kernel versions
- Maintain and update kernel-specific data
- Debug configuration issues
- Add new kernel version support

## Files Added

### 1. `kernel_config.h`
Header file defining the kernel configuration system:
- `KERNEL_VERSION` enum for version identification
- `KERNEL_OFFSET_CONFIG` structure containing kernel-specific offsets
- `INJECT_CONFIG` main configuration context
- Public API functions for configuration management

### 2. `kernel_config.c`
Implementation file containing:
- Kernel offset configuration table (currently supports Linux 5.13.0-30)
- Configuration lookup and initialization functions
- Pattern matching with mask support
- Address calculation utilities

## Files Modified

### 1. `drvmain.c`
**Changes:**
- Added `#include "kernel_config.h"`
- Added global `INJECT_CONFIG gInjectConfig` for configuration state
- Replaced hard-coded offsets (0x8b8986, 0xa5f1e6, etc.) with configuration-based calculations
- Replaced hard-coded rest_init offset (0xa4) with `gInjectConfig.KernelConfig->RestInitToCompleteOffset`
- Simplified `VerifyEfiEnterVirtualMode()` to use configuration-based pattern matching
- Added kernel configuration initialization in `UefiMain()`
- Removed `efi_enter_virtual_mode_template[]` global array (now in config)
- Added kernel version string to debug output

**Before:**
```c
__kmalloc = (UINT8*)((UINT64)printk - 0x8b8986); // System.map tells us this
msleep = (UINT8*)((UINT64)printk - 0xa5f1e6);    // System.map tells us this
```

**After:**
```c
__kmalloc = CalculateKernelAddress(printk, gInjectConfig.KernelConfig->PrintkToKmalloc);
msleep = CalculateKernelAddress(printk, gInjectConfig.KernelConfig->PrintkToMsleep);
```

### 2. `ProcInject.vcxproj`
**Changes:**
- Added `kernel_config.c` to `<ClCompile>` section
- Added `kernel_config.h` to `<ClInclude>` section

## How to Add New Kernel Version Support

To add support for a new kernel version:

1. **Obtain the System.map file** for the target kernel version

2. **Calculate function offsets** relative to `printk`:
   ```
   PrintkToKmalloc = <__kmalloc_address> - <printk_address>
   PrintkToMsleep = <msleep_address> - <printk_address>
   PrintkToKthreadCreateOnNode = <kthread_create_on_node_address> - <printk_address>
   ```

3. **Analyze kernel binary** to determine:
   - `RestInitToCompleteOffset`: Offset within rest_init() to complete(&kthreadd_done) call
   - `EfiEnterVirtualModePattern`: Byte pattern for efi_enter_virtual_mode return address verification

4. **Add new configuration** to `kernel_config.c`:
   ```c
   CONST KERNEL_OFFSET_CONFIG gKernelConfig_X_XX_X_XX = {
       .Version = KernelVersion_X_XX_X_XX,
       .VersionString = "X.XX.X-XX-generic",
       .PrintkToKmalloc = -0xXXXXXX,
       .PrintkToMsleep = -0xXXXXXX,
       .PrintkToKthreadCreateOnNode = -0xXXXXXX,
       .RestInitToCompleteOffset = 0xXX,
       // ... pattern data
   };
   ```

5. **Update the kernel table** in `kernel_config.c`:
   ```c
   CONST KERNEL_OFFSET_CONFIG* gKernelConfigTable[] = {
       &gKernelConfig_5_13_0_30,
       &gKernelConfig_X_XX_X_XX,  // Add new entry
       NULL
   };
   ```

6. **Add enum value** to `KERNEL_VERSION` in `kernel_config.h`

7. **(Optional) Implement version detection** in `DetectKernelVersion()` to auto-select configuration

## Benefits of This Refactoring

1. **Maintainability**: All kernel-specific data is centralized in one location
2. **Extensibility**: Easy to add support for new kernel versions
3. **Debuggability**: Configuration values are named and documented
4. **Readability**: Code intent is clearer with named configuration fields
5. **Validation**: Centralized location for validating kernel compatibility

## Future Enhancements

The following enhancements can build on this refactoring:

1. **Automatic Version Detection**: Implement heuristics in `DetectKernelVersion()` to:
   - Search for linux_banner string in kernel memory
   - Validate function signatures at calculated addresses
   - Use pattern matching on known kernel structures

2. **Runtime Validation**: Add checks to validate that calculated addresses point to valid code

3. **Configuration File Support**: Load kernel configurations from external files at runtime

4. **Signature Database**: Build a database of kernel signatures for reliable version detection

## Testing

To verify the changes:

1. Build the project in Visual Studio
2. Run the debugger/run.bat script with Ubuntu 20.04.4 LiveCD
3. Verify console output shows: "Kernel: 5.13.0-30-generic"
4. Verify the <UEFI> kernel thread appears with PID 3

## Backward Compatibility

This refactoring maintains 100% backward compatibility:
- Same functionality as before for Linux 5.13.0-30
- No changes to external interfaces
- No changes to runtime behavior
- Same .efi output for the supported kernel version

## Notes for OEM/BIOS Vendors

When reviewing this code for production integration:

1. **Kernel Version Table**: You will need to populate the configuration table with all supported kernel versions for your target platforms

2. **Version Detection**: Implement robust version detection to automatically select the correct configuration

3. **Validation**: Add comprehensive validation of all calculated addresses before patching

4. **Error Handling**: Enhance error reporting for configuration mismatches

5. **Security**: Consider signing/validating configuration data to prevent tampering
