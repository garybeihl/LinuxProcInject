//
// Injection Runtime Context
//
// Provides a structured container for all runtime state discovered
// during the injection process, replacing scattered global variables.
//
#ifndef _INJECT_CONTEXT_H_
#define _INJECT_CONTEXT_H_

#include <Uefi.h>
#include "kernel_config.h"

//
// Kernel function addresses discovered at runtime
//
typedef struct _KERNEL_FUNCTIONS {
    UINT8* Printk;                  // Address of printk
    UINT8* Kmalloc;                 // Address of __kmalloc
    UINT8* Msleep;                  // Address of msleep
    UINT8* KthreadCreateOnNode;     // Address of kthread_create_on_node
} KERNEL_FUNCTIONS;

//
// Stack discovery results
//
typedef struct _STACK_DISCOVERY {
    UINT64* StackPointer;           // RSP at time of callback
    UINT8* EevmReturnAddr;          // efi_enter_virtual_mode return address
    UINTN EevmStackIndex;           // Stack index where EEVM return addr found
    UINT8* StartKernelRetAddr;      // start_kernel return address
} STACK_DISCOVERY;

//
// Kernel initialization function addresses
//
typedef struct _KERNEL_INIT_FUNCTIONS {
    UINT8* ArchCallRestInit;        // Address of arch_call_rest_init
    UINT8* RestInit;                // Address of rest_init
    UINT8* Complete;                // Address of complete(&kthreadd_done)
    UINT8* ReturnFromPatch;         // Location to return after patch
} KERNEL_INIT_FUNCTIONS;

//
// Patch installation results
//
typedef struct _PATCH_LOCATIONS {
    UINT8* Patch1Destination;       // Where Patch 1 (banner) was installed
    UINT8* Patch2Destination;       // Where Patch 2 (kthread) was installed
    BOOLEAN Patch1Installed;        // TRUE if Patch 1 installed successfully
    BOOLEAN Patch2Installed;        // TRUE if Patch 2 installed successfully
} PATCH_LOCATIONS;

//
// Complete runtime context for injection process
//
typedef struct _INJECT_RUNTIME_CONTEXT {
    //
    // Signature for validation
    //
    UINT32 Signature;
    #define INJECT_CONTEXT_SIGNATURE SIGNATURE_32('I','N','J','C')

    //
    // Configuration (read-only reference)
    //
    CONST INJECT_CONFIG* Config;

    //
    // Discovered addresses and state
    //
    STACK_DISCOVERY Stack;
    KERNEL_FUNCTIONS KernelFuncs;
    KERNEL_INIT_FUNCTIONS InitFuncs;
    PATCH_LOCATIONS Patches;

    //
    // Injection progress tracking
    //
    UINT8 CurrentStep;              // Current step (0-6)
    UINT8 StepsCompleted;           // Bitmask of completed steps
    EFI_STATUS LastError;           // Last error status encountered

} INJECT_RUNTIME_CONTEXT;

//
// Step completion bits for StepsCompleted field
//
#define INJECT_STEP_EEVM_FOUND              BIT0
#define INJECT_STEP_ADDRESSES_CALCULATED    BIT1
#define INJECT_STEP_PATCH1_INSTALLED        BIT2
#define INJECT_STEP_ARCH_CALL_FOUND         BIT3
#define INJECT_STEP_REST_INIT_FOUND         BIT4
#define INJECT_STEP_PATCH2_INSTALLED        BIT5

//
// Validation Constants for Bounds Checking
//
#define INJECT_MAX_STACK_SCAN_DEPTH     0x100   // 256 QWORDS = 2KB max stack scan
#define INJECT_EEVM_SCAN_START          0x28    // Start of EEVM scan range
#define INJECT_EEVM_SCAN_END            0x48    // End of EEVM scan range
#define INJECT_MAX_CALL_SCAN_BYTES      1000    // 1KB max for call pattern scanning
#define INJECT_MAX_FUNCTION_SIZE        2000    // 2KB max expected function size
#define INJECT_MIN_KERNEL_ADDRESS       0xFFFFFFFF80000000ULL  // x86_64 kernel base

//
// Context management functions
//

/**
 * Initialize runtime context
 *
 * @param Context   Context structure to initialize
 * @param Config    Configuration to use (must remain valid)
 * @return EFI_SUCCESS if initialized successfully
 */
EFI_STATUS
EFIAPI
InitializeInjectContext(
    OUT INJECT_RUNTIME_CONTEXT* Context,
    IN  CONST INJECT_CONFIG* Config
);

/**
 * Validate context structure
 *
 * @param Context   Context to validate
 * @return TRUE if context is valid, FALSE otherwise
 */
BOOLEAN
EFIAPI
ValidateInjectContext(
    IN CONST INJECT_RUNTIME_CONTEXT* Context
);

/**
 * Mark a step as completed
 *
 * @param Context   Context structure
 * @param StepBit   Step bit to mark (INJECT_STEP_*)
 */
VOID
EFIAPI
MarkStepCompleted(
    IN OUT INJECT_RUNTIME_CONTEXT* Context,
    IN     UINT8 StepBit
);

/**
 * Check if a step is completed
 *
 * @param Context   Context structure
 * @param StepBit   Step bit to check (INJECT_STEP_*)
 * @return TRUE if step completed, FALSE otherwise
 */
BOOLEAN
EFIAPI
IsStepCompleted(
    IN CONST INJECT_RUNTIME_CONTEXT* Context,
    IN UINT8 StepBit
);

/**
 * Reset context for a new injection attempt
 *
 * @param Context   Context to reset
 */
VOID
EFIAPI
ResetInjectContext(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
);

#endif // _INJECT_CONTEXT_H_
