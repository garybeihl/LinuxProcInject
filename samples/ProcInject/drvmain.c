#include "drv.h"
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

UINT8 efi_enter_virtual_mode_template[] = {
    0x48, 0x89, 0xc6, 0x48, 0x85, 0xc0, 0x74, 0x0e,
    0x48, 0xc7, 0xc7, 0x40, 0xcd, 0x9b, 0xb1, 0xe8,  // bytes 13 and 14 can vary
    0x7d, 0x63, 0x9b, 0xfe, 0xeb, 0x2d, 0xe8, 0x80,
    0x39, 0x04, 0x00, 0xe8, 0x62, 0xf1, 0xff, 0xff
};

UINT8 printk_banner_template[] = {
    0x50, // push rax
    0x48, 0xc7, 0xc7, 0x00, 0x00, 0x00, 0x00, // mov rdi, <banner>
    0xe8, 0x00, 0x00, 0x00, 0x00,  // call printk
    0x58  // pop rax
};

UINT8 proc_template[] = {
	    0x57, // push rdi
	// loop1:
		0x48, 0xBF, 0x60, 0x51, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rdi, 86400 (1 day)
		0xe8, 0x00, 0x00, 0x00, 0x00, // call ssleep(86400)
		0xe9, 0xEC, 0xFF, 0xFF, 0xFF, // jmp loop1
		0x5f, // pop rdi
		0x48, 0x31, 0xc0, // xor rax,rax
		0xc3  // ret
};

UINT8 patch_code_2[] = {
	0x57, // push rdi
	// ...TODO: Code to allocate new kernel thread goes here
	// tfunc = __kmalloc(sizeof(proc_template), GPF_KERNEL | GPF_ZERO = 0x400CC0
	// <Copy proc_template into tfunc space, fixing up call to ssleep>
	// task = kthread_create_on_node(tfunc, 0, -1, "<UEFI>"); 
	// wakeup_process(task); // TODO: check task pointer for error
	//
	0x5f, // pop rdi
	0xe8, 0x00, 0x00, 0x00, 0x00, // call complete(&kthreadd_done)
	0xe9, 0x00, 0x00, 0x00, 0x00  // jmp back into rest_init() code
};

UINT8 StrBuffer[256]; // For debug messages
UINT8* printk;        // Address of the printk routine in linux boot
UINT8 banner[] = "\001\063ProcInject v0.6\n";
UINT8* destptr;
UINT8* arch_call_rest_init = NULL;
UINT8* rest_init = NULL;
UINT8* complete = NULL;
UINT8* patch_point_2;
UINT8* patch_2;
EFI_EVENT mVirtMemEvt;

#define PUT_FIXUP(cp, addr) *(INT32*)cp = (INT32)(INT64)(addr - (cp + 4))

BOOLEAN
VerifyEfiEnterVirtualMode(UINT8* cp) {
    int i;
    UINT8 *origcp = cp;
    UINT64 rptr;

    for (i = 0; i < sizeof(efi_enter_virtual_mode_template); i++) {
        if ((*cp != efi_enter_virtual_mode_template[i]) && (i != 13) && (i != 14)) {
            return FALSE;
        }
        cp++;
    }
    rptr = (UINT64)(INT64) * (INT32*)(origcp + 0xb); // mov rdi, 0xffffffffxxxxxxxx instruction
    rptr += 2; // Getting ready to call printk from efi_enter_virtual_mode?
    if (AsciiStrCmp((UINT8*)rptr, "efi: Unable to switch EFI into virtual mode (status=%lx)!\n")) {
        return FALSE;
    }
    return TRUE;
}

