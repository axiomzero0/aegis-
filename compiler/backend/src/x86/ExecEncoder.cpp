// backend/x86/ExecEncoder.cpp — MachineFunction -> executable x86-64.
//
// The encoding table below follows Intel SDM Vol. 2. Every literal is
// a named constant (Rules D.1/D.2); the sequences are the standard
// two-operand lowering of our three-address MachineInstrs:
//
//   dst = a OP b   ->  mov dst, a ; OP dst, b     (when dst != a)
//   dst = a << k   ->  mov dst, a ; shl dst, k
//   dst = a / b    ->  mov rax, a ; cqo ; idiv b ; mov dst, rax
//   dst = (a < b)  ->  cmp a, b ; setl al ; movzx dst, al
//
// RAX/RDX are scratch-only (never homes) so the div sequences and the
// setcc/movzx pair can never clobber a live vreg (see header).
#include "aegis/backend/x86/ExecEncoder.hpp"

#include <algorithm>

#include "aegis/backend/ElfConstants.hpp"
#include "aegis/backend/x86/Target_x86.hpp"

namespace aegis::backend::x86 {

namespace {

// ---- Named encoding constants (Intel SDM Vol. 2; Rule D.1/D.2). ----

/// REX prefix with the W bit (64-bit operand size).
constexpr uint8_t kRexW{0x48};
/// REX R bit: extends the modrm REG field to registers 8-15.
constexpr uint8_t kRexR{0x44};
/// REX.B prefix (B bit set): extends the modrm R/M field, or the
/// opcode-embedded register of push/pop, to registers 8-15. NOTE:
/// this is the FULL PREFIX byte 0x41, not the raw bit — push/pop emit
/// it standalone (0x41 0x54 = push r12), and OR-ing the full prefixes
/// still yields correct combinations because the REX low nibble is
/// bit-OR idempotent (0x48|0x44|0x41 = 0x4D).
constexpr uint8_t kRexB{0x41};
/// Modrm byte for register-to-register form (mod=11).
constexpr uint8_t kModrmRegReg{0xC0};
/// Shift of the REG field inside a modrm byte.
constexpr uint32_t kModrmRegShift{3};
/// Mask of the low (non-REX) 3-bit register index.
constexpr uint8_t kRegIdxMask{0x07};

// One- and two-byte opcodes.
constexpr uint8_t kOpRet{0xC3};          // ret
constexpr uint8_t kOpPush{0x50};         // 50+rd : push r64
constexpr uint8_t kOpPop{0x58};          // 58+rd : pop r64
constexpr uint8_t kOpMovRmR{0x89};       // 89 /r : mov r/m64, r64
constexpr uint8_t kOpCmpRmR{0x3B};       // 3B /r : cmp r64, r/m64
constexpr uint8_t kOpAddRR{0x01};        // 01 /r : add r/m64, r64
constexpr uint8_t kOpOrRR{0x09};         // 09 /r
constexpr uint8_t kOpAndRR{0x21};        // 21 /r
constexpr uint8_t kOpSubRR{0x29};        // 29 /r
constexpr uint8_t kOpXorRR{0x31};        // 31 /r
constexpr uint8_t kOpTestRR{0x85};       // 85 /r : test r/m64, r64
constexpr uint8_t kOpGrpF7{0xF7};        // F7 /n : not(2) neg(3) div(6) idiv(7)
constexpr uint8_t kOpGrpC1{0xC1};        // C1 /n ib : shl(4) shr(5) sar(7)
constexpr uint8_t kOpMovImm{0xB8};       // B8+rd io : mov r64, imm64
constexpr uint8_t kOpCqo{0x99};          // cqo (with REX.W)
constexpr uint8_t kOpEscape2Byte{0x0F};  // two-byte opcode escape
constexpr uint8_t kOpImulRR{0xAF};       // 0F AF /r : imul r64, r/m64
constexpr uint8_t kOpMovzxR64R8{0xB6};   // 0F B6 /r : movzx r64, r/m8
constexpr uint8_t kOpCmovNe{0x45};       // 0F 45 /r : cmovne r64, r/m64

// F7-group /n register-field selectors.
constexpr uint8_t kGrpNot{2};
constexpr uint8_t kGrpNeg{3};
constexpr uint8_t kGrpDiv{6};
constexpr uint8_t kGrpIDiv{7};
// C1-group shift selectors.
constexpr uint8_t kGrpShl{4};
constexpr uint8_t kGrpShr{5};
constexpr uint8_t kGrpSar{7};

// setcc opcodes (0F 9x): the low byte of the setcc family.
constexpr uint8_t kSetccEq{0x94};
constexpr uint8_t kSetccNe{0x95};
constexpr uint8_t kSetccLt{0x9C};
constexpr uint8_t kSetccGe{0x9D};
constexpr uint8_t kSetccLe{0x9E};
constexpr uint8_t kSetccGt{0x9F};
/// AL is the fixed destination of the setcc byte (rm=0 in 0F 9x C0).
constexpr uint8_t kModrmAl{0xC0};

// Registers by Intel encoding id (x86-64 GPR file).
constexpr uint16_t kRegRax{0}, kRegRcx{1}, kRegRdx{2}, kRegRbx{3};
constexpr uint16_t kRegRsi{6}, kRegRdi{7};
constexpr uint16_t kRegR8{8}, kRegR9{9}, kRegR10{10}, kRegR11{11};
constexpr uint16_t kRegR12{12}, kRegR13{13}, kRegR14{14}, kRegR15{15};

/// VReg homes, indexed by preg id. Caller-saved first, then the
/// callee-saved set the prologue preserves. RAX/RDX are deliberately
/// absent (div/idiv scratch + return; see header).
///
/// PARAMETER SAFETY: homes overlap the SysV argument registers, so a
/// naive per-param `mov home_i, arg_i` sequence can clobber an
/// unread argument (pre-fix: `mov rcx, rdi` for param 0 destroyed
/// argument 4 — caught by the runtime differential harness). The
/// encoder therefore resolves ALL param moves as one PARALLEL MOVE
/// (cycle-safe, RAX scratch) before any body instruction.
constexpr uint16_t kHomeRegs[kExecHomeRegCount] = {
    kRegRcx, kRegRsi, kRegRdi, kRegR8, kRegR9, kRegR10, kRegR11,
    kRegRbx, kRegR12, kRegR13, kRegR14, kRegR15,
};
/// First home index whose register is callee-saved.
constexpr uint32_t kFirstCalleeSavedHome{7};
/// SysV AMD64 integer argument registers, by argument index.
constexpr uint16_t kAbiArgRegs[6] = {kRegRdi, kRegRsi, kRegRdx,
                                     kRegRcx, kRegR8, kRegR9};
/// Number of SysV integer argument registers (hard ABI bound).
constexpr uint32_t kAbiMaxArgs{6};

/// Modrm byte for `xor edx, edx` (32-bit operand — zero-extends).
constexpr uint8_t kModrmXorEdxEax{0xD2};

// ---- Encoder state ----

struct Encoder {
    std::vector<uint8_t>& buf;
    std::string err;

