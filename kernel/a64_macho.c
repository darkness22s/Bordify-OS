#include "a64_macho.h"

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

bool a64_macho_run(const uint8_t *file, size_t file_size,
                   uint8_t *guest, size_t guest_size,
                   a64_output_t output, void *context,
                   a64_report_t *report) {
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
        if ((instruction & 0xffe0001fu) == 0xd4000001u) {
            /* SVC #0: guest demonstration ABI, NOT a Darwin syscall. */
            if (((instruction >> 5) & 0xffffu) != 0) return false;
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
