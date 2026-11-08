#include "a64_macho.h"
#include "darwin_objc.h"

/* ARM64 is always interpreted here. No foreign machine code enters ring 0. */
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static bool within(size_t limit, size_t at, size_t size) {
    return at <= limit && size <= limit - at;
}
static void bytes_copy(uint8_t *to, const uint8_t *from, size_t size) {
    for (size_t i = 0; i < size; ++i) to[i] = from[i];
}
static void bytes_zero(uint8_t *to, size_t size) {
    for (size_t i = 0; i < size; ++i) to[i] = 0;
}

/* Guest pointers are never dereferenced as kernel pointers. All loads
 * and stores validate both the low and high address before touching RAM.
 */
static bool a64_guest_offset(uint64_t address, size_t bytes,
                             size_t capacity, size_t *offset) {
    if (address < A64_GUEST_BASE ||
        address - A64_GUEST_BASE > capacity ||
        bytes > capacity - (size_t)(address - A64_GUEST_BASE))
        return false;
    *offset=(size_t)(address-A64_GUEST_BASE);
    return true;
}
static bool a64_load(const uint8_t *guest, size_t size,
                     uint64_t address, unsigned bytes, uint64_t *value) {
    size_t off;
    if (!a64_guest_offset(address, bytes, size, &off)) return false;
    uint64_t x=0;
    for (unsigned i=0;i<bytes;++i) x|=(uint64_t)guest[off+i]<<(8u*i);
    *value=x;
    return true;
}
static bool a64_store(uint8_t *guest, size_t size,
                      uint64_t address, unsigned bytes, uint64_t value,
                      size_t readonly_until) {
    size_t off;
    if (!a64_guest_offset(address, bytes, size, &off) ||
        off < readonly_until) return false;
    for (unsigned i=0;i<bytes;++i) guest[off+i]=(uint8_t)(value>>(8u*i));
    return true;
}
static int32_t a64_sign_extend(unsigned raw, unsigned bits) {
    unsigned sign=1u<<(bits-1u);
    return (int32_t)((raw^sign)-sign);
}
static bool a64_condition(unsigned condition, bool n, bool z,
                          bool c, bool v, bool *taken) {
    switch (condition) {
    case 0: *taken=z; break;                 /* EQ */
    case 1: *taken=!z; break;                /* NE */
    case 2: *taken=c; break;                 /* CS */
    case 3: *taken=!c; break;                /* CC */
    case 4: *taken=n; break;                 /* MI */
    case 5: *taken=!n; break;                /* PL */
    case 6: *taken=v; break;                 /* VS */
    case 7: *taken=!v; break;                /* VC */
    case 8: *taken=c && !z; break;           /* HI */
    case 9: *taken=!c || z; break;           /* LS */
    case 10: *taken=n==v; break;             /* GE */
    case 11: *taken=n!=v; break;             /* LT */
    case 12: *taken=!z && n==v; break;       /* GT */
    case 13: *taken=z || n!=v; break;        /* LE */
    default: return false;                  /* AL/NV refused */
    }
    return true;
}

