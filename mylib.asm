BITS 64
default rel

; ============================================================
; Стековый фрейм my_printf / my_scanf:
;
;   rbp+0    = сохранённый rbp (push rbp)
;   rbp-8    = save_area[0]   (rbp)
;   rbp-16   = save_area[1]   (r12)
;   ...
;   rbp-104  = save_area[12]  (последний слот)
;   rbp-112  = xmm_save[0]
;   ...
;   rbp-168  = xmm_save[7]
;   rbp-176  = float_idx
;   rbp-192  = scan_buf[0..1] (16 байт)
;   rbp-224  = buf[0..3]      (32 байта, используем 28)
;
;   sub rsp, 224   — итоговое выделение
;
; Макросы обращаются к стеку через rbp — работают корректно
; вне зависимости от того, где физически лежит фрейм.
; ============================================================

%define FRAME_SIZE 224

%define SA(n) [rbp - 8 * (n + 1)]
%define XA(n) [rbp - 104 - 8 * (n + 1)]
%define FIDX [rbp - 176]
%define SCANBUF rbp - 192
%define BUF rbp - 224

%macro SAVE_REG 2
        mov SA(%2), %1
%endmacro

%macro REST_REG 2
        mov %1, SA(%2)
%endmacro

%macro SAVE_XMM 2
    movsd XA(%2), %1
%endmacro

%macro REST_XMM 2
    movsd %1, XA(%2)
%endmacro

section .text

;------------------------------------------------------------------------------
; my_printf - for x86-64 Linux
;
; Supported format specifiers:
;   %b  binary
;   %c  character
;   %d  decimal
;   %f  float
;   %g  g
;   %o  octal
;   %p  pointer
;   %s  string
;   %x  hex
;   %%  literal %
;
; Entry:    RDI = format string
;           RSI = arg1  RDX = arg2  RCX = arg3  R8 = arg4  R9 = arg5
;           [rbp + 16] = arg6  [rbp + 24] = arg7  ...
; Exit:     output written to stdout
; Destr:    RAX, RCX, RDX, RSI, RDI, R10, R11
;
; Internal register map:
;   R11 = arg3  R12 = arg2  R13 = arg1  R14 = arg index  R15 = buf pos
;------------------------------------------------------------------------------
my_printf:
        push rbp
        mov rbp, rsp
        sub rsp, FRAME_SIZE
        and rsp, -16            ; align to 16

        SAVE_REG rbp, 0
        SAVE_REG r12, 1
        SAVE_REG r13, 2
        SAVE_REG r14, 3
        SAVE_REG r15, 4
        SAVE_REG rbx, 5

        mov rbx, [rbp + 8]
        SAVE_REG rbx, 6         ; saving ret address

        SAVE_REG rdi, 9
        SAVE_REG rsi, 10
        SAVE_REG rdx, 11
        SAVE_REG rcx, 12
        SAVE_REG r8, 7
        SAVE_REG r9, 8

        SAVE_XMM xmm0, 0
        SAVE_XMM xmm1, 1
        SAVE_XMM xmm2, 2
        SAVE_XMM xmm3, 3
        SAVE_XMM xmm4, 4
        SAVE_XMM xmm5, 5
        SAVE_XMM xmm6, 6
        SAVE_XMM xmm7, 7

        call my_printf_impl

        REST_REG rdi, 9
        REST_REG rsi, 10
        REST_REG rdx, 11
        REST_REG rcx, 12
        REST_REG r8, 7
        REST_REG r9, 8

        REST_XMM xmm0, 0
        REST_XMM xmm1, 1
        REST_XMM xmm2, 2
        REST_XMM xmm3, 3
        REST_XMM xmm4, 4
        REST_XMM xmm5, 5
        REST_XMM xmm6, 6
        REST_XMM xmm7, 7

        REST_REG r12, 1
        REST_REG r13, 2
        REST_REG r14, 3
        REST_REG r15, 4
        REST_REG rbx, 5
        REST_REG rbp, 0

        leave
        ret

