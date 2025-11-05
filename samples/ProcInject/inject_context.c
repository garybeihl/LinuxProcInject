//
// Injection Runtime Context Implementation
//
// Provides functions to manage the runtime state container
// for the injection process.
//

#include <Uefi.h>
#include "inject_context.h"
#include "logging.h"

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
)
{
    if (Context == NULL || Config == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    //
    // Initialize all fields explicitly
    // Do NOT use ZeroMem() - it may not work during SetVirtualAddressMap callback
    //
    Context->Signature = INJECT_CONTEXT_SIGNATURE;
    Context->Config = Config;

    // Stack discovery fields
    Context->Stack.StackPointer = NULL;
    Context->Stack.EevmReturnAddr = NULL;
    Context->Stack.EevmStackIndex = 0;
    Context->Stack.StartKernelRetAddr = NULL;

    // Kernel function addresses
    Context->KernelFuncs.Printk = NULL;
    Context->KernelFuncs.Kmalloc = NULL;
    Context->KernelFuncs.Msleep = NULL;
    Context->KernelFuncs.KthreadCreateOnNode = NULL;

    // Kernel init function addresses
    Context->InitFuncs.ArchCallRestInit = NULL;
    Context->InitFuncs.RestInit = NULL;
    Context->InitFuncs.Complete = NULL;
    Context->InitFuncs.ReturnFromPatch = NULL;

    // Patch tracking
    Context->Patches.Patch1Destination = NULL;
    Context->Patches.Patch2Destination = NULL;
    Context->Patches.Patch1Installed = FALSE;
    Context->Patches.Patch2Installed = FALSE;

    // Progress tracking
    Context->CurrentStep = 0;
    Context->StepsCompleted = 0;
    Context->LastError = EFI_SUCCESS;

    LOG_DEBUG("Inject context initialized at 0x%llx", (UINT64)Context);

    return EFI_SUCCESS;
}

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
)
{
    if (Context == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER, "Context pointer is NULL");
        return FALSE;
    }

    if (Context->Signature != INJECT_CONTEXT_SIGNATURE) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context signature: expected 0x%x, got 0x%x",
                 INJECT_CONTEXT_SIGNATURE, Context->Signature);
        return FALSE;
    }

    if (Context->Config == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER, "Context has NULL config");
        return FALSE;
    }

    return TRUE;
}

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
)
{
    if (!ValidateInjectContext(Context)) {
        return;
    }

    //
    // Set the completion bit
    //
    Context->StepsCompleted |= StepBit;

    LOG_VERBOSE("Marked step 0x%x as completed (mask now 0x%x)",
               StepBit, Context->StepsCompleted);
}

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
)
{
    if (!ValidateInjectContext(Context)) {
        return FALSE;
    }

    return (Context->StepsCompleted & StepBit) != 0;
}

/**
 * Reset context for a new injection attempt
 *
 * Clears all runtime state while preserving the configuration
 * reference and signature. Useful for retry scenarios.
 *
 * @param Context   Context to reset
 */
VOID
EFIAPI
ResetInjectContext(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    CONST INJECT_CONFIG* savedConfig;

    if (!ValidateInjectContext(Context)) {
        return;
    }

    //
    // Save the config pointer
    //
    savedConfig = Context->Config;

    //
    // Reset all runtime state explicitly
    // Do NOT use ZeroMem() - it may not work during SetVirtualAddressMap callback
    //

    // Stack discovery fields
    Context->Stack.StackPointer = NULL;
    Context->Stack.EevmReturnAddr = NULL;
    Context->Stack.EevmStackIndex = 0;
    Context->Stack.StartKernelRetAddr = NULL;

    // Kernel function addresses
    Context->KernelFuncs.Printk = NULL;
    Context->KernelFuncs.Kmalloc = NULL;
    Context->KernelFuncs.Msleep = NULL;
    Context->KernelFuncs.KthreadCreateOnNode = NULL;

    // Kernel init function addresses
    Context->InitFuncs.ArchCallRestInit = NULL;
    Context->InitFuncs.RestInit = NULL;
    Context->InitFuncs.Complete = NULL;
    Context->InitFuncs.ReturnFromPatch = NULL;

    // Patch tracking
    Context->Patches.Patch1Destination = NULL;
    Context->Patches.Patch2Destination = NULL;
    Context->Patches.Patch1Installed = FALSE;
    Context->Patches.Patch2Installed = FALSE;

    // Progress tracking
    Context->CurrentStep = 0;
    Context->StepsCompleted = 0;
    Context->LastError = EFI_SUCCESS;

    //
    // Restore config pointer (signature is unchanged)
    //
    Context->Config = savedConfig;

    LOG_DEBUG("Inject context reset");
}

