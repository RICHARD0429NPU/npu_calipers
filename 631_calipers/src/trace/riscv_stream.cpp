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

#include <iostream>
#include <string.h>

#include "calipers_defs.h"
#include "calipers_types.h"
#include "instruction_stream.h"
#include "riscv_stream.h"

using namespace std;

Instruction* RiscvStream::next()
{
    string line;

    while (true)
    {
        if (readFromFile)
        {
            if (!getline(traceFile, line))
            {
                return NULL;
            }
        }
        else
        {
            line = lastInstrLine;
            readFromFile = true;
            if (line == "")
            {
                return NULL;
            }
        }

        //cout << "-----------" << endl;
        //cout << line << endl;

        if (line.find("@I ") == 0)      //开头
        {
            parseInstr(line);
            lastInstrLine = line;

            if (traceICache)
            {
                string fetch_line;
                getline(traceFile, fetch_line);
                if (fetch_line.find("@F ") != 0)
                {
                    CALIPERS_ERROR("Expecting fetch cycles for \"" << line << 
                                   "\" but getting \"" << fetch_line << "\"");
                }
                instr.fetchCycles = parseFetchCycles(fetch_line);
            }

            if (traceBP)
            {
                string branch_line;
                getline(traceFile, branch_line);
                if (branch_line.find("@B ") != 0)
                {
                    CALIPERS_ERROR("Expecting branch prediction result for \"" << line <<
                                   "\" but getting \"" << branch_line << "\"");
                }
                instr.mispredicted = parseBranch(branch_line);

                if (instr.mispredicted &&
                    (instr.executionType != ExecutionType::BranchCond) && 
                    (instr.executionType != ExecutionType::BranchUncond) &&
                    (instr.executionType != ExecutionType::Syscall))
                {
                    //CALIPERS_ERROR("Misprediction for a regular instruction \"" << line << "\"");
                    CALIPERS_WARNING("Misprediction for a regular instruction \"" << line << "\"");
                    //instr.mispredicted = false;
                }
            }

            if (traceDCache)
            {
                if ((instr.executionType == ExecutionType::Load) ||
                    (instr.executionType == ExecutionType::Store) ||
                    (instr.executionType == ExecutionType::Atomic))
                {
                    string mem_line;
                    getline(traceFile, mem_line);
                    if (mem_line.find("@M ") != 0)
                    {
                        //CALIPERS_ERROR("Expecting memory access cycles for \"" << line << "\"");
                        CALIPERS_WARNING("Expecting memory access cycles for \"" << line << "\"");
                        instr.lsCycles = 1;
                        lastInstrLine = mem_line;
                        readFromFile = false;
                    }
                    else
                    {
                        instr.lsCycles = parseMemoryCycles(mem_line);
                    }
                }
            }

            break;
        }
        else if ((line.find("@F") == 0) || (line.find("@B") == 0) || (line.find("@M") == 0))
        {
            // Probably because of atomic instructions
            CALIPERS_WARNING("Ignoring \"" << line << "\" after \"" << lastInstrLine << "\"");
        }
        else
        {
            CALIPERS_ERROR("Invalid trace line \"" << line << "\"");
        }
    }
    return &instr;
}

string RiscvStream::parseNext(string& instr_line, size_t& current_pos)
{
    size_t pos;

    if (current_pos == string::npos)
    {
        return "";
    }

    pos = instr_line.find(" ", current_pos);

    string str;
    
    if (pos == string::npos)
    {
        str = instr_line.substr(current_pos);
        current_pos = string::npos;
    }
    else
    {
        str = instr_line.substr(current_pos, pos - current_pos);
        current_pos = pos + 1;
    }

    pos = str.find("(");
    if (pos != string::npos)
    {
        size_t next_pos = str.find(")");
        return str.substr(pos + 1, next_pos - pos - 1);
    }
    else if (str[str.length() - 1] == ',')
    {
        return str.substr(0, str.length() - 1);
    }
    else
    {
        return str;
    }
}

