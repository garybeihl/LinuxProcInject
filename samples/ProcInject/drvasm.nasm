    DEFAULT REL
    SECTION .text

;------------------------------------------------------------------------------
; UINT64
; EFIAPI
; AsmGetRsp (
;   VOID
;   );
;------------------------------------------------------------------------------
global ASM_PFX(AsmGetRsp)
ASM_PFX(AsmGetRsp):
    mov     rax, rsp
    ret

;------------------------------------------------------------------------------
; UINT64
; EFIAPI
; AsmCallSysV1 (
;   UINT8 *funcname, arg1
;   );
;------------------------------------------------------------------------------
global ASM_PFX(AsmCallSysV1)
ASM_PFX(AsmCallSysV1):
    ; func addr in rcx
    ; arg1 in rdx -> rdi
    mov     rdi, rdx
    call    rcx
    ret