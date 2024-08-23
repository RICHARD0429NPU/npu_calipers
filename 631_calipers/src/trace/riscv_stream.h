/**
 * Copyright (c) Microsoft Corporation.
 * 
 * MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RISCV_STREAM_H
#define RISCV_STREAM_H

#include <unordered_map>
#include "instruction_stream.h"

/**
 * Defining how a RISC-V stream of instructions is parsed
 * Based on: "The RISC-V Instruction Set Manual" (Version 2.2)
 */
class RiscvStream : public InstructionStream
{
  private:
enum IntReg
    {
        // 64-bit registers (x0-x30)
        x0 = 0,
        x1 = 1,
        x2 = 2,
        x3 = 3,
        x4 = 4,
        x5 = 5,
        x6 = 6,
        x7 = 7,
        x8 = 8,
        x9 = 9,
        x10 = 10,
        x11 = 11,
        x12 = 12,
        x13 = 13,
        x14 = 14,
        x15 = 15,
        x16 = 16,
        x17 = 17,
        x18 = 18,
        x19 = 19,
        x20 = 20,
        x21 = 21,
        x22 = 22,
        x23 = 23,
        x24 = 24,
        x25 = 25,
        x26 = 26,
        x27 = 27,
        x28 = 28,
        x29 = 29,  // Frame Pointer (x29)
        x30 = 30,  // Link Register (x30)
        sp = 31,  // Stack Pointer
        pc = 32,  // Program Counter

        // 32-bit registers (w0-w30)
        w0 = 33,
        w1 = 34,
        w2 = 35,
        w3 = 36,
        w4 = 37,
        w5 = 38,
        w6 = 39,
        w7 = 40,
        w8 = 41,
        w9 = 42,
        w10 = 43,
        w11 = 44,
        w12 = 45,
        w13 = 46,
        w14 = 47,
        w15 = 48,
        w16 = 49,
        w17 = 50,
        w18 = 51,
        w19 = 52,
        w20 = 53,
        w21 = 54,
        w22 = 55,
        w23 = 56,
        w24 = 57,
        w25 = 58,
        w26 = 59,
        w27 = 60,
        w28 = 61,
        w29 = 62,  // Frame Pointer (w29)
        w30 = 63,  // Link Register (w30)

        last = 63
    }; // enum IntReg

    enum FpReg
    {
        v0 = IntReg::last + 1,
        v1,
        v2,
        v3,
        v4,
        v5,
        v6,
        v7,
        v8,
        v9,
        v10,
        v11,
        v12,
        v13,
        v14,
        v15,
        v16,
        v17,
        v18,
        v19,
        v20,
        v21,
        v22,
        v23,
        v24,
        v25,
        v26,
        v27,
        v28,
        v29,
        v30,
        v31
    }; // enum FpReg

    enum Csr
    {
        SCTLR_EL1 = 0x000,
        CPACR_EL1 = 0x002,
        TTBR0_EL1 = 0x008,
        TTBR1_EL1 = 0x009,
        ESR_EL1 = 0x012,
        FAR_EL1 = 0x013,
        AFSR0_EL1 = 0x014,
        AFSR1_EL1 = 0x015,
        CONTEXTIDR_EL1 = 0x019,
        TPIDR_EL0 = 0x01E,
        TPIDR_EL1 = 0x081,
        TPIDR_EL2 = 0x082,
        TPIDR_EL3 = 0x083,
        CNTFRQ_EL0 = 0xC01,
        CNTPCT_EL0 = 0xC02,
        CNTVCT_EL0 = 0xC03,
    }; // enum Csr

    unordered_map<string, int> regMap;
    // Key: Register name, Value: Register number in the IntReg enum

    unordered_map<string, int> opcodeToTypeMap;
    // Key: Opcode, Value: ExecutionType

    unordered_map<string, char[MAX_OPERANDS]> syntaxMap;
    // Key: Opcode, Value: R/W characters for register read/write

    unordered_map<string, char> memAccessMap;
    // Key: Opcode, Value: L/S/A character for memory load/store/atomic operations

    unordered_map<string, uint32_t> memLengthMap;
    // Key: Opcode, Value: Memory access in bytes

    unordered_map<string, uint32_t> bytesMap;
    // Key: Opcode, Value: Number of instruction bytes

    string inst;
    string lastInstrLine;
    bool readFromFile = true;
    
    void initMaps();
    string parseNext(string& instr_line, size_t& current_pos);
    void parseInstr(string& instr_line);
    bool parseBranch(string& branch_line);
    uint32_t parseMemoryCycles(string& mem_line);
    uint32_t parseFetchCycles(string& fetch_line);
    string get_inst(string);

  public:
    RiscvStream(string trace_file_name, bool trace_bp, bool trace_icache, bool trace_dcache) :
                InstructionStream(trace_file_name, trace_bp, trace_icache, trace_dcache)
    {
        initMaps();
    }

    Instruction* next();
};

#endif // RISCV_STREAM_H