my_printf_impl:
        mov r13, SA(10)
        mov r12, SA(11)
        mov r11, SA(12)

        xor r15, r15
        xor r14, r14
        mov qword FIDX, 0

        mov rsi, SA(9)

.main_loop:
        lodsb
        test al, al
        je .flush_buffer

        cmp al, '%'
        je .handle_specifier

        call BufferChar
        jmp .main_loop

.handle_specifier:
        push rsi
        call FlushIfFull
        pop rsi

        lodsb
        test al, al
        je .flush_buffer

        cmp al, '%'
        je .store_percent

        push rsi
        call ProcessFormat
        pop rsi
        jmp .main_loop

.store_percent:
        mov al, '%'
        call BufferChar
        jmp .main_loop

.flush_buffer:
        test r15, r15
        jz .my_printf_end

        lea rdi, [BUF]
        mov rsi, r15
        call FlushBuffer
        xor r15, r15

.my_printf_end:
        ret

;------------------------------------------------------------------------------
; ProcessFormat - Dispatch format specifier via jump table
;
; Entry:    AL = format character (b/c/d/f/g/o/p/s/x)
; Exit:     appropriate handler called
; Destr:    -
;------------------------------------------------------------------------------
ProcessFormat:
        movzx rax, al

        cmp rax, 'b'
        jb .skip
        cmp rax, 'x'
        ja .skip

        lea rbx, [rel jump_table]
        mov rax, [rbx + (rax - 'b') * 8]
        jmp rax

.skip:
        push rax
        mov al, '%'
        call BufferChar
        pop rax
        call BufferChar
        ret

;------------------------------------------------------------------------------
; BufferChar - Append one character to buf[], flushing first if full
;
; Entry:    AL  = character to store
;           R15 = current buffer write position
; Exit:     R15 incr
; Destr:    -
;------------------------------------------------------------------------------
BufferChar:
        cmp r15, 27
        jb .store

        push rax
        push rsi
        push rdi

        lea rdi, [BUF]
        mov rsi, r15
        call FlushBuffer
        xor r15, r15

        pop rdi
        pop rsi
        pop rax

.store:
        lea rdi, [BUF]
        add rdi, r15
        mov [rdi], al
        inc r15
        ret

;------------------------------------------------------------------------------
; FlushIfFull - Flush buffer if write position >= 21
;
; Entry:    R15 = current buffer write position
; Exit:     R15 = 0 if flushed, unchanged otherwise
; Destr:    -
;------------------------------------------------------------------------------
FlushIfFull:
        cmp r15, 21
        jb .skip

        push rsi
        push rdi

        lea rdi, [BUF]
        mov rsi, r15
        call FlushBuffer
        xor r15, r15

        pop rdi
        pop rsi

.skip:
        ret

;------------------------------------------------------------------------------
; FlushBuffer - Flush buf[] to stdout via write(2) syscall
;
; Entry:    R15 = number of bytes to write
; Exit:     R15 = 0
; Destr:    RDX, RSI, RAX, RDI
;------------------------------------------------------------------------------
FlushBuffer:
        mov rdx, rsi
        mov rsi, rdi
        mov rax, 1
        mov rdi, 1
        syscall
        ret

;------------------------------------------------------------------------------
; PrintBinary - Print next argument as binary integer
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, digits written to buffer
; Destr:    RAX, RBX
;------------------------------------------------------------------------------
PrintBinary:
        call NextArg
        mov cl, 1
        mov rbx, 0x1
        call PrintIntBase
        ret

;------------------------------------------------------------------------------
; PrintChar - Print next argument as a single character
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, one byte written to buffer
; Destr:    RAX
;------------------------------------------------------------------------------
PrintChar:
        call NextArg
        call BufferChar
        ret

;------------------------------------------------------------------------------
; PrintDecimal - Print next argument as signed decimal integer
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, digits written to buffer
; Destr:    RAX
;------------------------------------------------------------------------------
PrintDecimal:
        call NextArg
        call PrintInt10
        ret