    void put(uint8_t b) { buf.push_back(b); }
    void put_u64(uint64_t v) {
        for (uint32_t i = 0; i < elf::kU64Bytes; ++i) {
            buf.push_back(static_cast<uint8_t>(
                (v >> (i * elf::kBitsPerByte)) & elf::kByteMask));
        }
    }
    // REX.W with optional R/B extension bits for reg/rm >= 8.
    void rex(bool reg_hi, bool rm_hi) {
        uint8_t r = kRexW;
        if (reg_hi) r = static_cast<uint8_t>(r | kRexR);
        if (rm_hi)  r = static_cast<uint8_t>(r | kRexB);
        put(r);
    }
    void modrm(uint16_t reg, uint16_t rm) {
        put(static_cast<uint8_t>(kModrmRegReg |
                                 ((reg & kRegIdxMask) << kModrmRegShift) |
                                 (rm & kRegIdxMask)));
    }
    // op r/m64, r64  (opcode 01/09/21/29/31/89 family; reg=src, rm=dst)
    void op_rm_r(uint8_t opcode, uint16_t dst, uint16_t src) {
        rex(src >= kRegR8, dst >= kRegR8);
        put(opcode);
        modrm(src, dst);
    }
    void mov_rr(uint16_t dst, uint16_t src) { op_rm_r(kOpMovRmR, dst, src); }
    void push_reg(uint16_t r) {
        if (r >= kRegR8) put(kRexB);
        put(static_cast<uint8_t>(kOpPush | (r & kRegIdxMask)));
    }
    void pop_reg(uint16_t r) {
        if (r >= kRegR8) put(kRexB);
        put(static_cast<uint8_t>(kOpPop | (r & kRegIdxMask)));
    }
    void mov_imm(uint16_t dst, int64_t imm) {
        rex(false, dst >= kRegR8);
        put(static_cast<uint8_t>(kOpMovImm | (dst & kRegIdxMask)));
        put_u64(static_cast<uint64_t>(imm));
    }
    // F7-group unary op on a register (/n selector).
    void grp_f7(uint8_t selector, uint16_t rm) {
        rex(false, rm >= kRegR8);
        put(kOpGrpF7);
        put(static_cast<uint8_t>(kModrmRegReg | (selector << kModrmRegShift) |
                                 (rm & kRegIdxMask)));
    }
    // C1-group shift of a register by an immediate byte.
    void grp_c1_imm(uint8_t selector, uint16_t rm, uint8_t count) {
        rex(false, rm >= kRegR8);
        put(kOpGrpC1);
        put(static_cast<uint8_t>(kModrmRegReg | (selector << kModrmRegShift) |
                                 (rm & kRegIdxMask)));
        put(count);
    }
    void setcc_al(uint8_t setcc_low) {
        put(kOpEscape2Byte);
        put(setcc_low);
        put(kModrmAl);
    }
    void movzx_rax_al(uint16_t dst) {
        rex(dst >= kRegR8, false);
        put(kOpEscape2Byte);
        put(kOpMovzxR64R8);
        // reg field = dst, rm = AL(0).
        put(static_cast<uint8_t>(kModrmRegReg |
                                 ((dst & kRegIdxMask) << kModrmRegShift)));
    }
};

// Resolve a vreg to its home register, validating the assignment
// (Rule 73: validate before writing any byte).
bool home_of(const LinearScanAllocator& ra, VRegId v, uint16_t& out,
             Encoder& enc, const std::string& ctx) {
    if (v == kInvalidVReg) {
        enc.err = ctx + ": missing operand vreg";
        return false;
    }
    if (ra.is_spilled(v)) {
        enc.err = ctx + ": vreg " + std::to_string(v) +
                  " is spilled — executable path requires zero spills";
        return false;
    }
    PRegId preg = ra.assignment(v);
    if (preg == static_cast<PRegId>(-1) || preg >= kExecHomeRegCount) {
        enc.err = ctx + ": vreg " + std::to_string(v) +
                  " has no executable home (preg=" + std::to_string(preg) + ")";
        return false;
    }
    out = kHomeRegs[preg];
    return true;
}

} // namespace

bool encode_executable(const MachineFunction& fn,
                       const LinearScanAllocator& ra,
                       std::vector<uint8_t>& out,
                       std::string& err) {
    Encoder enc{out, err};
    out.clear();

    // ---- Prologue: preserve the callee-saved homes. ----
    for (uint32_t i = kFirstCalleeSavedHome; i < kExecHomeRegCount; ++i) {
        enc.push_reg(kHomeRegs[i]);
    }

    // ---- Parameter parallel move (cycle-safe, RAX scratch). ----
    //
    // Collect (src=ABI arg reg, dst=home) pairs first, then emit them
    // in an order where no move destroys a still-unread source; a
    // dependency cycle is broken by staging one source in RAX (RAX is
    // never a home and never an argument register).
    struct PMove { uint16_t src; uint16_t dst; };
    std::vector<PMove> pmoves;
    bool param_block_emitted = false;
    for (const auto& mi : fn.instrs) {
        if (mi.op != "param") continue;
        const std::string ctx = "param imm=" + std::to_string(mi.imm);
        if (!mi.has_imm || mi.imm < 0 ||
            mi.imm >= static_cast<int64_t>(kAbiMaxArgs)) {
            err = ctx + ": param index out of SysV range [0,6)";
            return false;
        }
        uint16_t home;
        if (!home_of(ra, mi.defs[0], home, enc, ctx)) return false;
        pmoves.push_back(PMove{kAbiArgRegs[static_cast<uint32_t>(mi.imm)],
                               home});
    }

    bool saw_ret = false;
    for (uint32_t idx = 0; idx < fn.instrs.size(); ++idx) {
        const MachineInstr& mi = fn.instrs[idx];
        const std::string ctx = "instr " + std::to_string(idx) +
                                " (" + mi.op + ")";

        if (mi.op == "param") {
            if (param_block_emitted) continue; // one block, at first param
            param_block_emitted = true;
            // Emit the moves in an order where no move's source was
            // already overwritten by an EARLIER move (src == dst
            // self-moves are free). With at most kAbiMaxArgs moves an
            // exhaustive permutation search is trivially cheap and
            // provably finds a safe order when one exists. A true
            // dependency CYCLE (no safe order) is broken by staging
            // one source in RAX (never a home, never an argument).
            // (The pending-only check used before missed that an
            // already-emitted move can clobber a later source —
            // observed as a wrong runtime result by the harness.)
            std::vector<PMove> moves = pmoves;
            bool staged = false;
            while (true) {
                // Try every ordering of the current move set.
                std::vector<uint32_t> order(moves.size());
                for (uint32_t k = 0; k < order.size(); ++k) {
                    order[k] = k;
                }
                bool found = false;
                do {
                    bool ok = true;
                    for (size_t i = 0; i < order.size() && ok; ++i) {
                        const uint16_t src = moves[order[i]].src;
                        for (size_t j = 0; j < i; ++j) {
                            const uint16_t dst = moves[order[j]].dst;
                            if (dst == src && src != moves[order[i]].dst) {
                                ok = false; // source clobbered earlier
                                break;
                            }
                        }
                    }
                    if (ok) {
                        for (uint32_t i : order) {
                            enc.mov_rr(moves[i].dst, moves[i].src);
                        }
                        found = true;
                        break;
                    }
                } while (std::next_permutation(order.begin(), order.end()));
                if (found) break;
                if (staged) {
                    // Even after staging, no safe order: unreachable
                    // for <=6 moves over 16 registers — fail loudly.
                    err = "param parallel move: unresolvable cycle";
                    return false;
                }
                enc.mov_rr(kRegRax, moves[0].src);
                moves[0].src = kRegRax; // staged; RAX is nobody's dst
                staged = true;
            }
            continue;
        }
        if (mi.op == "ret") {
            uint16_t src;
            if (!home_of(ra, mi.uses[0], src, enc, ctx)) return false;
            enc.mov_rr(kRegRax, src);
            for (uint32_t i = kExecHomeRegCount; i-- > kFirstCalleeSavedHome;) {
                enc.pop_reg(kHomeRegs[i]);
            }
            enc.put(kOpRet);
            saw_ret = true;
            continue;
        }
        if (mi.op == "mov_imm") {
            if (!mi.has_imm) { err = ctx + ": mov_imm without immediate"; return false; }
            uint16_t home;
            if (!home_of(ra, mi.defs[0], home, enc, ctx)) return false;
            enc.mov_imm(home, mi.imm);
            continue;
        }
        if (mi.op == "add" || mi.op == "sub" || mi.op == "and" ||
            mi.op == "or" || mi.op == "xor") {
            uint16_t dst, a, b;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx) ||
                !home_of(ra, mi.uses[1], b, enc, ctx)) return false;
            if (dst != a) enc.mov_rr(dst, a);
            const uint8_t opcode = mi.op == "add" ? kOpAddRR
                                 : mi.op == "sub" ? kOpSubRR
                                 : mi.op == "and" ? kOpAndRR
                                 : mi.op == "or"  ? kOpOrRR
                                                  : kOpXorRR;
            enc.op_rm_r(opcode, dst, b);
            continue;
        }
        if (mi.op == "mul") {
            uint16_t dst, a, b;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx) ||
                !home_of(ra, mi.uses[1], b, enc, ctx)) return false;
            if (dst != a) enc.mov_rr(dst, a);
            enc.rex(dst >= kRegR8, b >= kRegR8);
            enc.put(kOpEscape2Byte);
            enc.put(kOpImulRR);
            enc.modrm(dst, b);
            continue;
        }
        if (mi.op == "div" || mi.op == "mod" ||
            mi.op == "udiv" || mi.op == "umod") {
            uint16_t dst, a, b;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx) ||
                !home_of(ra, mi.uses[1], b, enc, ctx)) return false;
            enc.mov_rr(kRegRax, a);
            if (mi.op == "udiv" || mi.op == "umod") {
                // Zero-extend the dividend: xor edx, edx.
                enc.put(kOpXorRR);
                enc.put(kModrmXorEdxEax);
                enc.grp_f7(kGrpDiv, b);
            } else {
                // Sign-extend rax into rdx:rax: cqo.
                enc.put(kRexW);
                enc.put(kOpCqo);
                enc.grp_f7(kGrpIDiv, b);
            }
            // Quotient lands in RAX, remainder in RDX.
            enc.mov_rr(dst, mi.op == "div" || mi.op == "udiv" ? kRegRax
                                                              : kRegRdx);
            continue;
        }
        if (mi.op == "shl" || mi.op == "sar" || mi.op == "shr") {
            if (mi.uses[1] != kInvalidVReg) {
                // Shift amount is a runtime value: needs the CL form,
                // which makes RCX a clobber — unsupported (loud).
                err = ctx + ": shift-by-register unsupported (constant "
                      "shift amounts only)";
                return false;
            }
            if (!mi.has_imm || mi.imm < 0) {
                err = ctx + ": shift without immediate count";
                return false;
            }
            uint16_t dst, a;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx)) return false;
            if (dst != a) enc.mov_rr(dst, a);
            const uint8_t selector = mi.op == "shl" ? kGrpShl
                                   : mi.op == "sar" ? kGrpSar
                                                    : kGrpShr;
            enc.grp_c1_imm(selector, dst, static_cast<uint8_t>(mi.imm));
            continue;
        }
        if (mi.op == "neg" || mi.op == "bnot") {
            uint16_t dst, a;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx)) return false;
            if (dst != a) enc.mov_rr(dst, a);
            enc.grp_f7(mi.op == "neg" ? kGrpNeg : kGrpNot, dst);
            continue;
        }
        if (mi.op == "lnot") {
            // dst = (a == 0) ? 1 : 0.
            uint16_t dst, a;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx)) return false;
            enc.op_rm_r(kOpTestRR, a, a); // test a, a (dst untouched)
            enc.setcc_al(kSetccEq);
            enc.movzx_rax_al(dst);
            continue;
        }
        if (mi.op.rfind("cmp_", 0) == 0) {
            uint16_t dst, a, b;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], a, enc, ctx) ||
                !home_of(ra, mi.uses[1], b, enc, ctx)) return false;
            // cmp a, b: opcode 3B /r (reg = left operand).
            enc.rex(a >= kRegR8, b >= kRegR8);
            enc.put(kOpCmpRmR);
            enc.modrm(a, b);
            const bool is_eq  = mi.op == "cmp_eq";
            const bool is_ne  = mi.op == "cmp_ne";
            const bool is_lt  = mi.op == "cmp_lt";
            const bool is_le  = mi.op == "cmp_le";
            const bool is_gt  = mi.op == "cmp_gt";
            const bool is_ge  = mi.op == "cmp_ge";
            if (!(is_eq || is_ne || is_lt || is_le || is_gt || is_ge)) {
                err = ctx + ": unsigned compare forms are not emitted "
                      "by instruction selection yet";
                return false;
            }
            enc.setcc_al(is_eq ? kSetccEq
                      : is_ne ? kSetccNe
                      : is_lt ? kSetccLt
                      : is_le ? kSetccLe
                      : is_gt ? kSetccGt
                              : kSetccGe);
            enc.movzx_rax_al(dst);
            continue;
        }
        if (mi.op == "select") {
            // dst = cond ? tv : fv, branchless:
            //   mov dst, fv ; test cond, cond ; cmovne dst, tv
            // (cond is a 0/1 value; ZF=0 means nonzero means true.)
            uint16_t dst, cond, tv, fv;
            if (!home_of(ra, mi.defs[0], dst, enc, ctx) ||
                !home_of(ra, mi.uses[0], cond, enc, ctx) ||
                !home_of(ra, mi.uses[1], tv, enc, ctx) ||
                !home_of(ra, mi.uses[2], fv, enc, ctx)) return false;
            enc.mov_rr(dst, fv);
            enc.op_rm_r(kOpTestRR, cond, cond);
            enc.rex(dst >= kRegR8, tv >= kRegR8);
            enc.put(kOpEscape2Byte);
            enc.put(kOpCmovNe);
            // CMOVcc r64, r/m64: reg field = destination, r/m = source.
            enc.modrm(dst, tv);
            continue;
        }
        if (mi.op == "load" || mi.op == "store") {
            err = ctx + ": memory operations are not supported in the "
                  "executable path (straight-line integer code only)";
            return false;
        }
        err = ctx + ": unknown machine op";
        return false;
    }

    if (!saw_ret) {
        err = "machine function '" + fn.name + "' has no return";
        return false;
    }
    return true;
}

} // namespace aegis::backend::x86
