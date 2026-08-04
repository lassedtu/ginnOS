bits 16

section .text

;
; void x86_div64_32(uint64_t dividend, uint32_t divisor, uint64_t* quotientOut, uint32_t* remainderOut);
;
global x86_div64_32
x86_div64_32:

    ; make new call frame
    push ebp            ; save old call frame
    mov ebp, esp        ; initialize new call frame

    push ebx

    ; i386-style stack slots:
    ; [ebp + 8]  = dividend low 32
    ; [ebp + 12] = dividend high 32
    ; [ebp + 16] = divisor
    ; [ebp + 20] = quotientOut
    ; [ebp + 24] = remainderOut

    ; divide upper 32 bits
    mov eax, [ebp + 12] ; eax <- upper 32 bits of dividend
    mov ecx, [ebp + 16] ; ecx <- divisor
    xor edx, edx
    div ecx             ; eax - quot, edx - remainder

    ; store upper 32 bits of quotient
    mov ebx, [ebp + 20]
    mov [ebx + 4], eax

    ; divide lower 32 bits
    mov eax, [ebp + 8]  ; eax <- lower 32 bits of dividend
                        ; edx <- old remainder
    div ecx

    ; store results
    mov [ebx], eax
    mov ebx, [ebp + 24]
    mov [ebx], edx

    pop ebx

    ; restore old call frame
    mov esp, ebp
    pop ebp
    ret

;
; int 10h ah=0Eh
; args: character, page
;
global x86_Video_WriteCharTeletype
x86_Video_WriteCharTeletype:
    
    ; make new call frame
    push ebp            ; save old call frame
    mov ebp, esp        ; initialize new call frame

    ; save bx
    push ebx

    ; [ebp + 8]  - first argument (character)
    ; [ebp + 12] - second argument (page)
    mov ah, 0Eh
    mov al, [ebp + 8]
    mov bh, [ebp + 12]

    int 10h

    ; restore bx
    pop ebx

    ; restore old call frame
    mov esp, ebp
    pop ebp
    ret