;------------------------------------------------------------------------------
; PrintOctal - Print next argument as octal integer
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, digits written to buffer
; Destr:    RAX, RBX
;------------------------------------------------------------------------------
PrintOctal:
        call NextArg
        mov cl, 3
        mov rbx, 0x07
        call PrintIntBase
        ret

;------------------------------------------------------------------------------
; PrintHex - Print next argument as hexadecimal integer
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, digits written to buffer
; Destr:    RAX, RBX
;------------------------------------------------------------------------------
PrintHex:
        call NextArg
        mov cl, 4
        mov rbx, 0x0F
        call PrintIntBase
        ret
;------------------------------------------------------------------------------
; PrintFloat - Print next argument as float
;
; Entry:    float_idx = current float argument index
; Exit:     float_idx incremented, digits written to buffer
; Destr:    RAX, RBX
;------------------------------------------------------------------------------
PrintFloat:
        call NextFloatArg
        call PrintDouble
        ret

;------------------------------------------------------------------------------
; PrintPointer - Print next argument as "0x<hex>"
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, "0x" + hex digits written to buffer
; Destr:    RAX, RBX
;------------------------------------------------------------------------------
PrintPointer:
        push rsi
        call NextArg
        test rax, rax
        jnz .not_null

        lea rsi, [rel nil_str]
        mov rcx, 5

.null_loop:
        lodsb
        call BufferChar
        loop .null_loop
        pop rsi
        ret

.not_null:
        push rax
        mov al, '0'
        call BufferChar
        mov al, 'x'
        call BufferChar
        pop rax
        mov cl, 4
        mov rbx, 0x0F
        call PrintIntBase
        pop rsi
        ret

;------------------------------------------------------------------------------
; PrintString - Print next argument as null-terminated string
;
; Entry:    R14 = current argument index
; Exit:     R14 incremented, string bytes written to buffer
; Destr:    RAX, RBX, RCX, RDI, R10
;------------------------------------------------------------------------------
PrintString:
        push rsi
        call NextArg
        test rax, rax
        jnz .not_null

        lea rsi, [rel null_str]
        mov rcx, 6
.null_loop:
        lodsb
        call BufferChar
        loop .null_loop
        pop rsi
        ret

.not_null:
        mov r10, rax
        push rdi
        mov rdi, r10
        call CountStrLen
        pop rdi
        mov rbx, rax

        cmp rbx, 27
        jbe .check_buf

        test r15, r15
        jz .print_direct
        lea rdi, [BUF]
        mov rsi, r15
        call FlushBuffer
        xor r15, r15

.print_direct:
        mov rdi, r10
        mov rsi, rbx
        call FlushBuffer
        pop rsi
        ret

.check_buf:
        mov rdx, r15
        add rdx, rbx
        cmp rdx, 27
        jbe .copy

        push r10
        lea rdi, [BUF]
        mov rsi, r15
        call FlushBuffer
        xor r15, r15
        pop r10

.copy:
        lea rdi, [BUF]
        add rdi, r15
        mov rsi, r10
        mov rcx, rbx
        rep movsb
        add r15, rbx
        pop rsi
        ret

;------------------------------------------------------------------------------
; PrintDecimal - Print signed decimal integer
;
; Entry:    RAX = integer (sign-extended 32->64 before call)
; Exit:     digits written to buffer
; Destr:    RAX, RBX, RCX, RDX
;------------------------------------------------------------------------------
PrintInt10:
        push rbx
        push rcx
        push rdx
        xor rcx, rcx
        movsxd rax, eax

        test rax, rax
        jz .zero
        jns .unsigned

.negative:
        push rax
        mov al, '-'
        call BufferChar
        pop rax
        neg rax

.unsigned:
        mov rbx, 10
        xor rcx, rcx

.loop:
        xor rdx, rdx
        div rbx
        add dl, '0'
        push rdx
        inc rcx
        test rax, rax
        jnz .loop

.print:
        pop rdx
        mov al, dl
        call BufferChar
        dec rcx
        jnz .print
        jmp .done

.zero:
        mov al, '0'
        call BufferChar

.done:
        pop rdx
        pop rcx
        pop rbx
        ret