void RiscvStream::parseInstr(string& instr_line)
{
    //cout << instr_line << endl;

    string opcode;
    string pc;
    string operands[MAX_OPERANDS];
    string mem_address;

    uint32_t operand_count = 0;
    bool mem_accessed = false;

    size_t current_pos = 3;

    pc = parseNext(instr_line, current_pos);
    //cout << pc << endl;

    opcode = parseNext(instr_line, current_pos);
    //cout << opcode << endl;

    while (operand_count < MAX_OPERANDS)
    {
        string operand = parseNext(instr_line, current_pos);

        if (operand == "")
        {
            break;
        }

        if ((operand[0] >= 'a') && (operand[0] <= 'z'))
        {
            operands[operand_count] = operand;
            ++operand_count;
        }
        if (operand[0] == '@')
        {
            mem_accessed = true;
            break;
        }
    }

    if (mem_accessed)
    {
        mem_address = parseNext(instr_line, current_pos);
        //cout << mem_address << endl;
    }

    if (opcodeToTypeMap.count(opcode) == 0)
    {
        CALIPERS_ERROR("Invalid opcode \"" << instr_line << "\"");
    }

    instr.pc = stoull(pc, NULL, 16);
    instr.bytes = bytesMap[opcode];

    int executionType = opcodeToTypeMap[opcode];
    instr.executionType = executionType;

    if (executionType == ExecutionType::Atomic)
    {
        mem_address = "0xffffffffffffffff";
    }

    char* syntax = syntaxMap[opcode];
    uint32_t reg_read_count = 0;
    uint32_t reg_write_count = 0;
    for (uint32_t i = 0; i < operand_count; ++i)
    {
        int operand = regMap[operands[i]];

        if (syntax[i] == 'W')
        {
            instr.regWrite[reg_write_count] = operand;
            ++reg_write_count;
        }
        else if (syntax[i] == 'R')
        {
            instr.regRead[reg_read_count] = operand;
            ++reg_read_count;
        }
        else
        {
            CALIPERS_ERROR("Invalid operand \"" << instr_line << "\"");
        }
    }

    instr.regReadCount = reg_read_count;
    instr.regWriteCount = reg_write_count;

    if (mem_accessed)
    {
        char mem_access = memAccessMap[opcode];
        if (mem_access == 0)
        {
            CALIPERS_ERROR("Instruction should not access memory \"" << instr_line << "\"");
        }

        if (mem_access == 'L')
        {
            instr.memStoreCount = 0;
            instr.memLoadCount = 1;
            instr.memLoadBase = stoull(mem_address, NULL, 16);
            instr.memLoadLength = memLengthMap[opcode];
        }
        else if (mem_access == 'S')
        {
            instr.memLoadCount = 0;
            instr.memStoreCount = 1;
            instr.memStoreBase = stoull(mem_address, NULL, 16);
            instr.memStoreLength = memLengthMap[opcode];
        }
        else if (mem_access == 'A')
        {
            instr.memLoadCount = 1;
            instr.memStoreCount = 1;
            instr.memLoadBase = stoull(mem_address, NULL, 16);
            instr.memLoadLength = memLengthMap[opcode];
            instr.memStoreBase = instr.memLoadBase;
            instr.memStoreLength = instr.memLoadLength;
        }
        else
        {
            CALIPERS_ERROR("Invalid memory access \"" << instr_line << "\"");
        }
    }
    else
    {
        instr.memLoadCount = 0;
        instr.memStoreCount = 0;        
    }
}

bool RiscvStream::parseBranch(string& branch_line)
{
    //cout << branch_line << endl;

    size_t current_pos = 3;

    string prediction = parseNext(branch_line, current_pos);

    bool mispredicted;

    if (prediction[0] == '0')
    {
        mispredicted = true;
    }
    else if (prediction[0] == '1')
    {
        mispredicted = false;
    }
    else
    {
        CALIPERS_ERROR("Invalid branch prediction result");
    }

    return mispredicted;
}