VOID
EFIAPI
VirtMemCallback(
	IN EFI_EVENT Event,
	IN VOID* Context
) {
	UINT64* rsp;
	BOOLEAN found;
	INT32 offset;
	UINT8* eevm_retaddr; // enter_efi_virtual_mode return address
	UINT8* start_kernel_retaddr;
	UINT8* cp;

	int i, j, retaddr_index;

	// We are called from RuntimeDriverSetVirtualAddressMap() (return addr @ *$esp)
	// Further up the call frame stack is the return address into efi_enter_virtual_mode in linux boot code.
	// Scan for the return address into efi_enter_virtual_mode.
	// We are looking for code similar to the following (efi_enter_virtual_mode_template above):
	//
	//    0xffffffffb63eb9c5:  mov    rsi, rax
	//    0xffffffffb63eb9c8 : test   rax, rax
	//    0xffffffffb63eb9cb : je     0xffffffffb63eb9db
	//    0xffffffffb63eb9cd : mov    rdi, 0xffffffffb57bcd40 <- Verify "\0013efi: Unable to switch EFI into virtual mode(status = % lx)!\n"
	//    0xffffffffb63eb9d4 : call   0xffffffffb4da1d56 <- printk addresss
	//    0xffffffffb63eb9d9 : jmp    0xffffffffb63eba08
	//    0xffffffffb63eb9db : call   0xffffffffb642f360

	rsp = AsmGetRsp();
	printk = NULL;

	// Search for the return address into efi_enter_virtual_mode
	for (i = 0x28; i < 0x48; i++) {
		if ((rsp[i] & 0xFFFFFFFF00000000L) == 0xFFFFFFFF00000000L) {
			eevm_retaddr = (UINT8*)rsp[i];
			found = VerifyEfiEnterVirtualMode(eevm_retaddr);
			if (found) {
				retaddr_index = i;
				break;
			}
		}
	}
	if (found) {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Found efi_enter_virtual_mode return addr @ 0x%llx!\n", eevm_retaddr);
		SerialOutString(StrBuffer);
		offset = *(INT32*)(eevm_retaddr + 0x10);
		printk = (eevm_retaddr + 0x14) + offset;
		AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Offset = 0x%lx, printk = 0x%llx, retaddr_index = 0x%x\n", offset, printk, retaddr_index);
		SerialOutString(StrBuffer);
	}
	else {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Did NOT find efi_enter_virtual_mode return addr\n");
		SerialOutString(StrBuffer);
		return; // We didn't find what we were looking for....
	}

	// We are in a firmware context here, not kernel, so we can't call linux kernel code directly.
	// Instead, we patch the linux code to printk our entry message when it returns to efi_enter_virtual_mode
	// This requires adjusting the efi_enter_virtual_mode return address on the stack.
	// There is no change to functionality since we are overwriting code that has already
	// executed and efi_enter_virtual_mode is only called once per boot.
	// 
	// Copy the printk call into kernel memory so it looks something like this (below):
	// This is patch_point_1 (overwrite of efi_enter_virtual_mode code).
	//
	// Message1:
	//   "\001\063<banner>"
	// New_Return_Addr:
	// push rax
	// mov  rdi, Message1
	// call printk
	// pop  rax
	// Orig_Return_Addr:
	// mov  rsi, rax
	// ...
	//
	destptr = eevm_retaddr - (sizeof(banner) + sizeof(printk_banner_template));

	// Copy the banner string
	for (i = 0; i < sizeof(banner); i++) {
		destptr[i] = banner[i];
	}
	// Copy the call to printk
	for (j = 0; j < sizeof(printk_banner_template); j++) {
		destptr[i + j] = printk_banner_template[j];
	}
	// Now fix up the correct addresses for the mov rdi and call print insns
	cp = destptr + (sizeof(banner) + 4); // mov rdi, Message1
	*(INT32*)cp = (INT32)(INT64)destptr;
	cp += (4 + 1); // offset call printk
	PUT_FIXUP(cp, printk); // call printk
	// Finally overwrite the stack return address to come back to our newly patched code
	rsp[retaddr_index] = (UINT64)(destptr + sizeof(banner));

	// Next look further up the stack for the sequence of call instructions in start_kernel
	start_kernel_retaddr = NULL;
	for (i = retaddr_index + 1; i < 0x40; i++) {
		if ((rsp[i] & 0xFFFFFFFF00000000L) == 0xFFFFFFFF00000000L) {
			cp = (UINT8*)rsp[i];
			found = 1;
			// Are the next 10 insns call instructions?
			for (j = 0; j < 10; j++) {
				if (*cp != 0xe8) {
					found = 0;
					break;
				}
				cp += 5;
			}
			if (found) {
				start_kernel_retaddr = (UINT8*)rsp[i];
				// Keep going until we hit the mfence after call to arch_call_rest_init()
				while (*cp == 0xe8) { // Skip over remaining call insns
					cp += 5;
				}
				if (*cp == 0x0f && *(cp + 1) == 0xae && *(cp + 2) == 0xf0) {
					cp -= 4; // backup to the call arch_call_rest_init() insn offset
					offset = *(INT32*)cp;
					arch_call_rest_init = (cp + 4) + offset;
					AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Mfence found: arch_call_rest_init = 0x%llx offset = 0x%x\n", arch_call_rest_init, offset);
					SerialOutString(StrBuffer);
				}
				else {
					found = 0;
					AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Mfence not found\n");
					SerialOutString(StrBuffer);
				}
				break;
			}
		}
	}
	if (found) {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Found start_kernel_retaddr @ 0x%llx, index = 0x%x\n", start_kernel_retaddr, i);
		SerialOutString(StrBuffer);
	}
	else {
		AsciiSPrint(StrBuffer, sizeof(StrBuffer), "Did NOT find start_kernel return addr\n");
		SerialOutString(StrBuffer);
	}

	if (arch_call_rest_init == NULL) {
		return;
	}
	// arch_call_rest_init() calls rest_init(), which creates the kernel_init and kernel_kthreadd
	// kernel threads. Once kthreadd is completed, the next call is to schedule_preempt_disabled(), which
	// begins launching waiting processes, one of which cleans up init space. Since we are patching into
	// init space, we need to create our own kernel thread after complete(&kthreadd) but before
	// schedule_preempt_disabled() is called.

	cp = arch_call_rest_init;
	// Look for:
	//   nop
	//   push rbp
	//   mov  rbp, rsp
	//   call rest_init
	//
	if (*cp != 0x0f || *(cp + 5) != 0x55 || *(cp + 6) != 0x48 || *(cp + 7) != 0x89 || *(cp + 8) != 0xe5 || *(cp + 9) != 0xe8) {
		return; // failure finding rest_init
	}
	cp += 10; // point to rest_init offset
	offset = *(INT32*)cp;
	rest_init = (cp + 4) + offset;
	AsciiSPrint(StrBuffer, sizeof(StrBuffer), "rest_init = 0x%llx\n", rest_init);
	SerialOutString(StrBuffer);

	// Initial code in rest_init() looks like this:
	//    0xffffffffa4ff7e13:  mov    rdi,0xffffffffa667e8e0           # &kthreadd_done
	//    0xffffffffa4ff7e1a:  mov    DWORD PTR[rip + 0x1563a20], 0x1  # system_state = SCHEDULING
	//	  0xffffffffa4ff7e24:  call   0xffffffffa4500b10               # complete(&kthreadd_done)
	//	  0xffffffffa4ff7e29 : call   0xffffffffa50013c0               # schedule_preempt_disabled() - goes multi-thread here 
	//	  0xffffffffa4ff7e2e : mov    edi, 0xe1                        # CPUHP_ONLINE
	//	  0xffffffffa4ff7e33 : call   0xffffffffa44e87e0               # cpu_startup_entry(CPUHP_ONLINE)   
	//	  0xffffffffa4ff7e38 : pop    rbp
	//    0xffffffffa4ff7e39:  ret
	// 
	// Patched code looks like this:
    //    0xffffffffa4ff7e13:  mov    rdi,0xffffffffa667e8e0           # &kthreadd_done
	//    0xffffffffa4ff7e1a:  mov    DWORD PTR[rip + 0x1563a20], 0x1  # system_state = SCHEDULING
	//	  0xffffffffa4ff7e24:  jmp    patch_point_2                    # patch_point_2 code below
	//	  0xffffffffa4ff7e29 : call   0xffffffffa50013c0               # schedule_preempt_disabled() - goes multi-thread here 
	//	  0xffffffffa4ff7e2e : mov    edi, 0xe1                        # CPUHP_ONLINE
	//	  0xffffffffa4ff7e33 : call   0xffffffffa44e87e0               # cpu_startup_entry(CPUHP_ONLINE)   
	//	  0xffffffffa4ff7e38 : pop    rbp
	//    0xffffffffa4ff7e39:  ret
	//
	//  Where patch_point_2 looks like this:
	//    patch_point_2:
	//             push rdi                               # save &kthreadd_done
	//             ...<allocate new UEFI kernel thread>
	//             pop  rdi                               # restore &kthreadd_done
	//             call 0xffffffffa4500b10                # complete(&kthreadd_done)
	//             jmp  0xffffffffa4ff7e29                # resume rest_init() code
	//             
	cp = rest_init + 0xa4; // TODO: Hard-coded offset here, we should search for the correct call insns at the end of rest_init
	if (*cp != 0xe8) {
		// Something not right, this should be a call insn
		return;
	}
	patch_point_2 = cp;
	cp++; // Point to offset
	offset = *(INT32*)cp;
	complete = (cp + 4) + offset;
	AsciiSPrint(StrBuffer, sizeof(StrBuffer), "complete = 0x%llx\n", complete);
	SerialOutString(StrBuffer);
	//
	// Setup code for patch_2. After it runs it will execute jmp back into the remaining rest_init() code.
	// We will overwrite start_kernel code immediately prior to start_kernel_retaddr. This won't change
	// functionality since all that code has already run, it only runs once per boot, and will be reclaimed
	// once the system goes multi-threaded.
	// 
	patch_2 = start_kernel_retaddr - sizeof(patch_code_2);
	//
	// First write the new code to patch_2
	//
	cp = patch_2;
	for (i = 0; i < sizeof(patch_code_2); i++) {
		*cp++ = patch_code_2[i];
	}

	cp = patch_2 + 3; // Fixup call to complete
	PUT_FIXUP(cp, complete);
	cp = patch_2 + 8; // Fixup the jump back into rest_init()
	PUT_FIXUP(cp, (patch_point_2 + 5));


	// Next, patch the rest_init() code with a jmp to patch_point_2.
	cp = patch_point_2;
	*cp = 0xe9; // direct near jmp to patch_2
	cp++; // point to offset
	PUT_FIXUP(cp, patch_2);

	//	CpuDeadLoop(); // Execution will hang here
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