;------------------------------------------------------------------------------
; PrintIntBase - Print unsigned integer in base 2 / 8 / 16
;
; Entry:    RAX = integer (treated as unsigned 32-bit: zero-extend)
;           RBX = mask (0x01, 0x07, 0x0F)
;           RCX = shift (1 for 2, 3 for 8, 4 for 16)
; Exit:     digits written to buffer
; Destr:    RAX, RDX, R9
;------------------------------------------------------------------------------
PrintIntBase:
        push rdx
        push r9

        test rax, rax
        jz .zero

        xor r9, r9

.loop:
        mov rdx, rax
        and rdx, rbx
        shr rax, cl
        add dl, '0'
        cmp dl, '9'
        jbe .store
        add dl, 'a' - '0' - 10

.store:
        push rdx
        inc r9
        test rax, rax
        jnz .loop

.print:
        pop rdx
        mov al, dl
        call BufferChar
        dec r9
        jnz .print
        jmp .done

.zero:
        mov al, '0'
        call BufferChar

.done:
        pop r9
        pop rdx
        ret

;------------------------------------------------------------------------------
; CountStrLen - Return length of null-terminated string
;
; Entry:    RDI = pointer to string
; Exit:     RAX = length (excluding null terminator)
; Destr:    -
;------------------------------------------------------------------------------
CountStrLen:
        test rdi, rdi
        jz .null

        push rsi
        mov rsi, rdi
.loop:
        lodsb
        test al, al
        jnz .loop
        lea rax, [rsi - 1]
        sub rax, rdi
        pop rsi
        ret

.null:
        xor rax, rax
        ret

;------------------------------------------------------------------------------
; PrintDouble - Print double with 6 digits after decimal point (%f)
;
; Entry:    XMM0 = double value
; Exit:     characters written via BufferChar
; Destr:    RAX, RBX, RCX, RDX, RSI, RDI, XMM0, XMM8
;------------------------------------------------------------------------------
PrintDouble:
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        sub rsp, 16

        call CheckSpecial
        jz .end

        call PrintSign          ; xmm0 = |x|
        movsd [rsp], xmm0       ; save |x|

        ; integer part
        cvttsd2si rax, xmm0
        call PrintInt10

        mov al, '.'
        call BufferChar

        ; fractional part
        movsd xmm0, [rsp]
        cvttsd2si rax, xmm0
        cvtsi2sd xmm8, rax      ; xmm8 = float(integer part)
        subsd xmm0, xmm8        ; xmm0 = fractional part
        call PrintFrac6

.end:
        add rsp, 16
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

;------------------------------------------------------------------------------
; PrintG - Print double in %g format
;
; RULE:     if -4 <= e < 6 — print as %f without trailing zeroes
;           else            — print as %e
;
; Entry:    XMM0 = double value
; Exit:     characters written via BufferChar
; Destr:    RAX, RBX, RCX, RDX, XMM0, XMM8
;------------------------------------------------------------------------------
PrintG:
        sub rsp, 16

        call CheckSpecial
        jz .end

        call PrintSign          ; xmm0 = |x|
        movsd [rsp], xmm0       ; save |x|

        ; e = floor(log10(|x|))
        fldlg2                  ; st0 = log10(2)
        fld qword [rsp]         ; st0 = |x|
        fyl2x                   ; st0 = log10(|x|)
        fistp dword [rsp + 8]   ; e = (int)floor(log10(|x|))
        mov ebx, [rsp + 8]

        cmp ebx, -4
        jl .use_exp
        cmp ebx, 6
        jge .use_exp

        movsd xmm0, [rsp]
        cvttsd2si rax, xmm0
        call PrintInt10

        mov al, '.'
        call BufferChar

        movsd xmm0, [rsp]
        cvttsd2si rax, xmm0
        cvtsi2sd xmm8, rax
        subsd xmm0, xmm8
        call PrintFrac6Strip
        jmp .end

.use_exp:
        movsd xmm0, [rsp]
        call PrintExp

.end:
        add rsp, 16
        ret

