//
// Kernel Configuration Support
//
// This module provides kernel version detection and offset management
// to support multiple Linux kernel versions.
//
#ifndef _KERNEL_CONFIG_H_
#define _KERNEL_CONFIG_H_

#include <Uefi.h>

//
// Kernel version identifier
//
typedef enum _KERNEL_VERSION {
    KernelVersion_Unknown = 0,
    KernelVersion_5_13_0_30,     // Ubuntu 20.04.4 - initial POC version
    KernelVersion_Max
} KERNEL_VERSION;

//
// Kernel-specific offset configuration
// These offsets are derived from System.map for each kernel version
//
typedef struct _KERNEL_OFFSET_CONFIG {
    //
    // Kernel version information
    //
    KERNEL_VERSION Version;
    CONST CHAR8* VersionString;

    //
    // Function offsets relative to printk
    // These are calculated from System.map as (function_addr - printk_addr)
    //
    INT64 PrintkToKmalloc;              // Offset from printk to __kmalloc
    INT64 PrintkToMsleep;               // Offset from printk to msleep
    INT64 PrintkToKthreadCreateOnNode;  // Offset from printk to kthread_create_on_node

    //
    // Code pattern offsets and sizes
    //
    UINT32 RestInitToCompleteOffset;    // Offset within rest_init to complete(&kthreadd_done)

    //
    // efi_enter_virtual_mode pattern template
    // Bytes that may vary between kernel versions are marked with 0xFF
    //
    UINT8 EfiEnterVirtualModePattern[32];
    UINT8 EfiEnterVirtualModePatternMask[32];  // 0xFF = must match, 0x00 = ignore
    UINTN EfiEnterVirtualModePatternSize;

} KERNEL_OFFSET_CONFIG;

//
// Thread template configuration
//
typedef struct _THREAD_TEMPLATE_CONFIG {
    CONST CHAR8* ThreadName;            // Name of injected thread (e.g., "<UEFI>")
    UINT32 SleepDurationMs;             // Sleep duration in milliseconds
    UINT8* TemplateCode;                // Pointer to code template
    UINTN TemplateSize;                 // Size of template in bytes
} THREAD_TEMPLATE_CONFIG;

//
// Main configuration context
//
typedef struct _INJECT_CONFIG {
    KERNEL_VERSION DetectedVersion;
    CONST KERNEL_OFFSET_CONFIG* KernelConfig;
    CONST THREAD_TEMPLATE_CONFIG* ThreadConfig;
} INJECT_CONFIG;

//
// Public Functions
//

/**
 * Initialize the kernel configuration system
 *
 * @param Config    Pointer to configuration context to initialize
 * @return EFI_SUCCESS if initialized successfully
 */
EFI_STATUS
EFIAPI
InitializeKernelConfig(
    OUT INJECT_CONFIG* Config
);

/**
 * Get kernel offset configuration for a specific version
 *
 * @param Version   Kernel version identifier
 * @return Pointer to offset configuration, or NULL if not found
 */
CONST KERNEL_OFFSET_CONFIG*
EFIAPI
GetKernelOffsetConfig(
    IN KERNEL_VERSION Version
);

/**
 * Detect kernel version based on discovered kernel structures
 * Currently returns the default version; can be extended for auto-detection
 *
 * @param PrintkAddr    Address of discovered printk function
 * @return Detected kernel version
 */
KERNEL_VERSION
EFIAPI
DetectKernelVersion(
    IN UINT8* PrintkAddr
);

/**
 * Calculate kernel function address from printk and offset config
 *
 * @param PrintkAddr    Base address (printk)
 * @param Offset        Signed offset from configuration
 * @return Calculated function address
 */
UINT8*
EFIAPI
CalculateKernelAddress(
    IN UINT8* PrintkAddr,
    IN INT64 Offset
);

#endif // _KERNEL_CONFIG_H_
