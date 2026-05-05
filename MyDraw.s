default rel
section .text

my_draw:
        push rbp
        mov rbp, rsp
        push r12
        push r13
        push r14

        test rdi, rdi
        jz .end_err

        mov r12, rdi
        xor r13, r13

.loop:
        mov rax, r13
        shl rax, 3
        mov rcx, r12
        sub rcx, rax
        cmp qword [rcx], 0
        jne .print_plus

.print_dot:
        lea rsi, [dot_raw]
        jmp .do_write

.print_plus:
        lea rsi, [plus_raw]

.do_write:
        mov rax, 1
        mov rdi, 1
        mov rdx, 2
        syscall

.check_newline:
        mov rax, r13
        inc rax
        and rax, 31
        jnz .next_iter

        mov rax, 1
        mov rdi, 1
        lea rsi, [nl_raw]
        mov rdx, 1
        syscall

.next_iter:
        inc r13
        cmp r13, 1024
        jl .loop

        xor eax, eax

.end_err:
        pop r14
        pop r13
        pop r12
        mov rsp, rbp
        pop rbp
        ret

section .data
    dot_raw:  db ".."
    plus_raw: db "++"
    nl_raw:   db 10