;------------------------------------------------------------------------------
; PrintSign - Print '-' if xmm0 is negative, make xmm0 positive
;
; Entry:    XMM0 = double value
; Exit:     XMM0 = |value|, '-' written to buffer if negative
; Destr:    RAX
;------------------------------------------------------------------------------
PrintSign:
        movq rax, xmm0
        test rax, rax
        jns .pos
        mov al, '-'
        call BufferChar
        andpd xmm0, [rel abs_mask]
.pos:
        ret

;------------------------------------------------------------------------------
; CheckSpecial - Check if xmm0 is NaN or Inf, print if so
;
; Entry:    XMM0 = double value
; Exit:     ZF=1 if printed (nan/inf), ZF=0 if normal number
;           XMM0 unchanged
; Destr:    RAX, RCX, RDX, RSI
;------------------------------------------------------------------------------
CheckSpecial:
        movq rax, xmm0
        mov rcx, rax
        shr rcx, 52
        and rcx, 0x7FF
        cmp rcx, 0x7FF
        jne .normal

        mov rdx, 0x000FFFFFFFFFFFFF
        and rax, rdx
        jz .is_inf

        call PrintSign
        lea rsi, [rel nan_str]
        mov rdx, 3
        call BufferString
        xor rax, rax
        ret

.is_inf:
        call PrintSign
        lea rsi, [rel inf_str]
        mov rdx, 3
        call BufferString
        xor rax, rax
        ret

.normal:
        or rax, 1
        ret

;------------------------------------------------------------------------------
; PrintFrac6 - Print 6 fractional digits from xmm0
;
; Entry:    XMM0 = fractional part (0 <= x < 1)
; Exit:     6 digits written to buffer
; Destr:    RAX, RBX, RCX, RDX, XMM8
;------------------------------------------------------------------------------
PrintFrac6:
        push rbx
        push rcx

        mov rax, 1000000
        cvtsi2sd xmm8, rax
        mulsd xmm8, xmm0
        addsd xmm8, [rel round_eps]
        cvttsd2si rax, xmm8

        mov rbx, 10
        mov rcx, 6

.loop:
        xor rdx, rdx
        div rbx
        add dl, '0'
        push rdx
        loop .loop

        mov rcx, 6
.print:
        pop rdx
        mov al, dl
        call BufferChar
        loop .print

        pop rcx
        pop rbx
        ret

;------------------------------------------------------------------------------
; BufferString - Write string to buffer byte by byte
;
; Entry:    RSI = pointer to string
;           RDX = length
; Exit:     characters written via BufferChar
; Destr:    RAX
;------------------------------------------------------------------------------
BufferString:
        push rcx
        mov rcx, rdx
.loop:
        lodsb
        call BufferChar
        loop .loop
        pop rcx
        ret

;------------------------------------------------------------------------------
; PrintFrac6Strip - same as PrintFrac6 but strips trailing zeroes
;
; Entry:    XMM0 = fractional part (0 <= x < 1)
; Exit:     digits without trailing zeroes written to buffer
; Destr:    RAX, RBX, RCX, RDX, R8, R9, XMM8
;------------------------------------------------------------------------------
PrintFrac6Strip:
        push rbx
        push rcx
        push r8
        push r9

        mov rax, 1000000
        cvtsi2sd xmm8, rax
        mulsd xmm8, xmm0
        addsd xmm8, [rel round_eps]
        cvttsd2si rax, xmm8

        mov rbx, 10
        mov r8, 6

.strip:
        cmp r8, 1
        je .done_strip
        xor rdx, rdx
        div rbx
        test rdx, rdx
        jnz .done_strip_back
        dec r8
        jmp .strip

.done_strip_back:
        imul rax, rbx
        add rax, rdx

.done_strip:
        mov rbx, 10
        mov rcx, r8

.loop:
        xor rdx, rdx
        div rbx
        add dl, '0'
        push rdx
        loop .loop

        mov rcx, r8
.print:
        pop rdx
        mov al, dl
        call BufferChar
        loop .print

        pop r9
        pop r8
        pop rcx
        pop rbx
        ret

