#include "dwarf_cfi.h"

#if KOTATSU_SYS_DWARF_CFI

#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

#include "frames.h"

namespace kotatsu::sys::dwarf {

namespace {

/// DW_EH_PE: a pointer's format in the low four bits, what it is relative to
/// in the next three.
enum : std::uint8_t {
    pe_absptr = 0x00,
    pe_uleb128 = 0x01,
    pe_udata2 = 0x02,
    pe_udata4 = 0x03,
    pe_udata8 = 0x04,
    pe_sleb128 = 0x09,
    pe_sdata2 = 0x0A,
    pe_sdata4 = 0x0B,
    pe_sdata8 = 0x0C,
    pe_pcrel = 0x10,
    pe_datarel = 0x30,
};

std::uint64_t load(std::uint64_t address) noexcept {
    std::uint64_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

/// Reads CFI, whose fields may be unaligned.
struct Cursor {
    const std::uint8_t* at;

    template <typename T>
    T read() noexcept {
        T value;
        std::memcpy(&value, at, sizeof(value));
        at += sizeof(value);
        return value;
    }

    std::uint64_t uleb() noexcept {
        std::uint64_t value = 0;
        unsigned shift = 0;
        std::uint8_t byte = 0;
        do {
            byte = *at++;
            if(shift < 64) {
                value |= std::uint64_t(byte & 0x7F) << shift;
            }
            shift += 7;
        } while(byte & 0x80);
        return value;
    }

    std::int64_t sleb() noexcept {
        std::uint64_t value = 0;
        unsigned shift = 0;
        std::uint8_t byte = 0;
        do {
            byte = *at++;
            if(shift < 64) {
                value |= std::uint64_t(byte & 0x7F) << shift;
            }
            shift += 7;
        } while(byte & 0x80);
        if(shift < 64 && (byte & 0x40)) {
            value |= ~std::uint64_t(0) << shift;
        }
        return static_cast<std::int64_t>(value);
    }

    /// The start of the block here, its length and then its bytes, which
    /// this moves past.
    const std::uint8_t* block() noexcept {
        const auto* start = at;
        auto length = uleb();
        at += length;
        return start;
    }

    /// A pointer in `encoding`, a pe_datarel one relative to `*data`: false
    /// for an encoding this does not read, pe_datarel one without `data`.
    /// An indirect one, only a personality routine's, is left unfollowed.
    bool pointer(std::uint8_t encoding, const std::uintptr_t* data, std::uintptr_t& out) noexcept {
        auto field = reinterpret_cast<std::uintptr_t>(at);
        std::uint64_t value = 0;
        switch(encoding & 0x0F) {
            case pe_absptr:
            case pe_udata8:
            case pe_sdata8: value = read<std::uint64_t>(); break;
            case pe_uleb128: value = uleb(); break;
            case pe_udata2: value = read<std::uint16_t>(); break;
            case pe_udata4: value = read<std::uint32_t>(); break;
            case pe_sleb128: value = static_cast<std::uint64_t>(sleb()); break;
            case pe_sdata2: value = static_cast<std::uint64_t>(read<std::int16_t>()); break;
            case pe_sdata4: value = static_cast<std::uint64_t>(read<std::int32_t>()); break;
            default: return false;
        }
        switch(encoding & 0x70) {
            case 0: break;
            case pe_pcrel: value += field; break;
            case pe_datarel:
                if(data == nullptr) {
                    return false;
                }
                value += *data;
                break;
            default: return false;
        }
        out = value;
        return true;
    }

    /// The end of the entry, a CIE or an FDE, whose length is here; moves
    /// past the length, setting `wide` when it is DWARF64's.
    const std::uint8_t* entry(bool& wide) noexcept {
        std::uint64_t length = read<std::uint32_t>();
        wide = length == 0xFFFFFFFF;
        if(wide) {
            length = read<std::uint64_t>();
        }
        return at + length;
    }
};

/// A Common Information Entry.
struct Cie {
    std::uint64_t code_alignment = 0;
    std::int64_t data_alignment = 0;
    std::uint64_t return_register = 0;
    std::uint8_t fde_encoding = pe_absptr;
    /// Whether its FDEs have augmentation data ("z").
    bool augmented = false;
    /// Whether its frames are signal trampolines' ("S").
    bool signal_frame = false;
    const std::uint8_t* instructions = nullptr;
    const std::uint8_t* end = nullptr;
};

bool parse_cie(const std::uint8_t* at, Cie& cie) noexcept {
    Cursor cursor{at};
    bool wide = false;
    cie.end = cursor.entry(wide);
    // .eh_frame's CIE ID.
    if((wide ? cursor.read<std::uint64_t>() : cursor.read<std::uint32_t>()) != 0) {
        return false;
    }
    auto version = cursor.read<std::uint8_t>();
    if(version != 1 && version != 3) {
        return false;
    }
    const auto* augmentation = reinterpret_cast<const char*>(cursor.at);
    cursor.at += std::strlen(augmentation) + 1;
    cie.code_alignment = cursor.uleb();
    cie.data_alignment = cursor.sleb();
    cie.return_register = version == 1 ? cursor.read<std::uint8_t>() : cursor.uleb();
    if(*augmentation == 'z') {
        cie.augmented = true;
        auto length = cursor.uleb();
        const auto* data_end = cursor.at + length;
        for(const auto* letter = augmentation + 1; *letter != '\0'; ++letter) {
            switch(*letter) {
                case 'R': cie.fde_encoding = cursor.read<std::uint8_t>(); break;
                case 'P': {
                    auto encoding = cursor.read<std::uint8_t>();
                    std::uintptr_t personality = 0;
                    if(!cursor.pointer(encoding, nullptr, personality)) {
                        return false;
                    }
                    break;
                }
                case 'L': cursor.read<std::uint8_t>(); break;
                case 'S': cie.signal_frame = true; break;
                // arm64's branch target identification and memory tagging,
                // which have no data.
                case 'B':
                case 'G': break;
                default: return false;
            }
        }
        cursor.at = data_end;
    } else if(*augmentation != '\0') {
        // One without "z", as GCC 2's "eh", whose data nothing measures.
        return false;
    }
    cie.instructions = cursor.at;
    return true;
}

/// A Frame Description Entry: the CFI of a function.
struct Fde {
    Cie cie;
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    const std::uint8_t* instructions = nullptr;
    const std::uint8_t* instructions_end = nullptr;
};

/// The FDE of the function `pc` is in, from the table of .eh_frame_hdr
/// `header`: the one whose function starts last at or before `pc`, if `pc`
/// is before its end. Every linker writes the table in pe_datarel and
/// pe_sdata4.
bool find_fde(const std::uint8_t* header, std::uintptr_t pc, Fde& fde) noexcept {
    Cursor cursor{header};
    auto version = cursor.read<std::uint8_t>();
    auto frame_encoding = cursor.read<std::uint8_t>();
    auto count_encoding = cursor.read<std::uint8_t>();
    auto table_encoding = cursor.read<std::uint8_t>();
    const auto base = reinterpret_cast<std::uintptr_t>(header);
    std::uintptr_t frame = 0;
    std::uintptr_t count = 0;
    if(version != 1 || table_encoding != (pe_datarel | pe_sdata4) ||
       !cursor.pointer(frame_encoding, &base, frame) ||
       !cursor.pointer(count_encoding, &base, count)) {
        return false;
    }
    const auto* table = cursor.at;
    auto entry = [&](std::size_t index, std::size_t field) {
        std::int32_t offset = 0;
        std::memcpy(&offset, table + index * 8 + field * 4, sizeof(offset));
        return base + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(offset));
    };
    std::size_t low = 0;
    std::size_t high = count;
    while(low < high) {
        auto middle = low + (high - low) / 2;
        if(entry(middle, 0) <= pc) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if(low == 0) {
        return false;
    }

    cursor.at = reinterpret_cast<const std::uint8_t*>(entry(low - 1, 1));
    bool wide = false;
    fde.instructions_end = cursor.entry(wide);
    const auto* field = cursor.at;
    std::uint64_t to_cie = wide ? cursor.read<std::uint64_t>() : cursor.read<std::uint32_t>();
    // 0 marks a CIE, which the table never points to.
    if(to_cie == 0 || !parse_cie(field - to_cie, fde.cie)) {
        return false;
    }
    std::uintptr_t range = 0;
    if(!cursor.pointer(fde.cie.fde_encoding, nullptr, fde.start) ||
       !cursor.pointer(fde.cie.fde_encoding & 0x0F, nullptr, range)) {
        return false;
    }
    if(fde.cie.augmented) {
        auto length = cursor.uleb();
        cursor.at += length;
    }
    fde.end = fde.start + range;
    fde.instructions = cursor.at;
    return pc < fde.end;
}

enum class Rule : std::uint8_t {
    Same,
    Undefined,
    Offset,
    ValOffset,
    Register,
    Expression,
    ValExpression,
};

/// How a register of the caller is found.
struct RegisterRule {
    Rule rule = Rule::Same;
    /// The offset from the CFA, the register, or the address of the block of
    /// the expression.
    std::int64_t value = 0;
};

/// The rules at an address of a function: its CFA, and its caller's
/// registers.
struct Row {
    std::uint64_t cfa_register = 0;
    std::int64_t cfa_offset = 0;
    /// The block of DW_CFA_def_cfa_expression's, or null for a register and
    /// an offset.
    const std::uint8_t* cfa_expression = nullptr;
    RegisterRule registers[register_count];
};

/// Runs the CFA instructions from `cursor` to `end` into `row`, `location`
/// the address of the function they are at, until those of an address past
/// `pc`. `initial` is the row of the CIE's instructions, which
/// DW_CFA_restore takes a register's rule back to.
bool run(Cursor cursor,
         const std::uint8_t* end,
         const Cie& cie,
         std::uintptr_t location,
         std::uintptr_t pc,
         Row& row,
         const Row& initial) noexcept {
    // Compilers nest remember_state once or twice.
    Row remembered[4];
    std::size_t depth = 0;
    auto set = [&](std::uint64_t reg, Rule rule, std::int64_t value) {
        // A register no caller is found by, such as arm64's vector ones.
        if(reg < register_count) {
            row.registers[reg] = {.rule = rule, .value = value};
        }
    };
    auto restore = [&](std::uint64_t reg) {
        if(reg < register_count) {
            row.registers[reg] = initial.registers[reg];
        }
    };
    auto factored = [&](std::int64_t offset) {
        return offset * cie.data_alignment;
    };
    auto advance = [&](std::uint64_t delta) {
        location += delta * cie.code_alignment;
        return location <= pc;
    };
    while(cursor.at < end) {
        auto op = cursor.read<std::uint8_t>();
        auto low = static_cast<std::uint64_t>(op & 0x3F);
        switch(op >> 6) {
            // DW_CFA_advance_loc, DW_CFA_offset and DW_CFA_restore, their
            // first operand in the low bits.
            case 1:
                if(!advance(low)) {
                    return true;
                }
                continue;
            case 2:
                set(low, Rule::Offset, factored(static_cast<std::int64_t>(cursor.uleb())));
                continue;
            case 3: restore(low); continue;
            default: break;
        }
        switch(op) {
            // DW_CFA_nop
            case 0x00: break;
            // DW_CFA_set_loc
            case 0x01: {
                std::uintptr_t address = 0;
                if(!cursor.pointer(cie.fde_encoding, nullptr, address)) {
                    return false;
                }
                if(address > pc) {
                    return true;
                }
                location = address;
                break;
            }
            // DW_CFA_advance_loc1, 2 and 4
            case 0x02:
                if(!advance(cursor.read<std::uint8_t>())) {
                    return true;
                }
                break;
            case 0x03:
                if(!advance(cursor.read<std::uint16_t>())) {
                    return true;
                }
                break;
            case 0x04:
                if(!advance(cursor.read<std::uint32_t>())) {
                    return true;
                }
                break;
            // DW_CFA_offset_extended
            case 0x05: {
                auto reg = cursor.uleb();
                set(reg, Rule::Offset, factored(static_cast<std::int64_t>(cursor.uleb())));
                break;
            }
            // DW_CFA_restore_extended
            case 0x06: restore(cursor.uleb()); break;
            // DW_CFA_undefined
            case 0x07: set(cursor.uleb(), Rule::Undefined, 0); break;
            // DW_CFA_same_value
            case 0x08: set(cursor.uleb(), Rule::Same, 0); break;
            // DW_CFA_register
            case 0x09: {
                auto reg = cursor.uleb();
                set(reg, Rule::Register, static_cast<std::int64_t>(cursor.uleb()));
                break;
            }
            // DW_CFA_remember_state
            case 0x0A:
                if(depth == std::size(remembered)) {
                    return false;
                }
                remembered[depth++] = row;
                break;
            // DW_CFA_restore_state
            case 0x0B:
                if(depth == 0) {
                    return false;
                }
                row = remembered[--depth];
                break;
            // DW_CFA_def_cfa
            case 0x0C:
                row.cfa_register = cursor.uleb();
                row.cfa_offset = static_cast<std::int64_t>(cursor.uleb());
                row.cfa_expression = nullptr;
                break;
            // DW_CFA_def_cfa_register
            case 0x0D:
                row.cfa_register = cursor.uleb();
                row.cfa_expression = nullptr;
                break;
            // DW_CFA_def_cfa_offset
            case 0x0E: row.cfa_offset = static_cast<std::int64_t>(cursor.uleb()); break;
            // DW_CFA_def_cfa_expression
            case 0x0F: row.cfa_expression = cursor.block(); break;
            // DW_CFA_expression
            case 0x10: {
                auto reg = cursor.uleb();
                set(reg, Rule::Expression, reinterpret_cast<std::intptr_t>(cursor.block()));
                break;
            }
            // DW_CFA_offset_extended_sf
            case 0x11: {
                auto reg = cursor.uleb();
                set(reg, Rule::Offset, factored(cursor.sleb()));
                break;
            }
            // DW_CFA_def_cfa_sf
            case 0x12:
                row.cfa_register = cursor.uleb();
                row.cfa_offset = factored(cursor.sleb());
                row.cfa_expression = nullptr;
                break;
            // DW_CFA_def_cfa_offset_sf
            case 0x13: row.cfa_offset = factored(cursor.sleb()); break;
            // DW_CFA_val_offset
            case 0x14: {
                auto reg = cursor.uleb();
                set(reg, Rule::ValOffset, factored(static_cast<std::int64_t>(cursor.uleb())));
                break;
            }
            // DW_CFA_val_offset_sf
            case 0x15: {
                auto reg = cursor.uleb();
                set(reg, Rule::ValOffset, factored(cursor.sleb()));
                break;
            }
            // DW_CFA_val_expression
            case 0x16: {
                auto reg = cursor.uleb();
                set(reg, Rule::ValExpression, reinterpret_cast<std::intptr_t>(cursor.block()));
                break;
            }
            // DW_CFA_GNU_args_size, of no use to a walk.
            case 0x2E: cursor.uleb(); break;
#ifdef __aarch64__
            // DW_CFA_AARCH64_negate_ra_state: whether the return address is
            // signed, which every one is stripped of anyway.
            case 0x2D: break;
#endif
            default: return false;
        }
    }
    return true;
}

/// Into `result`, the value of the DWARF expression in `block` in the frame
/// of `registers`, `*initial` on its stack first when given, as a register
/// rule's expression has the CFA. False for an operation this does not
/// know, or one the stack has too few or too many values for.
bool evaluate(const std::uint8_t* block,
              const Registers& registers,
              const std::uint64_t* initial,
              std::uint64_t& result) noexcept {
    Cursor cursor{block};
    auto length = cursor.uleb();
    const auto* start = cursor.at;
    const auto* end = start + length;
    std::uint64_t stack[16];
    std::size_t depth = 0;
    auto push = [&](std::uint64_t value) {
        if(depth == std::size(stack)) {
            return false;
        }
        stack[depth++] = value;
        return true;
    };
    auto from_register = [&](std::uint64_t reg, std::int64_t offset) {
        return reg < register_count &&
               push(registers.value[reg] + static_cast<std::uint64_t>(offset));
    };
    auto jump = [&](std::int16_t offset) {
        cursor.at += offset;
        return cursor.at >= start && cursor.at <= end;
    };
    if(initial != nullptr) {
        push(*initial);
    }
    while(cursor.at < end) {
        auto op = cursor.read<std::uint8_t>();
        // DW_OP_lit0 to 31 and DW_OP_breg0 to 31.
        if(op >= 0x30 && op <= 0x4F) {
            if(!push(op - 0x30u)) {
                return false;
            }
            continue;
        }
        if(op >= 0x70 && op <= 0x8F) {
            if(!from_register(op - 0x70u, cursor.sleb())) {
                return false;
            }
            continue;
        }
        bool pushed = true;
        switch(op) {
            // DW_OP_addr
            case 0x03: pushed = push(cursor.read<std::uint64_t>()); break;
            // DW_OP_const1u to DW_OP_const8s
            case 0x08: pushed = push(cursor.read<std::uint8_t>()); break;
            case 0x09: pushed = push(static_cast<std::uint64_t>(cursor.read<std::int8_t>())); break;
            case 0x0A: pushed = push(cursor.read<std::uint16_t>()); break;
            case 0x0B:
                pushed = push(static_cast<std::uint64_t>(cursor.read<std::int16_t>()));
                break;
            case 0x0C: pushed = push(cursor.read<std::uint32_t>()); break;
            case 0x0D:
                pushed = push(static_cast<std::uint64_t>(cursor.read<std::int32_t>()));
                break;
            case 0x0E:
            case 0x0F: pushed = push(cursor.read<std::uint64_t>()); break;
            // DW_OP_constu, DW_OP_consts
            case 0x10: pushed = push(cursor.uleb()); break;
            case 0x11: pushed = push(static_cast<std::uint64_t>(cursor.sleb())); break;
            // DW_OP_bregx
            case 0x92: {
                auto reg = cursor.uleb();
                pushed = from_register(reg, cursor.sleb());
                break;
            }
            // DW_OP_skip
            case 0x2F:
                if(!jump(cursor.read<std::int16_t>())) {
                    return false;
                }
                break;
            // DW_OP_nop
            case 0x96: break;
            default: {
                // The operations on the stack: how many values each takes.
                std::size_t takes = 0;
                switch(op) {
                    case 0x06:
                    case 0x12:
                    case 0x13:
                    case 0x15:
                    case 0x19:
                    case 0x1F:
                    case 0x20:
                    case 0x23:
                    case 0x28:
                    case 0x94: takes = 1; break;
                    case 0x14:
                    case 0x16:
                    case 0x1A:
                    case 0x1B:
                    case 0x1C:
                    case 0x1D:
                    case 0x1E:
                    case 0x21:
                    case 0x22:
                    case 0x24:
                    case 0x25:
                    case 0x26:
                    case 0x27:
                    case 0x29:
                    case 0x2A:
                    case 0x2B:
                    case 0x2C:
                    case 0x2D:
                    case 0x2E: takes = 2; break;
                    case 0x17: takes = 3; break;
                    default: return false;
                }
                if(depth < takes) {
                    return false;
                }
                auto& top = stack[depth - 1];
                auto signed_top = static_cast<std::int64_t>(top);
                switch(op) {
                    // DW_OP_deref
                    case 0x06: top = load(top); break;
                    // DW_OP_deref_size
                    case 0x94: {
                        auto size = cursor.read<std::uint8_t>();
                        if(size == 0 || size > 8) {
                            return false;
                        }
                        std::uint64_t value = 0;
                        std::memcpy(&value, reinterpret_cast<const void*>(top), size);
                        top = value;
                        break;
                    }
                    // DW_OP_dup
                    case 0x12: pushed = push(top); break;
                    // DW_OP_drop
                    case 0x13: --depth; break;
                    // DW_OP_over
                    case 0x14: pushed = push(stack[depth - 2]); break;
                    // DW_OP_pick
                    case 0x15: {
                        auto index = cursor.read<std::uint8_t>();
                        if(index >= depth) {
                            return false;
                        }
                        pushed = push(stack[depth - 1 - index]);
                        break;
                    }
                    // DW_OP_swap
                    case 0x16: std::swap(stack[depth - 1], stack[depth - 2]); break;
                    // DW_OP_rot: the top goes third, the second and third up.
                    case 0x17: {
                        auto value = stack[depth - 1];
                        stack[depth - 1] = stack[depth - 2];
                        stack[depth - 2] = stack[depth - 3];
                        stack[depth - 3] = value;
                        break;
                    }
                    // DW_OP_abs, DW_OP_neg, DW_OP_not, DW_OP_plus_uconst
                    case 0x19:
                        if(signed_top < 0) {
                            top = 0 - top;
                        }
                        break;
                    case 0x1F: top = 0 - top; break;
                    case 0x20: top = ~top; break;
                    case 0x23: top += cursor.uleb(); break;
                    // DW_OP_bra
                    case 0x28: {
                        auto offset = cursor.read<std::int16_t>();
                        --depth;
                        if(signed_top != 0 && !jump(offset)) {
                            return false;
                        }
                        break;
                    }
                    default: {
                        // The binary ones: the second value with the top.
                        auto right = stack[--depth];
                        auto& left = stack[depth - 1];
                        auto signed_left = static_cast<std::int64_t>(left);
                        auto signed_right = static_cast<std::int64_t>(right);
                        switch(op) {
                            case 0x1A: left &= right; break;
                            case 0x1B:
                                if(right == 0) {
                                    return false;
                                }
                                left = static_cast<std::uint64_t>(signed_left / signed_right);
                                break;
                            case 0x1C: left -= right; break;
                            case 0x1D:
                                if(right == 0) {
                                    return false;
                                }
                                left %= right;
                                break;
                            case 0x1E: left *= right; break;
                            case 0x21: left |= right; break;
                            case 0x22: left += right; break;
                            case 0x24: left = right < 64 ? left << right : 0; break;
                            case 0x25: left = right < 64 ? left >> right : 0; break;
                            case 0x26:
                                left = static_cast<std::uint64_t>(signed_left >>
                                                                  (right < 64 ? right : 63));
                                break;
                            case 0x27: left ^= right; break;
                            case 0x29: left = signed_left == signed_right; break;
                            case 0x2A: left = signed_left >= signed_right; break;
                            case 0x2B: left = signed_left > signed_right; break;
                            case 0x2C: left = signed_left <= signed_right; break;
                            case 0x2D: left = signed_left < signed_right; break;
                            default: left = signed_left != signed_right; break;
                        }
                    }
                }
            }
        }
        if(!pushed) {
            return false;
        }
    }
    if(depth == 0) {
        return false;
    }
    result = stack[depth - 1];
    return true;
}

}  // namespace

bool step(Registers& registers, bool& exact, const std::uint8_t* header) noexcept {
    const std::uintptr_t pc = registers.pc - (exact ? 0 : 1);
    Fde fde;
    if(!find_fde(header, pc, fde) || fde.cie.return_register >= register_count) {
        return false;
    }
    Row initial;
    if(!run(Cursor{fde.cie.instructions},
            fde.cie.end,
            fde.cie,
            fde.start,
            std::numeric_limits<std::uintptr_t>::max(),
            initial,
            initial)) {
        return false;
    }
    Row row = initial;
    if(!run(Cursor{fde.instructions}, fde.instructions_end, fde.cie, fde.start, pc, row, initial)) {
        return false;
    }
    const auto& return_rule = row.registers[fde.cie.return_register];
    if(return_rule.rule == Rule::Undefined) {
        return false;
    }

#ifdef __x86_64__
    // rip, the column of the return address, which a PLT stub's CFA
    // expression reads.
    registers.value[16] = registers.pc;
#endif
    std::uint64_t cfa = 0;
    if(row.cfa_expression != nullptr) {
        if(!evaluate(row.cfa_expression, registers, nullptr, cfa)) {
            return false;
        }
    } else if(row.cfa_register < register_count) {
        cfa = registers.value[row.cfa_register] + static_cast<std::uint64_t>(row.cfa_offset);
    } else {
        return false;
    }

    // The caller's stack pointer is the CFA unless a rule says otherwise.
    Registers caller = registers;
    caller.value[sp_register] = cfa;
    for(unsigned reg = 0; reg < register_count; ++reg) {
        const auto& rule = row.registers[reg];
        std::uint64_t value = 0;
        switch(rule.rule) {
            case Rule::Same:
            case Rule::Undefined: continue;
            case Rule::Offset: value = load(cfa + static_cast<std::uint64_t>(rule.value)); break;
            case Rule::ValOffset: value = cfa + static_cast<std::uint64_t>(rule.value); break;
            case Rule::Register:
                if(static_cast<std::uint64_t>(rule.value) >= register_count) {
                    return false;
                }
                value = registers.value[rule.value];
                break;
            case Rule::Expression:
            case Rule::ValExpression:
                if(!evaluate(reinterpret_cast<const std::uint8_t*>(rule.value),
                             registers,
                             &cfa,
                             value)) {
                    return false;
                }
                if(rule.rule == Rule::Expression) {
                    value = load(value);
                }
                break;
        }
        caller.value[reg] = value;
    }
    caller.pc = caller.value[fde.cie.return_register];
#ifdef __aarch64__
    caller.pc = strip_return_address(caller.pc);
#endif
    exact = fde.cie.signal_frame;
    registers = caller;
    return true;
}

}  // namespace kotatsu::sys::dwarf

#endif