uint32_t RiscvStream::parseMemoryCycles(string& mem_line)
{
    //cout << mem_line << endl;

    size_t current_pos = 3;

    string cycles = parseNext(mem_line, current_pos);

    uint32_t num = stoul(cycles) / TICKS_PER_CYCLE;

    return num;
}

uint32_t RiscvStream::parseFetchCycles(string& fetch_line)
{
    //cout << fetch_line << endl;

    size_t current_pos = 3;

    string cycles = parseNext(fetch_line, current_pos);

    return stoul(cycles) / TICKS_PER_CYCLE;
}

void RiscvStream::initMaps()
{
    // Initializing register maps

    regMap["x0"] = IntReg::x0;
    regMap["x1"] = IntReg::x1;
    regMap["x2"] = IntReg::x2;
    regMap["x3"] = IntReg::x3;
    regMap["x4"] = IntReg::x4;
    regMap["x5"] = IntReg::x5;
    regMap["x6"] = IntReg::x6;
    regMap["x7"] = IntReg::x7;
    regMap["x8"] = IntReg::x8;
    regMap["x9"] = IntReg::x9;
    regMap["x10"] = IntReg::x10;
    regMap["x11"] = IntReg::x11;
    regMap["x12"] = IntReg::x12;
    regMap["x13"] = IntReg::x13;
    regMap["x14"] = IntReg::x14;
    regMap["x15"] = IntReg::x15;
    regMap["x16"] = IntReg::x16;
    regMap["x17"] = IntReg::x17;
    regMap["x18"] = IntReg::x18;
    regMap["x19"] = IntReg::x19;
    regMap["x20"] = IntReg::x20;
    regMap["x21"] = IntReg::x21;
    regMap["x22"] = IntReg::x22;
    regMap["x23"] = IntReg::x23;
    regMap["x24"] = IntReg::x24;
    regMap["x25"] = IntReg::x25;
    regMap["x26"] = IntReg::x26;
    regMap["x27"] = IntReg::x27;
    regMap["x28"] = IntReg::x28;
    regMap["x29"] = IntReg::x29;
    regMap["x30"] = IntReg::x30;
    regMap["sp"]  =  IntReg::sp;
    regMap["pc"]  =  IntReg::pc;

    regMap["w0"] = IntReg::w0;
    regMap["w1"] = IntReg::w1;
    regMap["w2"] = IntReg::w2;
    regMap["w3"] = IntReg::w3;
    regMap["w4"] = IntReg::w4;
    regMap["w5"] = IntReg::w5;
    regMap["w6"] = IntReg::w6;
    regMap["w7"] = IntReg::w7;
    regMap["w8"] = IntReg::w8;
    regMap["w9"] = IntReg::w9;
    regMap["w10"] = IntReg::w10;
    regMap["w11"] = IntReg::w11;
    regMap["w12"] = IntReg::w12;
    regMap["w13"] = IntReg::w13;
    regMap["w14"] = IntReg::w14;
    regMap["w15"] = IntReg::w15;
    regMap["w16"] = IntReg::w16;
    regMap["w17"] = IntReg::w17;
    regMap["w18"] = IntReg::w18;
    regMap["w19"] = IntReg::w19;
    regMap["w20"] = IntReg::w20;
    regMap["w21"] = IntReg::w21;
    regMap["w22"] = IntReg::w22;
    regMap["w23"] = IntReg::w23;
    regMap["w24"] = IntReg::w24;
    regMap["w25"] = IntReg::w25;
    regMap["w26"] = IntReg::w26;
    regMap["w27"] = IntReg::w27;
    regMap["w28"] = IntReg::w28;
    regMap["w29"] = IntReg::w29;
    regMap["w30"] = IntReg::w30;

    // IntBase,
    // IntMul,
    // IntDiv,
    // FpBase,
    // FpMul,
    // FpDiv,
    // Load,
    // Store,
    // BranchCond,
    // BranchUncond,
    // Syscall,
    // Atomic,
    // Other 

    opcodeToTypeMap["addi"] = ExecutionType::IntBase;
    strncpy(syntaxMap["addi"], "WR", MAX_OPERANDS);
    memAccessMap["addi"] = 0;
    memLengthMap["addi"] = 0;
    bytesMap["addi"] = 4;

    opcodeToTypeMap["rev"] = ExecutionType::IntBase;
    strncpy(syntaxMap["rev"], "WRR", MAX_OPERANDS);
    memAccessMap["rev"] = 0;
    memLengthMap["rev"] = 0;
    bytesMap["rev"] = 4;

    opcodeToTypeMap["nop"] = ExecutionType::IntBase;
    strncpy(syntaxMap["nop"], "", MAX_OPERANDS);
    memAccessMap["nop"] = 0;
    memLengthMap["nop"] = 0;
    bytesMap["nop"] = 4;

    opcodeToTypeMap["and"] = ExecutionType::IntBase;
    strncpy(syntaxMap["and"], "WR", MAX_OPERANDS);
    memAccessMap["and"] = 0;
    memLengthMap["and"] = 0;
    bytesMap["and"] = 4;

    opcodeToTypeMap["tst"] = ExecutionType::IntBase;
    strncpy(syntaxMap["tst"], "W", MAX_OPERANDS);
    memAccessMap["tst"] = 0;
    memLengthMap["tst"] = 0;
    bytesMap["tst"] = 4;    

    opcodeToTypeMap["clz"] = ExecutionType::IntBase;
    strncpy(syntaxMap["clz"], "WR", MAX_OPERANDS);
    memAccessMap["clz"] = 0;
    memLengthMap["clz"] = 0;
    bytesMap["clz"] = 4;

    opcodeToTypeMap["ands"] = ExecutionType::IntBase;
    strncpy(syntaxMap["ands"], "WR", MAX_OPERANDS);
    memAccessMap["ands"] = 0;
    memLengthMap["ands"] = 0;
    bytesMap["ands"] = 4;

    opcodeToTypeMap["ubfm"] = ExecutionType::IntBase;
    strncpy(syntaxMap["ubfm"], "WR", MAX_OPERANDS);
    memAccessMap["ubfm"] = 0;
    memLengthMap["ubfm"] = 0;
    bytesMap["ubfm"] = 4;

    opcodeToTypeMap["adrp"] = ExecutionType::IntBase;
    strncpy(syntaxMap["adrp"], "W", MAX_OPERANDS);
    memAccessMap["adrp"] = 0;
    memLengthMap["adrp"] = 0;
    bytesMap["adrp"] = 4;

    opcodeToTypeMap["asrv"] = ExecutionType::IntBase;
    strncpy(syntaxMap["asrv"], "WRR", MAX_OPERANDS);
    memAccessMap["asrv"] = 0;
    memLengthMap["asrv"] = 0;
    bytesMap["asrv"] = 4;
    
    opcodeToTypeMap["asr"] = ExecutionType::IntBase;
    strncpy(syntaxMap["asr"], "WRR", MAX_OPERANDS);
    memAccessMap["asr"] = 0;
    memLengthMap["asr"] = 0;
    bytesMap["asr"] = 4;

    opcodeToTypeMap["lsrv"] = ExecutionType::IntBase;
    strncpy(syntaxMap["lsrv"], "WRR", MAX_OPERANDS);
    memAccessMap["lsrv"] = 0;
    memLengthMap["lsrv"] = 0;
    bytesMap["lsrv"] = 4;

    opcodeToTypeMap["lsr"] = ExecutionType::IntBase;
    strncpy(syntaxMap["lsr"], "WR", MAX_OPERANDS);
    memAccessMap["lsr"] = 0;
    memLengthMap["lsv"] = 0;
    bytesMap["lsr"] = 4;

    opcodeToTypeMap["lslv"] = ExecutionType::IntBase;
    strncpy(syntaxMap["lslv"], "WRR", MAX_OPERANDS);
    memAccessMap["lslv"] = 0;
    memLengthMap["lslv"] = 0;
    bytesMap["lslv"] = 4;

    opcodeToTypeMap["lsl"] = ExecutionType::IntBase;
    strncpy(syntaxMap["lsl"], "WR", MAX_OPERANDS);
    memAccessMap["lsl"] = 0;
    memLengthMap["lsl"] = 0;
    bytesMap["lsl"] = 4;

    opcodeToTypeMap["cmp"] = ExecutionType::IntBase;
    strncpy(syntaxMap["cmp"], "WR", MAX_OPERANDS);
    memAccessMap["cmp"] = 0;
    memLengthMap["cmp"] = 0;
    bytesMap["cmp"] = 4;

    opcodeToTypeMap["ccmp.eq"] = ExecutionType::IntBase;
    strncpy(syntaxMap["ccmp.eq"], "WRR", MAX_OPERANDS);
    memAccessMap["ccmp.eq"] = 0;
    memLengthMap["ccmp.eq"] = 0;
    bytesMap["ccmp.eq"] = 4;

    opcodeToTypeMap["ccmp.ne"] = ExecutionType::IntBase;
    strncpy(syntaxMap["ccmp.ne"], "WR", MAX_OPERANDS);
    memAccessMap["ccmp.ne"] = 0;
    memLengthMap["ccmp.ne"] = 0;
    bytesMap["ccmp.ne"] = 4;

    opcodeToTypeMap["ccmp.cs"] = ExecutionType::IntBase;
    strncpy(syntaxMap["ccmp.cs"], "WR", MAX_OPERANDS);
    memAccessMap["ccmp.cs"] = 0;
    memLengthMap["ccmp.cs"] = 0;
    bytesMap["ccmp.cs"] = 4;

    opcodeToTypeMap["orr"] = ExecutionType::IntBase;
    strncpy(syntaxMap["orr"], "WR", MAX_OPERANDS);
    memAccessMap["orr"] = 0;
    memLengthMap["orr"] = 0;
    bytesMap["orr"] = 4;

    opcodeToTypeMap["bics"] = ExecutionType::IntBase;
    strncpy(syntaxMap["bics"], "WRR", MAX_OPERANDS);
    memAccessMap["bics"] = 0;
    memLengthMap["bics"] = 0;
    bytesMap["bics"] = 4;

    opcodeToTypeMap["eor"] = ExecutionType::IntBase;
    strncpy(syntaxMap["eor"], "WRR", MAX_OPERANDS);
    memAccessMap["eor"] = 0;
    memLengthMap["eor"] = 0;
    bytesMap["eor"] = 4;

    opcodeToTypeMap["mrs"] = ExecutionType::IntBase;
    strncpy(syntaxMap["mrs"], "WR", MAX_OPERANDS);
    memAccessMap["mrs"] = 0;
    memLengthMap["mrs"] = 0;
    bytesMap["mrs"] = 4;

    opcodeToTypeMap["mov"] = ExecutionType::IntBase;
    strncpy(syntaxMap["mov"], "W", MAX_OPERANDS);
    memAccessMap["mov"] = 0;
    memLengthMap["mov"] = 0;
    bytesMap["mov"] = 4;

    opcodeToTypeMap["movn"] = ExecutionType::IntBase;
    strncpy(syntaxMap["movn"], "W", MAX_OPERANDS);
    memAccessMap["movn"] = 0;
    memLengthMap["movn"] = 0;
    bytesMap["movn"] = 4;

    opcodeToTypeMap["csinc"] = ExecutionType::IntBase;
    strncpy(syntaxMap["csinc"], "WRR", MAX_OPERANDS);
    memAccessMap["csinc"] = 0;
    memLengthMap["csinc"] = 0;
    bytesMap["csinc"] = 4;

    opcodeToTypeMap["cset"] = ExecutionType::IntBase;
    strncpy(syntaxMap["cset"], "W", MAX_OPERANDS);
    memAccessMap["cset"] = 0;
    memLengthMap["cset"] = 0;
    bytesMap["cset"] = 4;

    opcodeToTypeMap["csel"] = ExecutionType::IntBase;
    strncpy(syntaxMap["csel"], "WRR", MAX_OPERANDS);
    memAccessMap["csel"] = 0;
    memLengthMap["csel"] = 0;
    bytesMap["csel"] = 4;

    opcodeToTypeMap["add"] = ExecutionType::IntBase;
    strncpy(syntaxMap["add"], "WRR", MAX_OPERANDS);
    memAccessMap["add"] = 0;
    memLengthMap["add"] = 0;
    bytesMap["add"] = 4;

    opcodeToTypeMap["subs"] = ExecutionType::IntBase;
    strncpy(syntaxMap["subs"], "WR", MAX_OPERANDS);
    memAccessMap["subs"] = 0;
    memLengthMap["subs"] = 0;
    bytesMap["subs"] = 4;

    opcodeToTypeMap["neg"] = ExecutionType::IntBase;
    strncpy(syntaxMap["neg"], "WRR", MAX_OPERANDS);
    memAccessMap["neg"] = 0;
    memLengthMap["neg"] = 0;
    bytesMap["neg"] = 4;

    opcodeToTypeMap["sub"] = ExecutionType::IntBase;
    strncpy(syntaxMap["sub"], "WRR", MAX_OPERANDS);
    memAccessMap["sub"] = 0;
    memLengthMap["sub"] = 0;
    bytesMap["sub"] = 4;
    
    opcodeToTypeMap["mul"] = ExecutionType::IntMul;
    strncpy(syntaxMap["mul"], "WRR", MAX_OPERANDS);
    memAccessMap["mul"] = 0;
    memLengthMap["mul"] = 0;
    bytesMap["mul"] = 4;

    opcodeToTypeMap["umaddl"] = ExecutionType::IntMul;
    strncpy(syntaxMap["umaddl"], "WRR", MAX_OPERANDS);
    memAccessMap["umaddl"] = 0;
    memLengthMap["umaddl"] = 0;
    bytesMap["umaddl"] = 4;

    opcodeToTypeMap["umull"] = ExecutionType::IntMul;
    strncpy(syntaxMap["umull"], "WRR", MAX_OPERANDS);
    memAccessMap["umull"] = 0;
    memLengthMap["umull"] = 0;
    bytesMap["umull"] = 4;

    opcodeToTypeMap["umulh"] = ExecutionType::IntMul;
    strncpy(syntaxMap["umulh"], "WRR", MAX_OPERANDS);
    memAccessMap["umulh"] = 0;
    memLengthMap["umulh"] = 0;
    bytesMap["umulh"] = 4;

    opcodeToTypeMap["madd"] = ExecutionType::IntMul;
    strncpy(syntaxMap["madd"], "WRRR", MAX_OPERANDS);
    memAccessMap["madd"] = 0;
    memLengthMap["madd"] = 0;
    bytesMap["madd"] = 4;

    opcodeToTypeMap["umaddl"] = ExecutionType::IntMul;
    strncpy(syntaxMap["umaddl"], "WRRR", MAX_OPERANDS);
    memAccessMap["umaddl"] = 0;
    memLengthMap["umaddl"] = 0;
    bytesMap["umaddl"] = 4;

    opcodeToTypeMap["msub"] = ExecutionType::IntMul;
    strncpy(syntaxMap["msub"], "WRRR", MAX_OPERANDS);
    memAccessMap["msub"] = 0;
    memLengthMap["msub"] = 0;
    bytesMap["msub"] = 4;

    opcodeToTypeMap["udiv"] = ExecutionType::IntDiv;
    strncpy(syntaxMap["udiv"], "WRR", MAX_OPERANDS);
    memAccessMap["udiv"] = 0;
    memLengthMap["udiv"] = 0;
    bytesMap["udiv"] = 4;

    opcodeToTypeMap["fadd_s"] = ExecutionType::FpBase;
    strncpy(syntaxMap["fadd_s"], "WRR", MAX_OPERANDS);
    memAccessMap["fadd_s"] = 0;
    memLengthMap["fadd_s"] = 0;
    bytesMap["fadd_s"] = 4;

    opcodeToTypeMap["fmul_s"] = ExecutionType::FpMul;
    strncpy(syntaxMap["fmul_s"], "WRR", MAX_OPERANDS);
    memAccessMap["fmul_s"] = 0;
    memLengthMap["fmul_s"] = 0;
    bytesMap["fmul_s"] = 4;

    opcodeToTypeMap["fdiv_s"] = ExecutionType::FpDiv;
    strncpy(syntaxMap["fdiv_s"], "WRR", MAX_OPERANDS);
    memAccessMap["fdiv_s"] = 0;
    memLengthMap["fdiv_s"] = 0;
    bytesMap["fdiv_s"] = 4;

    opcodeToTypeMap["ldr"] = ExecutionType::Load;
    strncpy(syntaxMap["ldr"], "WR", MAX_OPERANDS);
    memAccessMap["ldr"] = 'L';
    memLengthMap["ldr"] = 8;
    bytesMap["ldr"] = 4;

    opcodeToTypeMap["ldur"] = ExecutionType::Load;
    strncpy(syntaxMap["ldur"], "WR", MAX_OPERANDS);
    memAccessMap["ldur"] = 'L';
    memLengthMap["ldur"] = 8;
    bytesMap["ldur"] = 4;

    opcodeToTypeMap["ldrb"] = ExecutionType::Load;
    strncpy(syntaxMap["ldrb"], "WR", MAX_OPERANDS);
    memAccessMap["ldrb"] = 'L';
    memLengthMap["ldrb"] = 4;
    bytesMap["ldrb"] = 4;

    opcodeToTypeMap["ldrh"] = ExecutionType::Load;
    strncpy(syntaxMap["ldrh"], "WR", MAX_OPERANDS);
    memAccessMap["ldrh"] = 'L';
    memLengthMap["ldrh"] = 4;
    bytesMap["ldrh"] = 4;

    opcodeToTypeMap["ldp"] = ExecutionType::Load;
    strncpy(syntaxMap["ldp"], "WWR", MAX_OPERANDS);
    memAccessMap["ldp"] = 'L';
    memLengthMap["ldp"] = 16;
    bytesMap["ldp"] = 4;

    opcodeToTypeMap["str"] = ExecutionType::Store;
    strncpy(syntaxMap["str"], "RR", MAX_OPERANDS);
    memAccessMap["str"] = 'S';
    memLengthMap["str"] = 8;
    bytesMap["str"] = 4;

    opcodeToTypeMap["stp"] = ExecutionType::Store;
    strncpy(syntaxMap["stp"], "RRR", MAX_OPERANDS);
    memAccessMap["stp"] = 'S';
    memLengthMap["stp"] = 16;
    bytesMap["stp"] = 4;

    opcodeToTypeMap["b.eq"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["b.eq"], "", MAX_OPERANDS);
    memAccessMap["b.eq"] = 0;
    memLengthMap["b.eq"] = 0;
    bytesMap["b.eq"] = 4;

    opcodeToTypeMap["b.ne"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["b.ne"], "", MAX_OPERANDS);
    memAccessMap["b.ne"] = 0;
    memLengthMap["b.ne"] = 0;
    bytesMap["b.ne"] = 4;

    opcodeToTypeMap["b.ls"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["b.ls"], "", MAX_OPERANDS);
    memAccessMap["b.ls"] = 0;
    memLengthMap["b.ls"] = 0;
    bytesMap["b.ls"] = 4;

    opcodeToTypeMap["b.hi"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["b.hi"], "", MAX_OPERANDS);
    memAccessMap["b.hi"] = 0;
    memLengthMap["b.hi"] = 0;
    bytesMap["b.hi"] = 4;

    opcodeToTypeMap["b.cc"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["b.cc"], "", MAX_OPERANDS);
    memAccessMap["b.cc"] = 0;
    memLengthMap["b.cc"] = 0;
    bytesMap["b.cc"] = 4;

    opcodeToTypeMap["b.lo"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["b.lo"], "", MAX_OPERANDS);
    memAccessMap["b.lo"] = 0;
    memLengthMap["b.lo"] = 0;
    bytesMap["b.lo"] = 4;

    opcodeToTypeMap["cbz"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["cbz"], "R", MAX_OPERANDS);
    memAccessMap["cbz"] = 0;
    memLengthMap["cbz"] = 0;
    bytesMap["cbz"] = 4;

    opcodeToTypeMap["cbnz"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["cbnz"], "R", MAX_OPERANDS);
    memAccessMap["cbnz"] = 0;
    memLengthMap["cbnz"] = 0;
    bytesMap["cbnz"] = 4;

    opcodeToTypeMap["tbz"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["tbz"], "R", MAX_OPERANDS);
    memAccessMap["tbz"] = 0;
    memLengthMap["tbz"] = 0;
    bytesMap["tbz"] = 4;

    opcodeToTypeMap["tbnz"] = ExecutionType::BranchCond;
    strncpy(syntaxMap["tbnz"], "R", MAX_OPERANDS);
    memAccessMap["tbnz"] = 0;
    memLengthMap["tbnz"] = 0;
    bytesMap["tbnz"] = 4;

    opcodeToTypeMap["br"] = ExecutionType::BranchUncond;
    strncpy(syntaxMap["br"], "R", MAX_OPERANDS);
    memAccessMap["br"] = 0;
    memLengthMap["br"] = 0;
    bytesMap["br"] = 4;

    opcodeToTypeMap["b"] = ExecutionType::BranchUncond;
    strncpy(syntaxMap["b"], "", MAX_OPERANDS);
    memAccessMap["b"] = 0;
    memLengthMap["b"] = 0;
    bytesMap["b"] = 4;

    opcodeToTypeMap["bl"] = ExecutionType::BranchUncond;
    strncpy(syntaxMap["bl"], "", MAX_OPERANDS);
    memAccessMap["bl"] = 0;
    memLengthMap["bl"] = 0;
    bytesMap["bl"] = 4;

    opcodeToTypeMap["ret"] = ExecutionType::BranchUncond;
    strncpy(syntaxMap["ret"], "", MAX_OPERANDS);
    memAccessMap["ret"] = 0;
    memLengthMap["ret"] = 0;
    bytesMap["ret"] = 4;

    // NOTE: Be careful about the format of the disassembled instruction
    opcodeToTypeMap["ecall"] = ExecutionType::Syscall;
    strncpy(syntaxMap["ecall"], "", MAX_OPERANDS);
    memAccessMap["ecall"] = 0;
    memLengthMap["ecall"] = 0;
    bytesMap["ecall"] = 4;

    // NOTE: How is the CSR register shown in the disassembled instruction?
    opcodeToTypeMap["csrrwi"] = ExecutionType::Other;
    strncpy(syntaxMap["csrrwi"], "WR", MAX_OPERANDS);
    memAccessMap["csrrwi"] = 0;
    memLengthMap["csrrwi"] = 0;
    bytesMap["csrrwi"] = 4;
    
}