;--------------------------------------------------------------------------------------
; ScaleMantissa - Scales the number in XMM0 to a mantissa in the range 1.0 <= x < 10.0
;
; Entry:    XMM0 = number
;           RBX  = exponent e (floor(log10(x)))
; Exit:     XMM0 = x / 10^e, in [1.0, 10.0)
; Destr:    RAX, RCX, XMM8
;--------------------------------------------------------------------------------------
ScaleMantissa:
        push rcx
        mov rax, 10
        cvtsi2sd xmm8, rax

        test rbx, rbx
        jz .done
        jns .down

.up:
        mov rcx, rbx
        neg rcx
.up_loop:
        mulsd xmm0, xmm8
        dec rcx
        jnz .up_loop
        jmp .done

.down:
        mov rcx, rbx
.down_loop:
        divsd xmm0, xmm8
        dec rcx
        jnz .down_loop

.done:
        pop rcx
        ret

;------------------------------------------------------------------------------
; PrintExp - Prints the number in XMM0 in exponential form: 1.234e+08
;
; Entry:    XMM0 >= 0 (sign already printed)
; Exit:     characters written via BufferChar
; Destr:    RAX, RBX, RCX, RDX, XMM0, XMM8
;------------------------------------------------------------------------------
PrintExp:
        sub rsp, 16
        movsd [rsp], xmm0

        fldlg2
        fld qword [rsp]
        fyl2x
        fistp dword [rsp + 8]
        movsxd rbx, dword [rsp + 8]

        movsd xmm0, [rsp]
        call ScaleMantissa
        movsd [rsp], xmm0

        cvttsd2si rax, xmm0
        add al, '0'
        call BufferChar

        mov al, '.'
        call BufferChar

        movsd xmm0, [rsp]
        cvttsd2si rax, xmm0
        cvtsi2sd xmm8, rax
        subsd xmm0, xmm8
        call PrintFrac6Strip

        call PrintExpSign

        add rsp, 16
        ret

;------------------------------------------------------------------------------
; PrintExpSign - Prints the exponent sign in the format 'e+XX' or 'e-XX'
;
; Entry:    RBX = exponent (can be negative)
; Exit:     The buffer is filled with exponent characters via BufferChar
; Destr:    RAX, RCX, RDX
;------------------------------------------------------------------------------
PrintExpSign:
        mov al, 'e'
        call BufferChar

        test rbx, rbx
        jns .pos
        mov al, '-'
        call BufferChar
        neg rbx
        jmp .digits

.pos:
        mov al, '+'
        call BufferChar

.digits:
        mov rax, rbx
        cmp rax, 10
        jge .no_zero
        mov al, '0'
        call BufferChar

.no_zero:
        mov rax, rbx
        xor rcx, rcx
        mov rbx, 10

.loop:
        xor rdx, rdx
        div rbx
        add dl, '0'
        push rdx
        inc rcx
        test rax, rax
        jnz .loop

.print:
        pop rdx
        mov al, dl
        call BufferChar
        dec rcx
        jnz .print
        ret

;------------------------------------------------------------------------------
; NextArg - Return next variadic argument in RAX
;
; Entry:    R14 = current argument index (0-based)
; Exit:     RAX = argument value
;           R14 incremented
; Destr:    RBX
;------------------------------------------------------------------------------
NextArg:
        mov rbx, r14
        inc r14

        cmp rbx, 0
        jl .error

        cmp rbx, 5
        jae .stack

        lea rax, [rel nextarg_table]
        mov rax, [rax + rbx * 8]
        jmp rax

.error:
        xor rax, rax
        ret

.stack:
        mov rax, SA(0)
        mov rax, [rax + 16 + (rbx - 5) * 8]
        ret

.a1:    mov rax, r13
        ret

.a2:    mov rax, r12
        ret

.a3:    mov rax, r11
        ret

.a4:    mov rax, SA(7)
        ret

.a5:    mov rax, SA(8)
        ret

