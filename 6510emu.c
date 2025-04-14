/**
 * 6510 CPU Emulator with Disassembly Support
 * 
 * A modern C implementation of a 6510/6502 CPU emulator.
 * The 6510 was used in the Commodore 64 and is compatible with the 6502
 * with some additional I/O port capabilities.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>   // For timing functions
#include <unistd.h> // For usleep
#include <inttypes.h> // For PRIu64 format specifier

// Define CLOCK_MONOTONIC if not already defined
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

// Define attribute macros for unused parameters
#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

// Define custom types for byte, word, and quad word
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint64_t QWORD;

// Constants for timing
#define CPU_FREQUENCY 1000000  // 1MHz - 6510 frequency
#define MICROSECONDS_PER_SECOND 1000000

// Status register flag bit positions with binary literals
typedef enum {
    CarryFlag            = 0b00000001,  // C
    ZeroFlag             = 0b00000010,  // Z
    InterruptDisableFlag = 0b00000100,  // I
    DecimalModeFlag      = 0b00001000,  // D
    BreakCommandFlag     = 0b00010000,  // B
    UnusedFlag           = 0b00100000,  // 1
    OverflowFlag         = 0b01000000,  // V
    NegativeFlag         = 0b10000000   // N
} StatusFlag;

// Speed control modes
typedef enum {
    SPEED_REALTIME,  // Run at accurate 1MHz speed
    SPEED_MAX        // Run as fast as possible
} SpeedMode;

// Addressing modes for disassembly
typedef enum {
    ADDR_IMPLIED,      // Implied - no operand
    ADDR_ACCUMULATOR,  // Accumulator - no operand
    ADDR_IMMEDIATE,    // Immediate - operand is value
    ADDR_ZERO_PAGE,    // Zero Page - operand is address in first 256 bytes
    ADDR_ZERO_PAGE_X,  // Zero Page,X - address is zero page + X
    ADDR_ZERO_PAGE_Y,  // Zero Page,Y - address is zero page + Y
    ADDR_RELATIVE,     // Relative - branch target as signed offset
    ADDR_ABSOLUTE,     // Absolute - full 16-bit address
    ADDR_ABSOLUTE_X,   // Absolute,X - full 16-bit address + X
    ADDR_ABSOLUTE_Y,   // Absolute,Y - full 16-bit address + Y
    ADDR_INDIRECT,     // Indirect - jump target is address stored at operand
    ADDR_INDIRECT_X,   // (Indirect,X) - zero page address + X gives pointer
    ADDR_INDIRECT_Y    // (Indirect),Y - zero page address gives pointer, add Y to final address
} AddressingMode;

// Forward declaration for Instruction structure
typedef struct Instruction Instruction;

// CPU Registers and Memory
typedef struct {
    // CPU Registers
    BYTE Accumulator;     // Accumulator (A) register
    BYTE XRegister;       // X index register
    BYTE YRegister;       // Y index register
    BYTE StackPointer;    // Stack pointer (SP)
    WORD ProgramCounter;  // Program counter (PC)
    BYTE StatusRegister;  // Status register (P) as a whole byte
    
    // 6510 specific I/O port at address 0x0000 and 0x0001
    BYTE PortDirection;   // Data Direction Register (DDR) at 0x0000
    BYTE PortData;        // Data Register at 0x0001
    
    // Memory
    BYTE Memory[65536];   // 64K memory space
    
    // System state
    bool IsRunning;       // Flag indicating if CPU is running
    BYTE CurrentOpcode;   // Current opcode being executed
    
    // Cycle counting
    QWORD Cycles;         // Total cycle count
    
    // Timing variables
    struct timespec LastCycleTime;     // Time of the last cycle update
    QWORD ExpectedCycles;              // Expected cycles at current time
    SpeedMode CurrentSpeed;            // Current speed mode
    
    // Performance metrics
    QWORD InstructionsExecuted;        // Count of instructions executed
    struct timespec EmulationStartTime; // Start time of emulation
    
    // Disassembly options
    bool EnableDisassembly;            // Whether to show disassembly
    bool ShowRegisters;                // Whether to show register values with disassembly
} CPU;

// Structure for opcode information used in disassembly
typedef struct {
    BYTE opcode;              // The opcode byte
    char mnemonic[4];         // Instruction name (3 chars + null)
    AddressingMode mode;      // Addressing mode for this opcode
    BYTE size;                // Size in bytes (1, 2, or 3)
} OpcodeInfo;

// Disassembly context structure for formatting
typedef struct {
    WORD address;            // Address of instruction
    BYTE opcode;             // Opcode byte
    BYTE operand1;           // First operand byte (if any)
    BYTE operand2;           // Second operand byte (if any)
    const char* mnemonic;    // Instruction mnemonic
    char buffer[64];         // Output buffer
    size_t buffer_size;      // Size of output buffer
} DisassemblyContext;

// Helper functions for status flag operations
static inline bool get_flag(CPU *cpu, StatusFlag flag) {
    return (cpu->StatusRegister & flag) != 0;
}

static inline void set_flag(CPU *cpu, StatusFlag flag, bool value) {
    if (value) {
        cpu->StatusRegister |= flag;
    } else {
        cpu->StatusRegister &= ~flag;
    }
}

// Function pointer type for addressing mode functions
typedef WORD (*AddressingModeFn)(CPU*);

// Instruction function type that takes address as parameter
typedef void (*InstructionFn)(CPU*, WORD);

// Instruction definition
typedef struct Instruction {
    char mnemonic[4];         // Instruction name (3 chars + null)
    InstructionFn operation;  // Function pointer to operation
    AddressingModeFn addr_mode; // Function to compute operand address
    BYTE cycles;              // Base cycle count
} Instruction;

// Function prototypes for CPU operations
void cpu_init(CPU *cpu);
void cpu_reset(CPU *cpu);
void cpu_step(CPU *cpu);
BYTE cpu_read(CPU *cpu, WORD address);
void cpu_write(CPU *cpu, WORD address, BYTE value);
void cpu_push(CPU *cpu, BYTE value);
BYTE cpu_pull(CPU *cpu);
void cpu_push16(CPU *cpu, WORD value);
WORD cpu_pull16(CPU *cpu);
void cpu_sync_timing(CPU *cpu);
void cpu_set_speed_mode(CPU *cpu, SpeedMode mode);
double cpu_get_mips(CPU *cpu);
void cpu_enable_disassembly(CPU *cpu, bool enable, bool show_registers);
int cpu_disassemble_instruction(CPU *cpu, WORD address, char *buffer, size_t buffer_size);
const OpcodeInfo* get_opcode_info(BYTE opcode);

// Addressing mode functions
WORD addr_implied(CPU *cpu UNUSED);
WORD addr_accumulator(CPU *cpu UNUSED);
WORD addr_immediate(CPU *cpu);
WORD addr_zero_page(CPU *cpu);
WORD addr_zero_page_x(CPU *cpu);
WORD addr_zero_page_y(CPU *cpu);
WORD addr_relative(CPU *cpu);
WORD addr_absolute(CPU *cpu);
WORD addr_absolute_x(CPU *cpu);
WORD addr_absolute_y(CPU *cpu);
WORD addr_indirect(CPU *cpu);
WORD addr_indirect_x(CPU *cpu);
WORD addr_indirect_y(CPU *cpu);

// Instruction handlers
void adc(CPU *cpu, WORD addr);
void and(CPU *cpu, WORD addr);
void asl(CPU *cpu, WORD addr);
void bcc(CPU *cpu, WORD addr UNUSED);
void bcs(CPU *cpu, WORD addr UNUSED);
void beq(CPU *cpu, WORD addr UNUSED);
void bit(CPU *cpu, WORD addr);
void bmi(CPU *cpu, WORD addr UNUSED);
void bne(CPU *cpu, WORD addr UNUSED);
void bpl(CPU *cpu, WORD addr UNUSED);
void bbrk(CPU *cpu, WORD addr UNUSED);
void bvc(CPU *cpu, WORD addr UNUSED);
void bvs(CPU *cpu, WORD addr UNUSED);
void clc(CPU *cpu, WORD addr UNUSED);
void cld(CPU *cpu, WORD addr UNUSED);
void cli(CPU *cpu, WORD addr UNUSED);
void clv(CPU *cpu, WORD addr UNUSED);
void cmp(CPU *cpu, WORD addr);
void cpx(CPU *cpu, WORD addr);
void cpy(CPU *cpu, WORD addr);
void dec(CPU *cpu, WORD addr);
void dex(CPU *cpu, WORD addr UNUSED);
void dey(CPU *cpu, WORD addr UNUSED);
void eor(CPU *cpu, WORD addr);
void inc(CPU *cpu, WORD addr);
void inx(CPU *cpu, WORD addr UNUSED);
void iny(CPU *cpu, WORD addr UNUSED);
void jmp(CPU *cpu, WORD addr);
void jsr(CPU *cpu, WORD addr);
void lda(CPU *cpu, WORD addr);
void ldx(CPU *cpu, WORD addr);
void ldy(CPU *cpu, WORD addr);
void lsr(CPU *cpu, WORD addr);
void nop(CPU *cpu UNUSED, WORD addr UNUSED);
void ora(CPU *cpu, WORD addr);
void pha(CPU *cpu, WORD addr UNUSED);
void php(CPU *cpu, WORD addr UNUSED);
void pla(CPU *cpu, WORD addr UNUSED);
void plp(CPU *cpu, WORD addr UNUSED);
void rol(CPU *cpu, WORD addr);
void ror(CPU *cpu, WORD addr);
void rti(CPU *cpu, WORD addr UNUSED);
void rts(CPU *cpu, WORD addr UNUSED);
void sbc(CPU *cpu, WORD addr);
void sec(CPU *cpu, WORD addr UNUSED);
void sed(CPU *cpu, WORD addr UNUSED);
void sei(CPU *cpu, WORD addr UNUSED);
void sta(CPU *cpu, WORD addr);
void stx(CPU *cpu, WORD addr);
void sty(CPU *cpu, WORD addr);
void tax(CPU *cpu, WORD addr UNUSED);
void tay(CPU *cpu, WORD addr UNUSED);
void tsx(CPU *cpu, WORD addr UNUSED);
void txa(CPU *cpu, WORD addr UNUSED);
void txs(CPU *cpu, WORD addr UNUSED);
void tya(CPU *cpu, WORD addr UNUSED);
void unknown_opcode(CPU *cpu, WORD addr UNUSED);

// Branch helper function
void branch_if(CPU *cpu, bool condition);

// Disassembly formatting functions
void disassemble_implied(DisassemblyContext *ctx);
void disassemble_accumulator(DisassemblyContext *ctx);
void disassemble_immediate(DisassemblyContext *ctx);
void disassemble_zero_page(DisassemblyContext *ctx);
void disassemble_zero_page_x(DisassemblyContext *ctx);
void disassemble_zero_page_y(DisassemblyContext *ctx);
void disassemble_relative(DisassemblyContext *ctx);
void disassemble_absolute(DisassemblyContext *ctx);
void disassemble_absolute_x(DisassemblyContext *ctx);
void disassemble_absolute_y(DisassemblyContext *ctx);
void disassemble_indirect(DisassemblyContext *ctx);
void disassemble_indirect_x(DisassemblyContext *ctx);
void disassemble_indirect_y(DisassemblyContext *ctx);
void disassemble_unknown(DisassemblyContext *ctx);

// Opcode information table for disassembly (partial list for brevity)
static const OpcodeInfo opcode_table[] = {
    // Format: {opcode, mnemonic, addressing_mode, size_in_bytes}
    {0x00, "BRK", ADDR_IMPLIED, 1},
    {0x01, "ORA", ADDR_INDIRECT_X, 2},
    {0x05, "ORA", ADDR_ZERO_PAGE, 2},
    {0x06, "ASL", ADDR_ZERO_PAGE, 2},
    {0x08, "PHP", ADDR_IMPLIED, 1},
    {0x09, "ORA", ADDR_IMMEDIATE, 2},
    {0x0A, "ASL", ADDR_ACCUMULATOR, 1},
    {0x0D, "ORA", ADDR_ABSOLUTE, 3},
    {0x0E, "ASL", ADDR_ABSOLUTE, 3},
    
    {0x10, "BPL", ADDR_RELATIVE, 2},
    {0x11, "ORA", ADDR_INDIRECT_Y, 2},
    {0x15, "ORA", ADDR_ZERO_PAGE_X, 2},
    {0x16, "ASL", ADDR_ZERO_PAGE_X, 2},
    {0x18, "CLC", ADDR_IMPLIED, 1},
    {0x19, "ORA", ADDR_ABSOLUTE_Y, 3},
    {0x1D, "ORA", ADDR_ABSOLUTE_X, 3},
    {0x1E, "ASL", ADDR_ABSOLUTE_X, 3},
    
    {0x20, "JSR", ADDR_ABSOLUTE, 3},
    {0x21, "AND", ADDR_INDIRECT_X, 2},
    {0x24, "BIT", ADDR_ZERO_PAGE, 2},
    {0x25, "AND", ADDR_ZERO_PAGE, 2},
    {0x26, "ROL", ADDR_ZERO_PAGE, 2},
    {0x28, "PLP", ADDR_IMPLIED, 1},
    {0x29, "AND", ADDR_IMMEDIATE, 2},
    {0x2A, "ROL", ADDR_ACCUMULATOR, 1},
    {0x2C, "BIT", ADDR_ABSOLUTE, 3},
    {0x2D, "AND", ADDR_ABSOLUTE, 3},
    {0x2E, "ROL", ADDR_ABSOLUTE, 3},
    
    {0x30, "BMI", ADDR_RELATIVE, 2},
    {0x31, "AND", ADDR_INDIRECT_Y, 2},
    {0x35, "AND", ADDR_ZERO_PAGE_X, 2},
    {0x36, "ROL", ADDR_ZERO_PAGE_X, 2},
    {0x38, "SEC", ADDR_IMPLIED, 1},
    {0x39, "AND", ADDR_ABSOLUTE_Y, 3},
    {0x3D, "AND", ADDR_ABSOLUTE_X, 3},
    {0x3E, "ROL", ADDR_ABSOLUTE_X, 3},
    
    {0x40, "RTI", ADDR_IMPLIED, 1},
    {0x41, "EOR", ADDR_INDIRECT_X, 2},
    {0x45, "EOR", ADDR_ZERO_PAGE, 2},
    {0x46, "LSR", ADDR_ZERO_PAGE, 2},
    {0x48, "PHA", ADDR_IMPLIED, 1},
    {0x49, "EOR", ADDR_IMMEDIATE, 2},
    {0x4A, "LSR", ADDR_ACCUMULATOR, 1},
    {0x4C, "JMP", ADDR_ABSOLUTE, 3},
    {0x4D, "EOR", ADDR_ABSOLUTE, 3},
    {0x4E, "LSR", ADDR_ABSOLUTE, 3},
    
    {0x50, "BVC", ADDR_RELATIVE, 2},
    {0x51, "EOR", ADDR_INDIRECT_Y, 2},
    {0x55, "EOR", ADDR_ZERO_PAGE_X, 2},
    {0x56, "LSR", ADDR_ZERO_PAGE_X, 2},
    {0x58, "CLI", ADDR_IMPLIED, 1},
    {0x59, "EOR", ADDR_ABSOLUTE_Y, 3},
    {0x5D, "EOR", ADDR_ABSOLUTE_X, 3},
    {0x5E, "LSR", ADDR_ABSOLUTE_X, 3},
    
    {0x60, "RTS", ADDR_IMPLIED, 1},
    {0x61, "ADC", ADDR_INDIRECT_X, 2},
    {0x65, "ADC", ADDR_ZERO_PAGE, 2},
    {0x66, "ROR", ADDR_ZERO_PAGE, 2},
    {0x68, "PLA", ADDR_IMPLIED, 1},
    {0x69, "ADC", ADDR_IMMEDIATE, 2},
    {0x6A, "ROR", ADDR_ACCUMULATOR, 1},
    {0x6C, "JMP", ADDR_INDIRECT, 3},
    {0x6D, "ADC", ADDR_ABSOLUTE, 3},
    {0x6E, "ROR", ADDR_ABSOLUTE, 3},
    
    {0x70, "BVS", ADDR_RELATIVE, 2},
    {0x71, "ADC", ADDR_INDIRECT_Y, 2},
    {0x75, "ADC", ADDR_ZERO_PAGE_X, 2},
    {0x76, "ROR", ADDR_ZERO_PAGE_X, 2},
    {0x78, "SEI", ADDR_IMPLIED, 1},
    {0x79, "ADC", ADDR_ABSOLUTE_Y, 3},
    {0x7D, "ADC", ADDR_ABSOLUTE_X, 3},
    {0x7E, "ROR", ADDR_ABSOLUTE_X, 3},
    
    {0x81, "STA", ADDR_INDIRECT_X, 2},
    {0x84, "STY", ADDR_ZERO_PAGE, 2},
    {0x85, "STA", ADDR_ZERO_PAGE, 2},
    {0x86, "STX", ADDR_ZERO_PAGE, 2},
    {0x88, "DEY", ADDR_IMPLIED, 1},
    {0x8A, "TXA", ADDR_IMPLIED, 1},
    {0x8C, "STY", ADDR_ABSOLUTE, 3},
    {0x8D, "STA", ADDR_ABSOLUTE, 3},
    {0x8E, "STX", ADDR_ABSOLUTE, 3},
    
    {0x90, "BCC", ADDR_RELATIVE, 2},
    {0x91, "STA", ADDR_INDIRECT_Y, 2},
    {0x94, "STY", ADDR_ZERO_PAGE_X, 2},
    {0x95, "STA", ADDR_ZERO_PAGE_X, 2},
    {0x96, "STX", ADDR_ZERO_PAGE_Y, 2},
    {0x98, "TYA", ADDR_IMPLIED, 1},
    {0x99, "STA", ADDR_ABSOLUTE_Y, 3},
    {0x9A, "TXS", ADDR_IMPLIED, 1},
    {0x9D, "STA", ADDR_ABSOLUTE_X, 3},
    
    {0xA0, "LDY", ADDR_IMMEDIATE, 2},
    {0xA1, "LDA", ADDR_INDIRECT_X, 2},
    {0xA2, "LDX", ADDR_IMMEDIATE, 2},
    {0xA4, "LDY", ADDR_ZERO_PAGE, 2},
    {0xA5, "LDA", ADDR_ZERO_PAGE, 2},
    {0xA6, "LDX", ADDR_ZERO_PAGE, 2},
    {0xA8, "TAY", ADDR_IMPLIED, 1},
    {0xA9, "LDA", ADDR_IMMEDIATE, 2},
    {0xAA, "TAX", ADDR_IMPLIED, 1},
    {0xAC, "LDY", ADDR_ABSOLUTE, 3},
    {0xAD, "LDA", ADDR_ABSOLUTE, 3},
    {0xAE, "LDX", ADDR_ABSOLUTE, 3},
    
    {0xB0, "BCS", ADDR_RELATIVE, 2},
    {0xB1, "LDA", ADDR_INDIRECT_Y, 2},
    {0xB4, "LDY", ADDR_ZERO_PAGE_X, 2},
    {0xB5, "LDA", ADDR_ZERO_PAGE_X, 2},
    {0xB6, "LDX", ADDR_ZERO_PAGE_Y, 2},
    {0xB8, "CLV", ADDR_IMPLIED, 1},
    {0xB9, "LDA", ADDR_ABSOLUTE_Y, 3},
    {0xBA, "TSX", ADDR_IMPLIED, 1},
    {0xBC, "LDY", ADDR_ABSOLUTE_X, 3},
    {0xBD, "LDA", ADDR_ABSOLUTE_X, 3},
    {0xBE, "LDX", ADDR_ABSOLUTE_Y, 3},
    
    {0xC0, "CPY", ADDR_IMMEDIATE, 2},
    {0xC1, "CMP", ADDR_INDIRECT_X, 2},
    {0xC4, "CPY", ADDR_ZERO_PAGE, 2},
    {0xC5, "CMP", ADDR_ZERO_PAGE, 2},
    {0xC6, "DEC", ADDR_ZERO_PAGE, 2},
    {0xC8, "INY", ADDR_IMPLIED, 1},
    {0xC9, "CMP", ADDR_IMMEDIATE, 2},
    {0xCA, "DEX", ADDR_IMPLIED, 1},
    {0xCC, "CPY", ADDR_ABSOLUTE, 3},
    {0xCD, "CMP", ADDR_ABSOLUTE, 3},
    {0xCE, "DEC", ADDR_ABSOLUTE, 3},
    
    {0xD0, "BNE", ADDR_RELATIVE, 2},
    {0xD1, "CMP", ADDR_INDIRECT_Y, 2},
    {0xD5, "CMP", ADDR_ZERO_PAGE_X, 2},
    {0xD6, "DEC", ADDR_ZERO_PAGE_X, 2},
    {0xD8, "CLD", ADDR_IMPLIED, 1},
    {0xD9, "CMP", ADDR_ABSOLUTE_Y, 3},
    {0xDD, "CMP", ADDR_ABSOLUTE_X, 3},
    {0xDE, "DEC", ADDR_ABSOLUTE_X, 3},
    
    {0xE0, "CPX", ADDR_IMMEDIATE, 2},
    {0xE1, "SBC", ADDR_INDIRECT_X, 2},
    {0xE4, "CPX", ADDR_ZERO_PAGE, 2},
    {0xE5, "SBC", ADDR_ZERO_PAGE, 2},
    {0xE6, "INC", ADDR_ZERO_PAGE, 2},
    {0xE8, "INX", ADDR_IMPLIED, 1},
    {0xE9, "SBC", ADDR_IMMEDIATE, 2},
    {0xEA, "NOP", ADDR_IMPLIED, 1},
    {0xEC, "CPX", ADDR_ABSOLUTE, 3},
    {0xED, "SBC", ADDR_ABSOLUTE, 3},
    {0xEE, "INC", ADDR_ABSOLUTE, 3},
    
    {0xF0, "BEQ", ADDR_RELATIVE, 2},
    {0xF1, "SBC", ADDR_INDIRECT_Y, 2},
    {0xF5, "SBC", ADDR_ZERO_PAGE_X, 2},
    {0xF6, "INC", ADDR_ZERO_PAGE_X, 2},
    {0xF8, "SED", ADDR_IMPLIED, 1},
    {0xF9, "SBC", ADDR_ABSOLUTE_Y, 3},
    {0xFD, "SBC", ADDR_ABSOLUTE_X, 3},
    {0xFE, "INC", ADDR_ABSOLUTE_X, 3}
};

// Complete instruction table organized by instruction mnemonic
static const Instruction instruction_table[256] = {
    // ADC - Add with Carry
    [0x69] = {"ADC", adc, addr_immediate, 2},
    [0x65] = {"ADC", adc, addr_zero_page, 3},
    [0x75] = {"ADC", adc, addr_zero_page_x, 4},
    [0x6D] = {"ADC", adc, addr_absolute, 4},
    [0x7D] = {"ADC", adc, addr_absolute_x, 4},
    [0x79] = {"ADC", adc, addr_absolute_y, 4},
    [0x61] = {"ADC", adc, addr_indirect_x, 6},
    [0x71] = {"ADC", adc, addr_indirect_y, 5},
    
    // AND - Logical AND
    [0x29] = {"AND", and, addr_immediate, 2},
    [0x25] = {"AND", and, addr_zero_page, 3},
    [0x35] = {"AND", and, addr_zero_page_x, 4},
    [0x2D] = {"AND", and, addr_absolute, 4},
    [0x3D] = {"AND", and, addr_absolute_x, 4},
    [0x39] = {"AND", and, addr_absolute_y, 4},
    [0x21] = {"AND", and, addr_indirect_x, 6},
    [0x31] = {"AND", and, addr_indirect_y, 5},
    
    // ASL - Arithmetic Shift Left
    [0x0A] = {"ASL", asl, addr_accumulator, 2},
    [0x06] = {"ASL", asl, addr_zero_page, 5},
    [0x16] = {"ASL", asl, addr_zero_page_x, 6},
    [0x0E] = {"ASL", asl, addr_absolute, 6},
    [0x1E] = {"ASL", asl, addr_absolute_x, 7},
    
    // Branch Instructions
    [0x90] = {"BCC", bcc, addr_relative, 2},  // Branch on Carry Clear
    [0xB0] = {"BCS", bcs, addr_relative, 2},  // Branch on Carry Set
    [0xF0] = {"BEQ", beq, addr_relative, 2},  // Branch on Equal (Zero Set)
    [0x30] = {"BMI", bmi, addr_relative, 2},  // Branch on Minus (Negative Set)
    [0xD0] = {"BNE", bne, addr_relative, 2},  // Branch on Not Equal (Zero Clear)
    [0x10] = {"BPL", bpl, addr_relative, 2},  // Branch on Plus (Negative Clear)
    [0x50] = {"BVC", bvc, addr_relative, 2},  // Branch on Overflow Clear
    [0x70] = {"BVS", bvs, addr_relative, 2},  // Branch on Overflow Set
    
    // BIT - Test Bits
    [0x24] = {"BIT", bit, addr_zero_page, 3},
    [0x2C] = {"BIT", bit, addr_absolute, 4},
    
    // BRK - Force Break
    [0x00] = {"BRK", bbrk, addr_implied, 7},
    
    // Clear Flag Instructions
    [0x18] = {"CLC", clc, addr_implied, 2},  // Clear Carry
    [0xD8] = {"CLD", cld, addr_implied, 2},  // Clear Decimal Mode
    [0x58] = {"CLI", cli, addr_implied, 2},  // Clear Interrupt Disable
    [0xB8] = {"CLV", clv, addr_implied, 2},  // Clear Overflow
    
    // CMP - Compare Accumulator
    [0xC9] = {"CMP", cmp, addr_immediate, 2},
    [0xC5] = {"CMP", cmp, addr_zero_page, 3},
    [0xD5] = {"CMP", cmp, addr_zero_page_x, 4},
    [0xCD] = {"CMP", cmp, addr_absolute, 4},
    [0xDD] = {"CMP", cmp, addr_absolute_x, 4},
    [0xD9] = {"CMP", cmp, addr_absolute_y, 4},
    [0xC1] = {"CMP", cmp, addr_indirect_x, 6},
    [0xD1] = {"CMP", cmp, addr_indirect_y, 5},
    
    // CPX - Compare X Register
    [0xE0] = {"CPX", cpx, addr_immediate, 2},
    [0xE4] = {"CPX", cpx, addr_zero_page, 3},
    [0xEC] = {"CPX", cpx, addr_absolute, 4},
    
    // CPY - Compare Y Register
    [0xC0] = {"CPY", cpy, addr_immediate, 2},
    [0xC4] = {"CPY", cpy, addr_zero_page, 3},
    [0xCC] = {"CPY", cpy, addr_absolute, 4},
    
    // DEC - Decrement Memory
    [0xC6] = {"DEC", dec, addr_zero_page, 5},
    [0xD6] = {"DEC", dec, addr_zero_page_x, 6},
    [0xCE] = {"DEC", dec, addr_absolute, 6},
    [0xDE] = {"DEC", dec, addr_absolute_x, 7},
    
    // DEX, DEY - Decrement Registers
    [0xCA] = {"DEX", dex, addr_implied, 2},
    [0x88] = {"DEY", dey, addr_implied, 2},
    
    // EOR - Exclusive OR
    [0x49] = {"EOR", eor, addr_immediate, 2},
    [0x45] = {"EOR", eor, addr_zero_page, 3},
    [0x55] = {"EOR", eor, addr_zero_page_x, 4},
    [0x4D] = {"EOR", eor, addr_absolute, 4},
    [0x5D] = {"EOR", eor, addr_absolute_x, 4},
    [0x59] = {"EOR", eor, addr_absolute_y, 4},
    [0x41] = {"EOR", eor, addr_indirect_x, 6},
    [0x51] = {"EOR", eor, addr_indirect_y, 5},
    
    // INC - Increment Memory
    [0xE6] = {"INC", inc, addr_zero_page, 5},
    [0xF6] = {"INC", inc, addr_zero_page_x, 6},
    [0xEE] = {"INC", inc, addr_absolute, 6},
    [0xFE] = {"INC", inc, addr_absolute_x, 7},
    
    // INX, INY - Increment Registers
    [0xE8] = {"INX", inx, addr_implied, 2},
    [0xC8] = {"INY", iny, addr_implied, 2},
    
    // JMP - Jump
    [0x4C] = {"JMP", jmp, addr_absolute, 3},
    [0x6C] = {"JMP", jmp, addr_indirect, 5},
    
    // JSR - Jump to Subroutine
    [0x20] = {"JSR", jsr, addr_absolute, 6},
    
    // LDA - Load Accumulator
    [0xA9] = {"LDA", lda, addr_immediate, 2},
    [0xA5] = {"LDA", lda, addr_zero_page, 3},
    [0xB5] = {"LDA", lda, addr_zero_page_x, 4},
    [0xAD] = {"LDA", lda, addr_absolute, 4},
    [0xBD] = {"LDA", lda, addr_absolute_x, 4},
    [0xB9] = {"LDA", lda, addr_absolute_y, 4},
    [0xA1] = {"LDA", lda, addr_indirect_x, 6},
    [0xB1] = {"LDA", lda, addr_indirect_y, 5},
    
    // LDX - Load X Register
    [0xA2] = {"LDX", ldx, addr_immediate, 2},
    [0xA6] = {"LDX", ldx, addr_zero_page, 3},
    [0xB6] = {"LDX", ldx, addr_zero_page_y, 4},
    [0xAE] = {"LDX", ldx, addr_absolute, 4},
    [0xBE] = {"LDX", ldx, addr_absolute_y, 4},
    
    // LDY - Load Y Register
    [0xA0] = {"LDY", ldy, addr_immediate, 2},
    [0xA4] = {"LDY", ldy, addr_zero_page, 3},
    [0xB4] = {"LDY", ldy, addr_zero_page_x, 4},
    [0xAC] = {"LDY", ldy, addr_absolute, 4},
    [0xBC] = {"LDY", ldy, addr_absolute_x, 4},
    
    // LSR - Logical Shift Right
    [0x4A] = {"LSR", lsr, addr_accumulator, 2},
    [0x46] = {"LSR", lsr, addr_zero_page, 5},
    [0x56] = {"LSR", lsr, addr_zero_page_x, 6},
    [0x4E] = {"LSR", lsr, addr_absolute, 6},
    [0x5E] = {"LSR", lsr, addr_absolute_x, 7},
    
    // NOP - No Operation
    [0xEA] = {"NOP", nop, addr_implied, 2},  // Official NOP
    
    // ORA - Logical OR
    [0x09] = {"ORA", ora, addr_immediate, 2},
    [0x05] = {"ORA", ora, addr_zero_page, 3},
    [0x15] = {"ORA", ora, addr_zero_page_x, 4},
    [0x0D] = {"ORA", ora, addr_absolute, 4},
    [0x1D] = {"ORA", ora, addr_absolute_x, 4},
    [0x19] = {"ORA", ora, addr_absolute_y, 4},
    [0x01] = {"ORA", ora, addr_indirect_x, 6},
    [0x11] = {"ORA", ora, addr_indirect_y, 5},
    
    // Push/Pull Instructions
    [0x48] = {"PHA", pha, addr_implied, 3},  // Push Accumulator
    [0x08] = {"PHP", php, addr_implied, 3},  // Push Processor Status
    [0x68] = {"PLA", pla, addr_implied, 4},  // Pull Accumulator
    [0x28] = {"PLP", plp, addr_implied, 4},  // Pull Processor Status
    
    // ROL - Rotate Left
    [0x2A] = {"ROL", rol, addr_accumulator, 2},
    [0x26] = {"ROL", rol, addr_zero_page, 5},
    [0x36] = {"ROL", rol, addr_zero_page_x, 6},
    [0x2E] = {"ROL", rol, addr_absolute, 6},
    [0x3E] = {"ROL", rol, addr_absolute_x, 7},
    
    // ROR - Rotate Right
    [0x6A] = {"ROR", ror, addr_accumulator, 2},
    [0x66] = {"ROR", ror, addr_zero_page, 5},
    [0x76] = {"ROR", ror, addr_zero_page_x, 6},
    [0x6E] = {"ROR", ror, addr_absolute, 6},
    [0x7E] = {"ROR", ror, addr_absolute_x, 7},
    
    // RTI - Return from Interrupt
    [0x40] = {"RTI", rti, addr_implied, 6},
    
    // RTS - Return from Subroutine
    [0x60] = {"RTS", rts, addr_implied, 6},
    
    // SBC - Subtract with Carry
    [0xE9] = {"SBC", sbc, addr_immediate, 2},
    [0xE5] = {"SBC", sbc, addr_zero_page, 3},
    [0xF5] = {"SBC", sbc, addr_zero_page_x, 4},
    [0xED] = {"SBC", sbc, addr_absolute, 4},
    [0xFD] = {"SBC", sbc, addr_absolute_x, 4},
    [0xF9] = {"SBC", sbc, addr_absolute_y, 4},
    [0xE1] = {"SBC", sbc, addr_indirect_x, 6},
    [0xF1] = {"SBC", sbc, addr_indirect_y, 5},
    
    // Set Flag Instructions
    [0x38] = {"SEC", sec, addr_implied, 2},  // Set Carry
    [0xF8] = {"SED", sed, addr_implied, 2},  // Set Decimal Mode
    [0x78] = {"SEI", sei, addr_implied, 2},  // Set Interrupt Disable
    
    // STA - Store Accumulator
    [0x85] = {"STA", sta, addr_zero_page, 3},
    [0x95] = {"STA", sta, addr_zero_page_x, 4},
    [0x8D] = {"STA", sta, addr_absolute, 4},
    [0x9D] = {"STA", sta, addr_absolute_x, 5},
    [0x99] = {"STA", sta, addr_absolute_y, 5},
    [0x81] = {"STA", sta, addr_indirect_x, 6},
    [0x91] = {"STA", sta, addr_indirect_y, 6},
    
    // STX - Store X Register
    [0x86] = {"STX", stx, addr_zero_page, 3},
    [0x96] = {"STX", stx, addr_zero_page_y, 4},
    [0x8E] = {"STX", stx, addr_absolute, 4},
    
    // STY - Store Y Register
    [0x84] = {"STY", sty, addr_zero_page, 3},
    [0x94] = {"STY", sty, addr_zero_page_x, 4},
    [0x8C] = {"STY", sty, addr_absolute, 4},
    
    // Transfer Instructions
    [0xAA] = {"TAX", tax, addr_implied, 2},  // Transfer A to X
    [0xA8] = {"TAY", tay, addr_implied, 2},  // Transfer A to Y
    [0xBA] = {"TSX", tsx, addr_implied, 2},  // Transfer SP to X
    [0x8A] = {"TXA", txa, addr_implied, 2},  // Transfer X to A
    [0x9A] = {"TXS", txs, addr_implied, 2},  // Transfer X to SP
    [0x98] = {"TYA", tya, addr_implied, 2}   // Transfer Y to A
};
// Array of disassembly formatting functions for each addressing mode
typedef void (*DisassemblyFn)(DisassemblyContext*);

static const DisassemblyFn disassembly_functions[] = {
    [ADDR_IMPLIED]     = disassemble_implied,
    [ADDR_ACCUMULATOR] = disassemble_accumulator,
    [ADDR_IMMEDIATE]   = disassemble_immediate,
    [ADDR_ZERO_PAGE]   = disassemble_zero_page,
    [ADDR_ZERO_PAGE_X] = disassemble_zero_page_x,
    [ADDR_ZERO_PAGE_Y] = disassemble_zero_page_y,
    [ADDR_RELATIVE]    = disassemble_relative,
    [ADDR_ABSOLUTE]    = disassemble_absolute,
    [ADDR_ABSOLUTE_X]  = disassemble_absolute_x,
    [ADDR_ABSOLUTE_Y]  = disassemble_absolute_y,
    [ADDR_INDIRECT]    = disassemble_indirect,
    [ADDR_INDIRECT_X]  = disassemble_indirect_x,
    [ADDR_INDIRECT_Y]  = disassemble_indirect_y
};

// Disassembly format functions for each addressing mode
void disassemble_implied(DisassemblyContext *ctx) {
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X        %s", 
             ctx->address, ctx->opcode, ctx->mnemonic);
}

void disassemble_accumulator(DisassemblyContext *ctx) {
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X        %s A", 
             ctx->address, ctx->opcode, ctx->mnemonic);
}

void disassemble_immediate(DisassemblyContext *ctx) {
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s #$%02X", 
             ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, ctx->operand1);
}

void disassemble_zero_page(DisassemblyContext *ctx) {
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s $%02X", 
             ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, ctx->operand1);
}

void disassemble_zero_page_x(DisassemblyContext *ctx) {
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s $%02X,X", 
             ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, ctx->operand1);
}

void disassemble_zero_page_y(DisassemblyContext *ctx) {
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s $%02X,Y", 
             ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, ctx->operand1);
}

void disassemble_relative(DisassemblyContext *ctx) {
    // Calculate target address for branch
    WORD target = ctx->address + 2 + (int8_t)ctx->operand1;
    snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s $%04X", 
             ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, target);
}

void disassemble_absolute(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X %02X  %s $%04X", 
            ctx->address, ctx->opcode, ctx->operand1, ctx->operand2, ctx->mnemonic, 
            (ctx->operand2 << 8) | ctx->operand1);
}

void disassemble_absolute_x(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X %02X  %s $%04X,X", 
            ctx->address, ctx->opcode, ctx->operand1, ctx->operand2, ctx->mnemonic, 
            (ctx->operand2 << 8) | ctx->operand1);
}

void disassemble_absolute_y(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X %02X  %s $%04X,Y", 
            ctx->address, ctx->opcode, ctx->operand1, ctx->operand2, ctx->mnemonic, 
            (ctx->operand2 << 8) | ctx->operand1);
}

void disassemble_indirect(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X %02X  %s ($%04X)", 
            ctx->address, ctx->opcode, ctx->operand1, ctx->operand2, ctx->mnemonic, 
            (ctx->operand2 << 8) | ctx->operand1);
}

void disassemble_indirect_x(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s ($%02X,X)", 
            ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, ctx->operand1);
}

void disassemble_indirect_y(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X %02X     %s ($%02X),Y", 
            ctx->address, ctx->opcode, ctx->operand1, ctx->mnemonic, ctx->operand1);
}

void disassemble_unknown(DisassemblyContext *ctx) {
   snprintf(ctx->buffer, ctx->buffer_size, "%04X: %02X        ???", ctx->address, ctx->opcode);
}

// Get information about an opcode
const OpcodeInfo* get_opcode_info(BYTE opcode) {
   // For now, we'll use a sequential search
   // In a full implementation, you could use a pre-built lookup table
   for (size_t i = 0; i < sizeof(opcode_table) / sizeof(opcode_table[0]); i++) {
       if (opcode_table[i].opcode == opcode) {
           return &opcode_table[i];
       }
   }
   
   // If opcode not found, it's an unknown/illegal opcode
   static const OpcodeInfo unknown = {0, "???", ADDR_IMPLIED, 1};
   return &unknown;
}

// Disassemble a single instruction at the given address
// Returns the size of the disassembled instruction in bytes
int cpu_disassemble_instruction(CPU *cpu, WORD address, char *buffer, size_t buffer_size) {
   BYTE opcode = cpu_read(cpu, address);
   const OpcodeInfo *info = get_opcode_info(opcode);
   
   // Operand bytes
   BYTE operand1 = (info->size > 1) ? cpu_read(cpu, address + 1) : 0;
   BYTE operand2 = (info->size > 2) ? cpu_read(cpu, address + 2) : 0;
   
   // Set up disassembly context
   DisassemblyContext ctx = {
       .address = address,
       .opcode = opcode,
       .operand1 = operand1,
       .operand2 = operand2,
       .mnemonic = info->mnemonic,
       .buffer = {0},
       .buffer_size = buffer_size
   };
   
   // Call the appropriate disassembly function for this addressing mode
   if (info->mode < sizeof(disassembly_functions) / sizeof(disassembly_functions[0]) && 
       disassembly_functions[info->mode] != NULL) {
       disassembly_functions[info->mode](&ctx);
   } else {
       disassemble_unknown(&ctx);
   }
   
   // Copy the result to the output buffer
   strncpy(buffer, ctx.buffer, buffer_size);
   buffer[buffer_size - 1] = '\0';  // Ensure null termination
   
   return info->size;
}

// Enable or disable disassembly output
void cpu_enable_disassembly(CPU *cpu, bool enable, bool show_registers) {
   cpu->EnableDisassembly = enable;
   cpu->ShowRegisters = show_registers;
}

// Addressing mode functions for instruction execution
WORD addr_implied(CPU *cpu UNUSED) {
   return 0; // No memory address needed
}

WORD addr_accumulator(CPU *cpu UNUSED) {
   return 0; // Special case, handled in the instruction implementation
}

WORD addr_immediate(CPU *cpu) {
   return cpu->ProgramCounter++;
}

WORD addr_zero_page(CPU *cpu) {
   return cpu_read(cpu, cpu->ProgramCounter++);
}

WORD addr_zero_page_x(CPU *cpu) {
   return (cpu_read(cpu, cpu->ProgramCounter++) + cpu->XRegister) & 0xFF;
}

WORD addr_zero_page_y(CPU *cpu) {
   return (cpu_read(cpu, cpu->ProgramCounter++) + cpu->YRegister) & 0xFF;
}

WORD addr_relative(CPU *cpu) {
   int8_t offset = (int8_t)cpu_read(cpu, cpu->ProgramCounter++);
   return cpu->ProgramCounter + offset;
}

WORD addr_absolute(CPU *cpu) {
   WORD address = cpu_read(cpu, cpu->ProgramCounter++);
   address |= (cpu_read(cpu, cpu->ProgramCounter++) << 8);
   return address;
}

WORD addr_absolute_x(CPU *cpu) {
   WORD address = cpu_read(cpu, cpu->ProgramCounter++);
   address |= (cpu_read(cpu, cpu->ProgramCounter++) << 8);
   address += cpu->XRegister;
   return address;
}

WORD addr_absolute_y(CPU *cpu) {
   WORD address = cpu_read(cpu, cpu->ProgramCounter++);
   address |= (cpu_read(cpu, cpu->ProgramCounter++) << 8);
   address += cpu->YRegister;
   return address;
}

WORD addr_indirect(CPU *cpu) {
   WORD pointer = cpu_read(cpu, cpu->ProgramCounter++);
   pointer |= (cpu_read(cpu, cpu->ProgramCounter++) << 8);
   
   // Hardware bug in the 6502: If the indirect vector falls on a page boundary,
   // the LSB is fetched from the specified address, but the MSB is fetched from
   // the beginning of the page rather than the next byte
   WORD address;
   if ((pointer & 0xFF) == 0xFF) {
       address = cpu_read(cpu, pointer);
       address |= (cpu_read(cpu, pointer & 0xFF00) << 8);
   } else {
       address = cpu_read(cpu, pointer);
       address |= (cpu_read(cpu, pointer + 1) << 8);
   }
   
   return address;
}

WORD addr_indirect_x(CPU *cpu) {
   BYTE pointer = (cpu_read(cpu, cpu->ProgramCounter++) + cpu->XRegister) & 0xFF;
   WORD address = cpu_read(cpu, pointer);
   address |= (cpu_read(cpu, (pointer + 1) & 0xFF) << 8);
   return address;
}

WORD addr_indirect_y(CPU *cpu) {
   BYTE pointer = cpu_read(cpu, cpu->ProgramCounter++);
   WORD address = cpu_read(cpu, pointer);
   address |= (cpu_read(cpu, (pointer + 1) & 0xFF) << 8);
   address += cpu->YRegister;
   return address;
}

// CPU Initialization
void cpu_init(CPU *cpu) {
   // Clear memory
   memset(cpu->Memory, 0, sizeof(cpu->Memory));
   
   // Initialize registers
   cpu->Accumulator = 0;
   cpu->XRegister = 0;
   cpu->YRegister = 0;
   cpu->StackPointer = 0xFF;  // Stack starts at the top of page 1 (0x01FF)
   cpu->ProgramCounter = 0;
   
   // Initialize status register
   cpu->StatusRegister = UnusedFlag;  // Only UnusedFlag is set (always 1), others are cleared
   
   // Initialize 6510 port registers
   cpu->PortDirection = 0x2F;  // Default on power-up (bits 0-2, 5 are outputs)
   cpu->PortData = 0x37;       // Default on power-up
   
   // Initialize cycle count
   cpu->Cycles = 0;
   cpu->ExpectedCycles = 0;
   
   // Initialize timing
   clock_gettime(CLOCK_MONOTONIC, &cpu->LastCycleTime);
   cpu->EmulationStartTime = cpu->LastCycleTime;
   
   // Set running state
   cpu->IsRunning = true;
   
   // Current opcode not set yet
   cpu->CurrentOpcode = 0;
   
   // Set default speed mode
   cpu->CurrentSpeed = SPEED_REALTIME;
   
   // Initialize performance metrics
   cpu->InstructionsExecuted = 0;
   
   // Disable disassembly by default
   cpu->EnableDisassembly = false;
   cpu->ShowRegisters = false;
}

// Memory read with 6510 I/O port handling
BYTE cpu_read(CPU *cpu, WORD address) {
   // 6510 specific I/O port handling
   if (address == 0x0000) {
       return cpu->PortDirection;
   } else if (address == 0x0001) {
       return cpu->PortData;
   }
   
   return cpu->Memory[address];
}

// Memory write with 6510 I/O port handling
void cpu_write(CPU *cpu, WORD address, BYTE value) {
   // 6510 specific I/O port handling
   if (address == 0x0000) {
       cpu->PortDirection = value;
       return;
   } else if (address == 0x0001) {
       cpu->PortData = value;
       return;
   }
   
   cpu->Memory[address] = value;
}

// Push a byte onto the stack
void cpu_push(CPU *cpu, BYTE value) {
   cpu_write(cpu, 0x0100 + cpu->StackPointer, value);
   cpu->StackPointer--;
}

// Pull a byte from the stack
BYTE cpu_pull(CPU *cpu) {
   cpu->StackPointer++;
   return cpu_read(cpu, 0x0100 + cpu->StackPointer);
}

// Push a 16-bit value onto the stack (high byte first, then low byte)
void cpu_push16(CPU *cpu, WORD value) {
   cpu_push(cpu, value >> 8);    // High byte
   cpu_push(cpu, value & 0xFF);  // Low byte
}

// Pull a 16-bit value from the stack (low byte first, then high byte)
WORD cpu_pull16(CPU *cpu) {
   WORD low = cpu_pull(cpu);
   WORD high = cpu_pull(cpu);
   return (high << 8) | low;
}

// CPU Reset - simulates hardware reset
void cpu_reset(CPU *cpu) {
   // Maintain memory contents
   
   // Reset registers (Accumulator, XRegister, YRegister are unchanged by hardware reset)
   cpu->StackPointer = 0xFF;  // Stack pointer is decremented by 3 but we'll set it to top
   
   // Set status register (only InterruptDisableFlag is set by reset)
   set_flag(cpu, InterruptDisableFlag, true);
   
   // Load reset vector
   WORD reset_vector = (cpu_read(cpu, 0xFFFD) << 8) | cpu_read(cpu, 0xFFFC);
   cpu->ProgramCounter = reset_vector;
   
   // Reset cycle count
   cpu->Cycles = 0;
   cpu->ExpectedCycles = 0;
   
   // Reset timing
   clock_gettime(CLOCK_MONOTONIC, &cpu->LastCycleTime);
   cpu->EmulationStartTime = cpu->LastCycleTime;
   
   // Set running state
   cpu->IsRunning = true;
   
   // Reset performance metrics
   cpu->InstructionsExecuted = 0;
}

// Set speed mode (real-time 1MHz or maximum speed)
void cpu_set_speed_mode(CPU *cpu, SpeedMode mode) {
   // If changing from max speed to real-time, reset timing
   if (cpu->CurrentSpeed == SPEED_MAX && mode == SPEED_REALTIME) {
       clock_gettime(CLOCK_MONOTONIC, &cpu->LastCycleTime);
       cpu->ExpectedCycles = cpu->Cycles;
   }
   
   cpu->CurrentSpeed = mode;
}

// Synchronize CPU timing with real time to achieve 1MHz operation
void cpu_sync_timing(CPU *cpu) {
   // Skip timing synchronization if running at maximum speed
   if (cpu->CurrentSpeed == SPEED_MAX) {
       return;
   }
   
   struct timespec current_time;
   clock_gettime(CLOCK_MONOTONIC, &current_time);
   
   // Calculate elapsed time in microseconds
   QWORD elapsed_us = (current_time.tv_sec - cpu->LastCycleTime.tv_sec) * MICROSECONDS_PER_SECOND;
   elapsed_us += (current_time.tv_nsec - cpu->LastCycleTime.tv_nsec) / 1000;
   
   // Calculate expected number of cycles based on elapsed time and CPU frequency
   cpu->ExpectedCycles += (elapsed_us * CPU_FREQUENCY) / MICROSECONDS_PER_SECOND;
   
   // Update last cycle time
   cpu->LastCycleTime = current_time;
   
   // If we're ahead of schedule, sleep until we catch up
   if (cpu->Cycles > cpu->ExpectedCycles) {
       QWORD cycles_ahead = cpu->Cycles - cpu->ExpectedCycles;
       QWORD sleep_us = (cycles_ahead * MICROSECONDS_PER_SECOND) / CPU_FREQUENCY;
       
       // Only sleep if the delay is significant
       if (sleep_us > 100) {
           usleep(sleep_us);
           
           // Update timing again after sleep
           clock_gettime(CLOCK_MONOTONIC, &cpu->LastCycleTime);
           cpu->ExpectedCycles = cpu->Cycles;
       }
   }
}

// Get MIPS (Million Instructions Per Second) performance
double cpu_get_mips(CPU *cpu) {
   struct timespec current_time;
   clock_gettime(CLOCK_MONOTONIC, &current_time);
   
   // Calculate elapsed time in seconds
   double elapsed_seconds = (current_time.tv_sec - cpu->EmulationStartTime.tv_sec) + 
                          ((current_time.tv_nsec - cpu->EmulationStartTime.tv_nsec) / 1.0e9);
   
   // Calculate MIPS
   if (elapsed_seconds > 0) {
       return cpu->InstructionsExecuted / (elapsed_seconds * 1000000.0);
   }
   
   return 0.0;
}

// Branch instructions all have similar behavior - only take branch if condition is met
void branch_if(CPU *cpu, bool condition) {
   if (condition) {
       int8_t offset = (int8_t)cpu_read(cpu, cpu->ProgramCounter - 1);
       WORD old_pc = cpu->ProgramCounter;
       
       cpu->ProgramCounter += offset;
       
       // Add cycles for taking branch
       cpu->Cycles += 1;
       
       // Add extra cycle if branch crosses page boundary
       if ((old_pc & 0xFF00) != (cpu->ProgramCounter & 0xFF00)) {
           cpu->Cycles += 1;
       }
   }
}

// Instruction Implementations

// ADC - Add with Carry
void adc(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   WORD result = cpu->Accumulator + operand + (get_flag(cpu, CarryFlag) ? 1 : 0);
   
   // Set carry flag
   set_flag(cpu, CarryFlag, result > 0xFF);
   
   // Set overflow flag (sign bit overflow)
   set_flag(cpu, OverflowFlag, ((cpu->Accumulator ^ result) & (operand ^ result) & 0x80) != 0);
   
   // Set final result
   cpu->Accumulator = result & 0xFF;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// AND - Logical AND with accumulator
void and(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   cpu->Accumulator &= operand;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// ASL - Arithmetic Shift Left
void asl(CPU *cpu, WORD addr) {
   if (cpu->CurrentOpcode == 0x0A) {  // Accumulator mode
       // Shift accumulator
       set_flag(cpu, CarryFlag, (cpu->Accumulator & 0x80) != 0);
       cpu->Accumulator <<= 1;
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
       set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
   } else {
       // Shift memory
       BYTE operand = cpu_read(cpu, addr);
       
       set_flag(cpu, CarryFlag, (operand & 0x80) != 0);
       operand <<= 1;
       
       // Write back to memory
       cpu_write(cpu, addr, operand);
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, operand == 0);
       set_flag(cpu, NegativeFlag, (operand & 0x80) != 0);
   }
}

// BCC - Branch on Carry Clear
void bcc(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, !get_flag(cpu, CarryFlag));
}

// BCS - Branch on Carry Set
void bcs(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, get_flag(cpu, CarryFlag));
}

// BEQ - Branch on Equal (Zero Set)
void beq(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, get_flag(cpu, ZeroFlag));
}

// BIT - Test Bits in Memory with Accumulator
void bit(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   // Set zero flag based on AND result but don't change accumulator
   set_flag(cpu, ZeroFlag, (cpu->Accumulator & operand) == 0);
   
   // Set overflow and negative flags from operand
   set_flag(cpu, OverflowFlag, (operand & 0x40) != 0);
   set_flag(cpu, NegativeFlag, (operand & 0x80) != 0);
}

// BMI - Branch on Minus (Negative Set)
void bmi(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, get_flag(cpu, NegativeFlag));
}

// BNE - Branch on Not Equal (Zero Clear)
void bne(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, !get_flag(cpu, ZeroFlag));
}

// BPL - Branch on Plus (Negative Clear)
void bpl(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, !get_flag(cpu, NegativeFlag));
}

// BRK - Force Break
void bbrk(CPU *cpu, WORD addr UNUSED) {
   // Push PC+1 to stack
   cpu->ProgramCounter++;
   cpu_push16(cpu, cpu->ProgramCounter);
   
   // Push status register with B flag set
   set_flag(cpu, BreakCommandFlag, true);
   cpu_push(cpu, cpu->StatusRegister);
   set_flag(cpu, BreakCommandFlag, false);  // B flag is not an actual flag in the status register
   
   // Set interrupt disable
   set_flag(cpu, InterruptDisableFlag, true);
   
   // Load interrupt vector
   WORD irq_vector = (cpu_read(cpu, 0xFFFF) << 8) | cpu_read(cpu, 0xFFFE);
   cpu->ProgramCounter = irq_vector;
}

// BVC - Branch on Overflow Clear
void bvc(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, !get_flag(cpu, OverflowFlag));
}

// BVS - Branch on Overflow Set
void bvs(CPU *cpu, WORD addr UNUSED) {
   branch_if(cpu, get_flag(cpu, OverflowFlag));
}

// CLC - Clear Carry Flag
void clc(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, CarryFlag, false);
}

// CLD - Clear Decimal Mode Flag
void cld(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, DecimalModeFlag, false);
}

// CLI - Clear Interrupt Disable Flag
void cli(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, InterruptDisableFlag, false);
}

// CLV - Clear Overflow Flag
void clv(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, OverflowFlag, false);
}

// CMP - Compare Accumulator
void cmp(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   WORD result = cpu->Accumulator - operand;
   
   // Set flags
   set_flag(cpu, CarryFlag, cpu->Accumulator >= operand);
   set_flag(cpu, ZeroFlag, cpu->Accumulator == operand);
   set_flag(cpu, NegativeFlag, (result & 0x80) != 0);
}

// CPX - Compare X Register
void cpx(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   WORD result = cpu->XRegister - operand;
   
   // Set flags
   set_flag(cpu, CarryFlag, cpu->XRegister >= operand);
   set_flag(cpu, ZeroFlag, cpu->XRegister == operand);
   set_flag(cpu, NegativeFlag, (result & 0x80) != 0);
}

// CPY - Compare Y Register
void cpy(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   WORD result = cpu->YRegister - operand;
   
   // Set flags
   set_flag(cpu, CarryFlag, cpu->YRegister >= operand);
   set_flag(cpu, ZeroFlag, cpu->YRegister == operand);
   set_flag(cpu, NegativeFlag, (result & 0x80) != 0);
}

// DEC - Decrement Memory
void dec(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   operand--;
   
   // Write back to memory
   cpu_write(cpu, addr, operand);
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, operand == 0);
   set_flag(cpu, NegativeFlag, (operand & 0x80) != 0);
}

// DEX - Decrement X Register
void dex(CPU *cpu, WORD addr UNUSED) {
   cpu->XRegister--;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->XRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->XRegister & 0x80) != 0);
}

// DEY - Decrement Y Register
void dey(CPU *cpu, WORD addr UNUSED) {
   cpu->YRegister--;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->YRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->YRegister & 0x80) != 0);
}

// EOR - Exclusive OR
void eor(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   cpu->Accumulator ^= operand;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// INC - Increment Memory
void inc(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   operand++;
   
   // Write back to memory
   cpu_write(cpu, addr, operand);
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, operand == 0);
   set_flag(cpu, NegativeFlag, (operand & 0x80) != 0);
}

// INX - Increment X Register
void inx(CPU *cpu, WORD addr UNUSED) {
   cpu->XRegister++;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->XRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->XRegister & 0x80) != 0);
}

// INY - Increment Y Register
void iny(CPU *cpu, WORD addr UNUSED) {
   cpu->YRegister++;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->YRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->YRegister & 0x80) != 0);
}

// JMP - Jump
void jmp(CPU *cpu, WORD addr) {
   cpu->ProgramCounter = addr;
}

// JSR - Jump to Subroutine
void jsr(CPU *cpu, WORD addr) {
   // Push return address (PC-1) to stack
   cpu_push16(cpu, cpu->ProgramCounter - 1);
   
   // Jump to target address
   cpu->ProgramCounter = addr;
}

// LDA - Load Accumulator
void lda(CPU *cpu, WORD addr) {
   cpu->Accumulator = cpu_read(cpu, addr);
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// LDX - Load X Register
void ldx(CPU *cpu, WORD addr) {
   cpu->XRegister = cpu_read(cpu, addr);
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->XRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->XRegister & 0x80) != 0);
}

// LDY - Load Y Register
void ldy(CPU *cpu, WORD addr) {
   cpu->YRegister = cpu_read(cpu, addr);
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->YRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->YRegister & 0x80) != 0);
}

// LSR - Logical Shift Right
void lsr(CPU *cpu, WORD addr) {
   if (cpu->CurrentOpcode == 0x4A) {  // Accumulator mode
       // Shift accumulator
       set_flag(cpu, CarryFlag, (cpu->Accumulator & 0x01) != 0);
       cpu->Accumulator >>= 1;
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
       set_flag(cpu, NegativeFlag, false);  // Bit 7 will always be 0 after right shift
   } else {
       // Shift memory
       BYTE operand = cpu_read(cpu, addr);
       
       set_flag(cpu, CarryFlag, (operand & 0x01) != 0);
       operand >>= 1;
       
       // Write back to memory
       cpu_write(cpu, addr, operand);
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, operand == 0);
       set_flag(cpu, NegativeFlag, false);  // Bit 7 will always be 0 after right shift
   }
}

// NOP - No Operation
void nop(CPU *cpu UNUSED, WORD addr UNUSED) {
   // Do nothing
}

// ORA - Logical OR with Accumulator
void ora(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   cpu->Accumulator |= operand;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// PHA - Push Accumulator
void pha(CPU *cpu, WORD addr UNUSED) {
   cpu_push(cpu, cpu->Accumulator);
}

// PHP - Push Processor Status
void php(CPU *cpu, WORD addr UNUSED) {
   // When pushing status register, B and unused flags are set
   set_flag(cpu, BreakCommandFlag, true);
   set_flag(cpu, UnusedFlag, true);
   cpu_push(cpu, cpu->StatusRegister);
}

// PLA - Pull Accumulator
void pla(CPU *cpu, WORD addr UNUSED) {
   cpu->Accumulator = cpu_pull(cpu);
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// PLP - Pull Processor Status
void plp(CPU *cpu, WORD addr UNUSED) {
   // B flag is not modified when pulling status register
   BYTE old_b = get_flag(cpu, BreakCommandFlag);
   
   cpu->StatusRegister = cpu_pull(cpu);
   
   // Restore original B flag (B flag is not physically part of the status register)
   set_flag(cpu, BreakCommandFlag, old_b);
   
   // Unused flag is always set
   set_flag(cpu, UnusedFlag, true);
}

// ROL - Rotate Left
void rol(CPU *cpu, WORD addr) {
   if (cpu->CurrentOpcode == 0x2A) {  // Accumulator mode
       // Rotate accumulator
       bool old_carry = get_flag(cpu, CarryFlag);
       set_flag(cpu, CarryFlag, (cpu->Accumulator & 0x80) != 0);
       
       cpu->Accumulator <<= 1;
       if (old_carry) {
           cpu->Accumulator |= 0x01;
       }
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
       set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
   } else {
       // Rotate memory
       BYTE operand = cpu_read(cpu, addr);
       
       bool old_carry = get_flag(cpu, CarryFlag);
       set_flag(cpu, CarryFlag, (operand & 0x80) != 0);
       
       operand <<= 1;
       if (old_carry) {
           operand |= 0x01;
       }
       
       // Write back to memory
       cpu_write(cpu, addr, operand);
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, operand == 0);
       set_flag(cpu, NegativeFlag, (operand & 0x80) != 0);
   }
}

// ROR - Rotate Right
void ror(CPU *cpu, WORD addr) {
   if (cpu->CurrentOpcode == 0x6A) {  // Accumulator mode
       // Rotate accumulator
       bool old_carry = get_flag(cpu, CarryFlag);
       set_flag(cpu, CarryFlag, (cpu->Accumulator & 0x01) != 0);
       
       cpu->Accumulator >>= 1;
       if (old_carry) {
           cpu->Accumulator |= 0x80;
       }
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
       set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
   } else {
       // Rotate memory
       BYTE operand = cpu_read(cpu, addr);
       
       bool old_carry = get_flag(cpu, CarryFlag);
       set_flag(cpu, CarryFlag, (operand & 0x01) != 0);
       
       operand >>= 1;
       if (old_carry) {
           operand |= 0x80;
       }
       
       // Write back to memory
       cpu_write(cpu, addr, operand);
       
       // Set zero and negative flags
       set_flag(cpu, ZeroFlag, operand == 0);
       set_flag(cpu, NegativeFlag, (operand & 0x80) != 0);
   }
}

// RTI - Return from Interrupt
void rti(CPU *cpu, WORD addr UNUSED) {
   // Pull status register
   cpu->StatusRegister = cpu_pull(cpu);
   
   // B flag is always 0 after RTI
   set_flag(cpu, BreakCommandFlag, false);
   
   // Unused flag is always 1
   set_flag(cpu, UnusedFlag, true);
   
   // Pull program counter
   cpu->ProgramCounter = cpu_pull16(cpu);
}

// RTS - Return from Subroutine
void rts(CPU *cpu, WORD addr UNUSED) {
   // Pull program counter and add 1
   cpu->ProgramCounter = cpu_pull16(cpu) + 1;
}

// SBC - Subtract with Carry
void sbc(CPU *cpu, WORD addr) {
   BYTE operand = cpu_read(cpu, addr);
   
   // Perform subtraction using ones' complement
   WORD result = cpu->Accumulator + (BYTE)~operand + (get_flag(cpu, CarryFlag) ? 1 : 0);
   
   // Set carry flag (inverted from ADC)
   set_flag(cpu, CarryFlag, (result & 0x100) != 0);
   
   // Set overflow flag
   set_flag(cpu, OverflowFlag, ((cpu->Accumulator ^ result) & (~operand ^ result) & 0x80) != 0);
   
   // Set final result
   cpu->Accumulator = result & 0xFF;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// SEC - Set Carry Flag
void sec(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, CarryFlag, true);
}

// SED - Set Decimal Flag
void sed(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, DecimalModeFlag, true);
}

// SEI - Set Interrupt Disable
void sei(CPU *cpu, WORD addr UNUSED) {
   set_flag(cpu, InterruptDisableFlag, true);
}

// STA - Store Accumulator
void sta(CPU *cpu, WORD addr) {
   cpu_write(cpu, addr, cpu->Accumulator);
}

// STX - Store X Register
void stx(CPU *cpu, WORD addr) {
   cpu_write(cpu, addr, cpu->XRegister);
}

// STY - Store Y Register
void sty(CPU *cpu, WORD addr) {
   cpu_write(cpu, addr, cpu->YRegister);
}

// TAX - Transfer Accumulator to X
void tax(CPU *cpu, WORD addr UNUSED) {
   cpu->XRegister = cpu->Accumulator;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->XRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->XRegister & 0x80) != 0);
}

// TAY - Transfer Accumulator to Y
void tay(CPU *cpu, WORD addr UNUSED) {
   cpu->YRegister = cpu->Accumulator;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->YRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->YRegister & 0x80) != 0);
}

// TSX - Transfer Stack Pointer to X
void tsx(CPU *cpu, WORD addr UNUSED) {
   cpu->XRegister = cpu->StackPointer;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->XRegister == 0);
   set_flag(cpu, NegativeFlag, (cpu->XRegister & 0x80) != 0);
}

// TXA - Transfer X to Accumulator
void txa(CPU *cpu, WORD addr UNUSED) {
   cpu->Accumulator = cpu->XRegister;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// TXS - Transfer X to Stack Pointer
void txs(CPU *cpu, WORD addr UNUSED) {
   cpu->StackPointer = cpu->XRegister;
   
   // Note: TXS does not affect flags
}

// TYA - Transfer Y to Accumulator
void tya(CPU *cpu, WORD addr UNUSED) {
   cpu->Accumulator = cpu->YRegister;
   
   // Set zero and negative flags
   set_flag(cpu, ZeroFlag, cpu->Accumulator == 0);
   set_flag(cpu, NegativeFlag, (cpu->Accumulator & 0x80) != 0);
}

// Handle unknown opcode
void unknown_opcode(CPU *cpu, WORD addr UNUSED) {
   fprintf(stderr, "Warning: Unknown opcode 0x%02X encountered at address 0x%04X\n", 
           cpu->CurrentOpcode, cpu->ProgramCounter - 1);
   // Just a NOP for unknown opcodes
}

// Modified cpu_step function with disassembly support
void cpu_step(CPU *cpu) {
   if (!cpu->IsRunning) {
       return;
   }
   
   // Disassemble current instruction if enabled
   if (cpu->EnableDisassembly) {
       char disassembly[64];
       cpu_disassemble_instruction(cpu, cpu->ProgramCounter, disassembly, sizeof(disassembly));
       
       if (cpu->ShowRegisters) {
           printf("%s  A:%02X X:%02X Y:%02X SP:%02X P:%02X\n", 
                  disassembly, cpu->Accumulator, cpu->XRegister, cpu->YRegister, 
                  cpu->StackPointer, cpu->StatusRegister);
       } else {
           printf("%s\n", disassembly);
       }
   }
   
   // Fetch opcode
   BYTE opcode = cpu_read(cpu, cpu->ProgramCounter++);
   cpu->CurrentOpcode = opcode;
   
   // Get instruction details
   const Instruction *instr = &instruction_table[opcode];
   
   // Check if this is a known opcode
   if (instr->operation == NULL) {
       // Unknown opcode - handle it
       unknown_opcode(cpu, 0);
       cpu->Cycles += 1;  // Conservative default
       
       // Sync CPU timing if not in max speed mode
       cpu_sync_timing(cpu);
       return;
   }
   
   // Compute operand address using the addressing mode function
   WORD addr = instr->addr_mode(cpu);
   
   // Execute instruction with computed address
   instr->operation(cpu, addr);
   
   // Add cycles
   cpu->Cycles += instr->cycles;
   
   // Update instruction count for performance metrics
   cpu->InstructionsExecuted++;
   
   // Sync CPU timing if not in max speed mode
   cpu_sync_timing(cpu);
}

// Main function for emulator
int main(int argc, char **argv) {
   CPU cpu;
   bool fast_mode = false;
   bool disassemble = false;
   bool show_registers = false;
   
   // Process command line arguments
   for (int i = 1; i < argc; i++) {
       if (strcmp(argv[i], "--fast") == 0 || strcmp(argv[i], "-f") == 0) {
           fast_mode = true;
       } else if (strcmp(argv[i], "--disassemble") == 0 || strcmp(argv[i], "-d") == 0) {
           disassemble = true;
       } else if (strcmp(argv[i], "--registers") == 0 || strcmp(argv[i], "-r") == 0) {
           show_registers = true;
           disassemble = true;  // Showing registers implies disassembly
       }
   }
   
   // Initialize CPU
   cpu_init(&cpu);
   
   // Set speed mode based on command line argument
   if (fast_mode) {
       cpu_set_speed_mode(&cpu, SPEED_MAX);
       printf("Running at maximum speed\n");
   } else {
       printf("Running at 1MHz (real-time)\n");
   }
   
   // Enable disassembly if requested
   if (disassemble) {
       cpu_enable_disassembly(&cpu, true, show_registers);
   }
   
   // Load a program (for example)
   // This would come from reading a ROM file in a real emulator
   // Example: A simple program that adds 5 and 10, stores result in location 0x200
   cpu.Memory[0xFFFC] = 0x00;  // Reset vector LSB
   cpu.Memory[0xFFFD] = 0x80;  // Reset vector MSB
   
   // Program at 0x8000
   cpu.Memory[0x8000] = 0xA9;  // LDA #$05
   cpu.Memory[0x8001] = 0x05;
   cpu.Memory[0x8002] = 0x69;  // ADC #$0A
   cpu.Memory[0x8003] = 0x0A;
   cpu.Memory[0x8004] = 0x8D;  // STA $0200
   cpu.Memory[0x8005] = 0x00;
   cpu.Memory[0x8006] = 0x02;
   cpu.Memory[0x8007] = 0x00;  // BRK
   
   // Reset CPU to start execution
   cpu_reset(&cpu);
   
   // Main emulation loop
   while (cpu.IsRunning) {
       cpu_step(&cpu);
       
       // Check for BRK instruction to stop
       if (cpu.Memory[cpu.ProgramCounter] == 0x00) {
           break;
       }
   }
   
   // Print final state
   printf("\n6510 Emulation Complete\n");
   printf("Accumulator: $%02X XRegister: $%02X YRegister: $%02X StackPointer: $%02X\n", 
          cpu.Accumulator, cpu.XRegister, cpu.YRegister, cpu.StackPointer);
   printf("ProgramCounter: $%04X Result at $0200: $%02X\n", 
          cpu.ProgramCounter, cpu.Memory[0x0200]);
   printf("Total cycles: %" PRIu64 "\n", cpu.Cycles);
   printf("Instructions executed: %" PRIu64 "\n", cpu.InstructionsExecuted);
   printf("Performance: %.2f MIPS\n", cpu_get_mips(&cpu));
   
   return 0;
}