static bool a64_run_impl(const uint8_t *file, size_t file_size,
                         uint8_t *guest, size_t guest_size,
                         a64_output_t output, void *context,
                         a64_report_t *report, bool darwin_mode) {
    if (!report) return false;
    report->finished = false;
    report->exit_code = -1;
    report->steps = 0;
    report->unsupported_opcode = 0;
    if (!file || !guest || !output ||
        file_size < 32 || file_size > A64_MACHO_MAX_FILE ||
        guest_size != A64_GUEST_BYTES ||
        le32(file) != 0xfeedfacfu ||        /* MH_MAGIC_64 */
        le32(file + 4) != 0x0100000cu ||    /* CPU_TYPE_ARM64 */
        le32(file + 8) != 0 ||              /* CPU_SUBTYPE_ARM64_ALL */
        le32(file + 12) != 2)               /* MH_EXECUTE */
        return false;

    uint32_t commands = le32(file + 16);
    uint32_t command_bytes = le32(file + 20);
    if (commands < 2 || commands > 8 ||
        !within(file_size, 32, command_bytes)) return false;

    uint64_t segment_base = 0;
    uint64_t segment_size = 0;
    uint64_t segment_file_size = 0;
    uint64_t main_offset = 0;
    bool segment_found = false, main_found = false;

    size_t at = 32, end = 32 + command_bytes;
    for (uint32_t i = 0; i < commands; ++i) {
        if (!within(end, at, 8)) return false;
        uint32_t command = le32(file + at);
        uint32_t command_size = le32(file + at + 4);
        if (command_size < 8 || !within(end, at, command_size)) return false;
        if (command == 0x19u) {           /* LC_SEGMENT_64 */
            if (segment_found || command_size != 72) return false;
            segment_found = true;
            segment_base = le64(file + at + 24);
            segment_size = le64(file + at + 32);
            uint64_t file_offset = le64(file + at + 40);
            segment_file_size = le64(file + at + 48);
            uint32_t initial_protection = le32(file + at + 60);
            uint32_t sections = le32(file + at + 64);
            /* Only one standalone __TEXT segment, no dependencies. */
            if (segment_base != A64_GUEST_BASE ||
                segment_size == 0 || segment_size > guest_size ||
                file_offset != 0 || segment_file_size > file_size ||
                segment_file_size > segment_size ||
                initial_protection != 5 || sections != 0) return false;
        } else if (command == 0x80000028u) {   /* LC_MAIN */
            if (main_found || command_size != 24) return false;
            main_found = true;
            main_offset = le64(file + at + 8);
        } else {
            /* No dyld, dylibs, shared cache or unknown load commands. */
            return false;
        }
        at += command_size;
    }
    if (!segment_found || !main_found || at != end ||
        main_offset > segment_file_size ||
        segment_file_size - main_offset < 4 ||
        (main_offset & 3u)) return false;
    bytes_zero(guest, guest_size);
    bytes_copy(guest, file, (size_t)segment_file_size);

    uint64_t registers[31] = {0};
    uint64_t stack_pointer=A64_GUEST_BASE + A64_GUEST_BYTES - 16u;
    bool flag_n=false, flag_z=false, flag_c=false, flag_v=false;
    darwin_objc_state_t objc_state = {0};
    uint64_t pc = A64_GUEST_BASE + main_offset;
    for (uint32_t step = 0; step < A64_MAX_STEPS; ++step) {
        uint64_t offset = pc - A64_GUEST_BASE;
        if (pc < A64_GUEST_BASE || offset >= segment_file_size ||
            segment_file_size - offset < 4 || (pc & 3u)) return false;
        uint32_t instruction = le32(guest + (size_t)offset);
        report->steps = step + 1;

        if ((instruction & 0xff800000u) == 0xd2800000u) {
            /* MOVZ Xd, imm16, LSL shift (0,16,32,48). */
            uint32_t dest = instruction & 31u;
            uint32_t immediate = (instruction >> 5) & 0xffffu;
            uint32_t shift = ((instruction >> 21) & 3u) * 16u;
            if (dest != 31u) registers[dest] = (uint64_t)immediate << shift;
            pc += 4;
            continue;
        }
        if ((instruction & 0x9f000000u) == 0x10000000u) {
            /* ADR Xd, signed 21-bit PC-relative byte offset. */
            uint32_t dest = instruction & 31u;
            int32_t displacement = (int32_t)(
                (((instruction >> 5) & 0x7ffffu) << 2) |
                ((instruction >> 29) & 3u));
            if (displacement & (1 << 20)) displacement -= (1 << 21);
            if (dest != 31u)
                registers[dest] = (uint64_t)((int64_t)pc + displacement);
            pc += 4;
            continue;
        }
        if ((instruction & 0xffe0ffe0u) == 0xaa0003e0u) {
            /* ARM64 MOV Xd,Xm (ORR Xd,XZR,Xm alias). */
            unsigned rd=instruction & 31u;
            unsigned rm=(instruction >> 16) & 31u;
            if (rd!=31u) registers[rd]=rm==31u ? 0 : registers[rm];
            pc+=4;
            continue;
        }

        /* ARMv8 move-wide 32/64 bit: MOVZ, MOVN, MOVK. */
        if ((instruction & 0x1f800000u)==0x12800000u) {
            unsigned op=(instruction>>29)&3u;
            bool sf=(instruction>>31)!=0;
            unsigned shift=(instruction>>21)&3u;
            unsigned rd=instruction&31u;
            if ((!sf && shift>=2u) || op==1u) return false;
            uint64_t val=(uint64_t)((instruction>>5)&65535u)<<(shift*16u);
            if (op==0u) val=~val;       /* MOVN */
            if (op==3u) {              /* MOVK */
                uint64_t old=rd==31u ? 0 : registers[rd];
                val=(old & ~(0xffffULL<<(shift*16u))) | val;
            }
            if (!sf) val=(uint32_t)val;
            if (rd!=31u) registers[rd]=val;
            pc+=4; continue;
        }
        /* ADD/SUB immediate or register, including CMP aliases and SP.
         * Updates N/Z/C/V as defined by ARM for ADDS/SUBS. */
        bool alu_immediate=(instruction & 0x1f000000u)==0x11000000u;
        bool alu_register=(instruction & 0x1f200000u)==0x0b000000u;
        if (alu_immediate || alu_register) {
            bool sf=(instruction>>31)!=0;
            bool subtract=(instruction & 0x40000000u)!=0;
            bool setflags=(instruction & 0x20000000u)!=0;
            unsigned rd=instruction&31u, rn=(instruction>>5)&31u;
            uint64_t lhs=(rn==31u && alu_immediate) ?
                stack_pointer : (rn==31u ? 0 : registers[rn]);
            uint64_t rhs=0;
            if (alu_immediate) {
                unsigned shift=(instruction>>22)&3u;
                if (shift>1u) return false;
                rhs=(instruction>>10)&4095u;
                rhs<<=(shift*12u);
            } else {
                /* Only LSL, no extended-register or rotate variants. */
                unsigned shift=(instruction>>22)&3u;
                unsigned amount=(instruction>>10)&63u;
                unsigned rm=(instruction>>16)&31u;
                if (shift!=0 || (!sf && amount>=32u)) return false;
                rhs=(rm==31u ? 0 : registers[rm])<<amount;
            }
            uint64_t mask=sf ? UINT64_MAX : 0xffffffffULL;
            lhs&=mask; rhs&=mask;
            uint64_t value=(subtract ? lhs-rhs : lhs+rhs)&mask;
            if (setflags) {
                uint64_t sign=sf ? (1ULL<<63) : (1ULL<<31);
                flag_n=(value&sign)!=0;
                flag_z=value==0;
                flag_c=subtract ? lhs>=rhs :
                    (sf ? value<lhs : (lhs+rhs)>0xffffffffULL);
                flag_v=subtract ?
                    (((lhs^rhs)&(lhs^value)&sign)!=0) :
                    (((~(lhs^rhs))&(lhs^value)&sign)!=0);
            }
            if (rd!=31u) registers[rd]=value;
            else if (!setflags && alu_immediate) stack_pointer=value;
            pc+=4; continue;
        }
        /* B and BL, signed PC-relative offsets measured in instructions. */
        if ((instruction & 0x7c000000u)==0x14000000u) {
            int32_t delta=a64_sign_extend(instruction&0x03ffffffu,26);
            if (instruction & 0x80000000u) registers[30]=pc+4;
            pc=(uint64_t)((int64_t)pc+(int64_t)delta*4);
            continue;
        }
        /* B.cond: EQ/NE plus all architecturally regular conditions. */
        if ((instruction & 0xff000010u)==0x54000000u) {
            bool taken=false;
            if (!a64_condition(instruction&15u,flag_n,flag_z,
                               flag_c,flag_v,&taken)) return false;
            if (taken) {
                int32_t delta=a64_sign_extend((instruction>>5)&0x7ffffu,19);
                pc=(uint64_t)((int64_t)pc+(int64_t)delta*4);
            } else pc+=4;
            continue;
        }
        /* CBZ/CBNZ on X or W values. */
        if ((instruction & 0x7e000000u)==0x34000000u) {
            unsigned rt=instruction&31u;
            uint64_t value=rt==31u ? 0 : registers[rt];
            if (!(instruction&0x80000000u)) value=(uint32_t)value;
            bool nz=(instruction&0x01000000u)!=0;
            bool taken=(value!=0)==nz;
            if (taken) {
                int32_t delta=a64_sign_extend((instruction>>5)&0x7ffffu,19);
                pc=(uint64_t)((int64_t)pc+(int64_t)delta*4);
            } else pc+=4;
            continue;
        }
        /* RET Xn, no host returns or native guest execution. */
        if ((instruction & 0xfffffc1fu)==0xd65f0000u) {
            unsigned rn=(instruction>>5)&31u;
            if (rn==31u) return false;
            pc=registers[rn];
            continue;
        }
        /* ARM64 32-/64-bit LDR/STR unsigned-scaled offset.
         * Guest executable bytes are read-only, stack/data are writable. */
        uint32_t ls_type=instruction & 0xffc00000u;
        if (ls_type==0xf9400000u || ls_type==0xf9000000u ||
            ls_type==0xb9400000u || ls_type==0xb9000000u) {
            unsigned width=(instruction&0x40000000u) ? 8u : 4u;
            unsigned rt=instruction&31u, rn=(instruction>>5)&31u;
            uint64_t base=rn==31u ? stack_pointer : registers[rn];
            uint64_t offset=((instruction>>10)&4095u)*width;
            if (UINT64_MAX-base<offset) return false;
            uint64_t addr=base+offset;
            if ((instruction & 0x00400000u)!=0) {
                uint64_t v64;
                if (!a64_load(guest,guest_size,addr,width,&v64)) return false;
                if (rt!=31u) registers[rt]=v64;
            } else {
                uint64_t val=rt==31u ? 0 : registers[rt];
                if (!a64_store(guest,guest_size,addr,width,val,
                               (size_t)segment_file_size)) return false;
            }
            pc+=4; continue;
        }
        /* STP Xn,Xm,[base,#signed_imm]! and LDP post-index. */
        uint32_t pair_type=instruction&0xffc00000u;
        if (pair_type==0xa9800000u || pair_type==0xa8c00000u) {
            bool load=pair_type==0xa8c00000u;
            unsigned rt=instruction&31u, rt2=(instruction>>10)&31u;
            unsigned rn=(instruction>>5)&31u;
            int32_t delta=a64_sign_extend((instruction>>15)&127u,7)*8;
            uint64_t base=rn==31u ? stack_pointer : registers[rn];
            uint64_t addr=load ? base : (uint64_t)((int64_t)base+delta);
            size_t off;
            if (!a64_guest_offset(addr,16u,guest_size,&off)) return false;
            if (load) {
                uint64_t first,second;
                if (!a64_load(guest,guest_size,addr,8u,&first) ||
                    !a64_load(guest,guest_size,addr+8u,8u,&second))
                    return false;
                if (rt!=31u) registers[rt]=first;
                if (rt2!=31u) registers[rt2]=second;
            } else {
                if (!a64_store(guest,guest_size,addr,8u,
                               rt==31u ? 0 : registers[rt],
                               (size_t)segment_file_size) ||
                    !a64_store(guest,guest_size,addr+8u,8u,
                               rt2==31u ? 0 : registers[rt2],
                               (size_t)segment_file_size)) return false;
            }
            uint64_t updated=load ?
                (uint64_t)((int64_t)base+delta) : addr;
            if (rn==31u) stack_pointer=updated;
            else registers[rn]=updated;
            pc+=4; continue;
        }
        if (instruction==0xd503201fu) {pc+=4;continue;} /* NOP */

        if ((instruction & 0xffe0001fu) == 0xd4000001u) {
            unsigned immediate=(instruction >> 5) & 0xffffu;
            if (darwin_mode && immediate == 0x80u) {
                /* Darwin ARM64 BSD syscall subset, not Linux or Bordify ABI.
                 * write(4), exit(1), getpid(20); unsupported calls fail. */
                if (registers[16]==4) {
                    uint64_t addr=registers[1], count=registers[2];
                    if (registers[0]!=1 || addr < A64_GUEST_BASE ||
                        addr-A64_GUEST_BASE > guest_size || count>256 ||
                        count>guest_size-(size_t)(addr-A64_GUEST_BASE))
                        return false;
                    output(guest+(size_t)(addr-A64_GUEST_BASE),
                           (size_t)count,context);
                    registers[0]=count;
                    pc+=4;
                    continue;
                }
                if (registers[16]==20) {
                    registers[0]=1; /* fixed synthetic process ID */
                    pc+=4;
                    continue;
                }
                if (registers[16]==1) {
                    report->exit_code=(int)(int32_t)registers[0];
                    report->finished=true;
                    return true;
                }
                return false;
            }
            if (darwin_mode && immediate==0x81u) {
                /* Experimental objc_* guest bridge. This is NOT dyld,
                 * actual libobjc linking, or a Cocoa GUI framework. */
                if (!darwin_objc_call(registers[8],registers,guest,guest_size,
                                      A64_GUEST_BASE,&objc_state))
                    return false;
                pc+=4;
                continue;
            }
            /* SVC #0: guest demonstration ABI, NOT a Darwin syscall. */
            if (immediate != 0) return false;
            if (registers[8] == 1) {
                uint64_t address = registers[0], count = registers[1];
                if (address < A64_GUEST_BASE ||
                    address - A64_GUEST_BASE > guest_size ||
                    count > 256 ||
                    count > guest_size - (size_t)(address - A64_GUEST_BASE))
                    return false;
                output(guest + (size_t)(address - A64_GUEST_BASE),
                       (size_t)count, context);
                pc += 4;
                continue;
            }
            if (registers[8] == 2) {
                report->exit_code = (int)(int32_t)registers[0];
                report->finished = true;
                return true;
            }
            return false;
        }
        report->unsupported_opcode = instruction;
        return false;
    }
    return false;     /* bounded instruction count */
}

bool a64_macho_run(const uint8_t *file, size_t file_size,
                   uint8_t *guest, size_t guest_size,
                   a64_output_t output, void *context,
                   a64_report_t *report) {
    return a64_run_impl(file,file_size,guest,guest_size,output,context,
                        report,false);
}
bool a64_macho_run_darwin(const uint8_t *file, size_t file_size,
                          uint8_t *guest, size_t guest_size,
                          a64_output_t output, void *context,
                          a64_report_t *report) {
    return a64_run_impl(file,file_size,guest,guest_size,output,context,
                        report,true);
}