;------------------------------------------------------------------------------
; NextFloatArg - Return next variadic argument in XMM0
;
; Entry:    float_idx = current argument index (0-based)
; Exit:     XMM0 = argument value
;           float_idx incremented
; Destr:    -
;------------------------------------------------------------------------------
NextFloatArg:
        push rbx
        mov rbx, FIDX
        inc qword FIDX

        cmp rbx, 8
        jae .stack

        lea rax, [rel xmm_nextarg_table]
        mov rax, [rax + rbx * 8]
        pop rbx
        jmp rax

.stack:
        sub rbx, 8
        mov rax, SA(0)          ; orig_rbp
        movsd xmm0, [rax + 16 + rbx * 8]
        pop rbx
        ret

.x1:    movsd xmm0, XA(0)
        ret
.x2:    movsd xmm0, XA(1)
        ret
.x3:    movsd xmm0, XA(2)
        ret
.x4:    movsd xmm0, XA(3)
        ret
.x5:    movsd xmm0, XA(4)
        ret
.x6:    movsd xmm0, XA(5)
        ret
.x7:    movsd xmm0, XA(6)
        ret
.x8:    movsd xmm0, XA(7)
        ret

;-------------------------------------------
;-------------------------------------------
;-------------------------------------------
my_scanf:
        push rbp
        mov rbp, rsp
        sub rsp, 16             ; scan_buf on stack: [rbp - 16]..[rbp - 1]
        and rsp, -16

        push rdi
        push rsi
        push rdx
        push rcx
        push rbx

        xor rax, rax            ; sys_read
        xor rdi, rdi            ; stdin
        lea rsi, [rbp - 16]     ; &scan_buf
        mov rdx, 15
        syscall

        mov byte [rbp - 1], 0

        lea rsi, [rbp - 16]
        xor rax, rax
        xor rcx, rcx
        mov rbx, 1
        call pupu

        pop rbx
        pop rcx
        pop rdx
        pop rsi
        pop rdi

        leave
        ret

pupu:
        call SkipSpaces
        movzx rdx, byte [rsi + rcx]
        call CheckSign

.loop:
        movzx rdx, byte [rsi + rcx]
        sub dl, '0'
        cmp dl, 9
        ja .done

        imul rax, rax, 10
        add rax, rdx
        inc rcx
        jmp .loop

.done:
        imul rax, rbx
        ret

SkipSpaces:
.loop:
        movzx rdx, byte [rsi + rcx]
        cmp dl, ' '
        je .skip
        cmp dl, 9
        je .skip
        cmp dl, 10
        je .skip
        cmp dl, 13
        je .skip
        jmp .done
.skip:
        inc rcx
        jmp .loop
.done:
        ret

CheckSign:
        cmp dl, '-'
        jne .done
        mov rbx, -1
        inc rcx
.done:
        ret

;-------------------------------------------
;-------------------------------------------
;-------------------------------------------
my_exit:
        mov rax, 60
        mov rdi, 0
        syscall


        align 8

jump_table:
                        dq PrintBinary
                        dq PrintChar
                        dq PrintDecimal
                        dq ProcessFormat.skip
                        dq PrintFloat
                        dq PrintG
times ('o' - 'g' - 1)   dq ProcessFormat.skip
                        dq PrintOctal
                        dq PrintPointer
times ('s' - 'p' - 1)   dq ProcessFormat.skip
                        dq PrintString
times ('x' - 's' - 1)   dq ProcessFormat.skip
                        dq PrintHex

nextarg_table:
        dq NextArg.a1
        dq NextArg.a2
        dq NextArg.a3
        dq NextArg.a4
        dq NextArg.a5

xmm_nextarg_table:
        dq NextFloatArg.x1
        dq NextFloatArg.x2
        dq NextFloatArg.x3
        dq NextFloatArg.x4
        dq NextFloatArg.x5
        dq NextFloatArg.x6
        dq NextFloatArg.x7
        dq NextFloatArg.x8

null_str:  db "(null)"
nil_str:   db "(nil)"
nan_str:   db "nan"
inf_str:   db "inf"

        align 16
abs_mask:  dq 0x7FFFFFFFFFFFFFFF
           dq 0
round_eps: dq 0.0000005