#ifndef DSL_H
#define DSL_H

#define BYTE(byte)                  Emit8(CODE,  (uint8_t)(byte))
#define DWORD(double_word)          Emit32(CODE, (uint32_t)(double_word))
#define QWORD(quad_word)            Emit64(CODE, (uint64_t)(quad_word))

#define NOP()                       BYTE(0x90)
#define REX_W(reg, rm)              Emit8(CODE, RexW((reg), (rm)))
#define MODRM(mod, reg, rm)         Emit8(CODE, ModRM((mod), (reg), (rm)))

#define PUSH(reg)                   EmitPush(context, (reg))
#define POP(reg)                    EmitPop (context, (reg))

#define MOV_RR(dst, src)            EmitMovRR(context, (dst), (src))
#define MOV_R_IMM64(reg, imm)       EmitMovR64Imm64(context, (reg), (int64_t)(imm))

// mov reg32, imm32
#define MOV_R_IMM32(reg, imm)                          \
    do {                                               \
        REX_W(0, (reg));                               \
        BYTE(0xC7);                                    \
        MODRM(3, 0, (reg));                            \
        DWORD((uint32_t)(int32_t)(imm));               \
    } while (0)

// mov qword [reg], imm32
#define MOV_MEM_IMM32(reg, imm)                        \
    do {                                               \
        REX_W(0, (reg));                               \
        BYTE(0xC7);                                    \
        MODRM(0, 0, reg);                              \
        DWORD((uint32_t)(int32_t)(imm));               \
    } while (0)

// mov [dst_reg], src_reg (REX.W 89 /r)
#define MOV_MEM_R(dst_addr_reg, src_reg)               \
    do {                                               \
        REX_W(src_reg, dst_addr_reg);                  \
        BYTE(0x89);                                    \
        MODRM(0, src_reg, dst_addr_reg);               \
    } while (0)

// push qword [reg]
#define PUSH_MEM(reg)                                  \
    do {                                               \
        BYTE(0xFF);                                    \
        MODRM(0, 6, reg);                              \
    } while (0)

// add dst, src
#define ADD_RR(dst, src)                               \
    do {                                               \
        REX_W(src, dst);                               \
        BYTE(0x01);                                    \
        MODRM(3, src, dst);                            \
    } while (0)

// sub dst, src
#define SUB_RR(dst, src)                               \
    do {                                               \
        REX_W(src, dst);                               \
        BYTE(0x29);                                    \
        MODRM(3, src, dst);                            \
    } while (0)

// imul dst, src
#define IMUL_RR(dst, src)                              \
    do {                                               \
        REX_W(dst, src);                               \
        BYTE(0x0F); BYTE(0xAF);                        \
        MODRM(3, dst, src);                            \
    } while (0)

/* cqo; idiv src (rax := rdx:rax / src) */
#define IDIV_R(src)                                    \
    do {                                               \
        REX_W(0, 0); BYTE(0x99);          /* cqo */    \
        REX_W(0, src);                                 \
        BYTE(0xF7);                                    \
        MODRM(3, 7, src);                              \
    } while (0)

#define ADD_R_IMM(reg, imm)  EmitAddRegImm(context, reg, (int64_t)(imm))

// shl reg, imm8
#define SHL_R_IMM8(reg, imm)                           \
    do {                                               \
        REX_W(0, reg);                                 \
        BYTE(0xC1);                                    \
        MODRM(3, 4, reg);                              \
        BYTE((uint8_t)(imm));                          \
    } while (0)

// jmp qword [rip + rel32]  — для PLT-стабов
#define JMP_RIP_REL32()                                \
    do {                                               \
        BYTE(0xFF);                                    \
        MODRM(0, 4, 5);                                \
    } while (0)

// xor eax, eax
#define XOR_EAX()                                      \
    do {                                               \
        BYTE(0x31);                                    \
        MODRM(3, kRAX, kRAX);                          \
    } while (0)

#define CMP_RR(dst, src)                               \
    do {                                               \
        REX_W((src), (dst));                           \
        BYTE(0x39);                                    \
        MODRM(3, (src), (dst));                        \
    } while (0)

#define CMP_RAX_RBX()      CMP_RR(kRAX, kRBX)

#define CALL(name)          EmitCall(context, name)
#define JMP(name)           EmitJmp(context, name)
#define JCC(cc, name)       EmitJCC(context, (uint8_t)(cc), name)
#define RET()               EmitRet(context)

#define LEA_RCX_RBP(disp)   EmitLeaRcxRbp(context, (int32_t)(disp))
#define VAR_ADDR(slot, pc)  EmitVarAddrBySlot(context, slot, pc)
#define MOV_DATA(reg, symbol) EmitMovData(context, reg, symbol);

#define MOV_DATA(reg, sym)  EmitMovData(context, reg, sym)

#define ALIGN_STACK()       EmitAlignStack(context)
#define PROLOGUE(frame)     EmitPrologue(context, frame)
#define EPILOGUE()          EmitEpilogue(context)

#define SAVE_RSP_R13()      EmitSaveRspToR13(context)
#define RESTORE_RSP_R13()   EmitRestoreRspFromR13(context)

#define CVTSI2SD_XMM0_R(reg)                                         \
    do {                                                             \
        BYTE(0xF2); REX_W(0, (reg));                                 \
        BYTE(0x0F); BYTE(0x2A); MODRM(3, 0, (reg));                  \
    } while (0)

#define SQRTSD_XMM_XMM(dst, src)                                     \
    do {                                                             \
        BYTE(0xF2); BYTE(0x0F); BYTE(0x51);                          \
        MODRM(3, (dst), (src));                                      \
    } while (0)

#define CVTTSD2SI_R_XMM0(reg)                                        \
    do {                                                             \
        BYTE(0xF2); REX_W((reg), 0);                                 \
        BYTE(0x0F); BYTE(0x2C); MODRM(3, (reg), 0);                  \
    } while (0)

#endif // DSL_H