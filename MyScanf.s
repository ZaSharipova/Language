
section .bss
    buffer resb 16

section .text

my_scanf:
    push rdi
    push rsi
    push rdx

    xor rax, rax
    xor rdi, rdi
    mov rsi, buffer
    mov rdx, 16
    syscall

    mov rsi, buffer
    xor rax, rax
    xor rcx, rcx
    mov rbx, 1
    call pupu

    pop rdx
    pop rsi
    pop rdi
    ret

;------------------------
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

;----------------
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

;----------------
CheckSign:
    cmp dl, '-'
    jne .done
    mov rbx, -1
    inc rcx

.done:
    ret