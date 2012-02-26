// Copyright (c) 2012, Sergey Zolotarev
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// // LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "jit.h"

#if defined _MSC_VER
	#define COMPILER_MSVC
#elif defined __GNUC__
	#define COMPILER_GCC
#else
	#error Unsupported compiler
#endif

#if defined COMPILER_MSVC
	#if !defined CDECL
		#define CDECL __cdecl
	#endif
	#if !defined STDCALL
		#define STDCALL __stdcall
	#endif
#elif defined COMPILER_GCC
	#if !defined CDECL
		#define CDECL __attribute__((cdecl))
	#endif
	#if !defined STDCALL
		#define STDCALL __attribute__((stdcall));
	#endif
#endif

// Opcode list from amx.c.
enum AmxOpcode {
	OP_NONE, /* invalid opcode */
	OP_LOAD_PRI,
	OP_LOAD_ALT,
	OP_LOAD_S_PRI,
	OP_LOAD_S_ALT,
	OP_LREF_PRI,
	OP_LREF_ALT,
	OP_LREF_S_PRI,
	OP_LREF_S_ALT,
	OP_LOAD_I,
	OP_LODB_I,
	OP_CONST_PRI,
	OP_CONST_ALT,
	OP_ADDR_PRI,
	OP_ADDR_ALT,
	OP_STOR_PRI,
	OP_STOR_ALT,
	OP_STOR_S_PRI,
	OP_STOR_S_ALT,
	OP_SREF_PRI,
	OP_SREF_ALT,
	OP_SREF_S_PRI,
	OP_SREF_S_ALT,
	OP_STOR_I,
	OP_STRB_I,
	OP_LIDX,
	OP_LIDX_B,
	OP_IDXADDR,
	OP_IDXADDR_B,
	OP_ALIGN_PRI,
	OP_ALIGN_ALT,
	OP_LCTRL,
	OP_SCTRL,
	OP_MOVE_PRI,
	OP_MOVE_ALT,
	OP_XCHG,
	OP_PUSH_PRI,
	OP_PUSH_ALT,
	OP_PUSH_R,
	OP_PUSH_C,
	OP_PUSH,
	OP_PUSH_S,
	OP_POP_PRI,
	OP_POP_ALT,
	OP_STACK,
	OP_HEAP,
	OP_PROC,
	OP_RET,
	OP_RETN,
	OP_CALL,
	OP_CALL_PRI,
	OP_JUMP,
	OP_JREL,
	OP_JZER,
	OP_JNZ,
	OP_JEQ,
	OP_JNEQ,
	OP_JLESS,
	OP_JLEQ,
	OP_JGRTR,
	OP_JGEQ,
	OP_JSLESS,
	OP_JSLEQ,
	OP_JSGRTR,
	OP_JSGEQ,
	OP_SHL,
	OP_SHR,
	OP_SSHR,
	OP_SHL_C_PRI,
	OP_SHL_C_ALT,
	OP_SHR_C_PRI,
	OP_SHR_C_ALT,
	OP_SMUL,
	OP_SDIV,
	OP_SDIV_ALT,
	OP_UMUL,
	OP_UDIV,
	OP_UDIV_ALT,
	OP_ADD,
	OP_SUB,
	OP_SUB_ALT,
	OP_AND,
	OP_OR,
	OP_XOR,
	OP_NOT,
	OP_NEG,
	OP_INVERT,
	OP_ADD_C,
	OP_SMUL_C,
	OP_ZERO_PRI,
	OP_ZERO_ALT,
	OP_ZERO,
	OP_ZERO_S,
	OP_SIGN_PRI,
	OP_SIGN_ALT,
	OP_EQ,
	OP_NEQ,
	OP_LESS,
	OP_LEQ,
	OP_GRTR,
	OP_GEQ,
	OP_SLESS,
	OP_SLEQ,
	OP_SGRTR,
	OP_SGEQ,
	OP_EQ_C_PRI,
	OP_EQ_C_ALT,
	OP_INC_PRI,
	OP_INC_ALT,
	OP_INC,
	OP_INC_S,
	OP_INC_I,
	OP_DEC_PRI,
	OP_DEC_ALT,
	OP_DEC,
	OP_DEC_S,
	OP_DEC_I,
	OP_MOVS,
	OP_CMPS,
	OP_FILL,
	OP_HALT,
	OP_BOUNDS,
	OP_SYSREQ_PRI,
	OP_SYSREQ_C,
	OP_FILE, /* obsolete */
	OP_LINE, /* obsolete */
	OP_SYMBOL, /* obsolete */
	OP_SRANGE, /* obsolete */
	OP_JUMP_PRI,
	OP_SWITCH,
	OP_CASETBL,
	OP_SWAP_PRI,
	OP_SWAP_ALT,
	OP_PUSH_ADR,
	OP_NOP,
	OP_SYSREQ_D,
	OP_SYMTAG, /* obsolete */
	OP_BREAK,
};

static ucell GetPublicAddress(AMX *amx, cell index) {
	AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);

	AMX_FUNCSTUBNT *publics = reinterpret_cast<AMX_FUNCSTUBNT*>(amx->base + hdr->publics);
	int num_publics = (hdr->natives - hdr->publics) / hdr->defsize;

	if (index == -1) {
		return hdr->cip;
	}

	if (index < 0 || index >= num_publics) {
		return 0;
	}

	return publics[index].address;
}

static ucell GetNativeAddress(AMX *amx, cell index) {
	AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);

	AMX_FUNCSTUBNT *natives = reinterpret_cast<AMX_FUNCSTUBNT*>(amx->base + hdr->natives);
	int num_natives = (hdr->libraries - hdr->natives) / hdr->defsize;

	if (index < 0 || index >= num_natives) {
		return 0;
	}

	return natives[index].address;
}

JITFunction::JITFunction(JIT *jitter, ucell address)
	: jitasm::function<void, JITFunction>()
	, jit_(jitter)
	, address_(address)
{
}

void JITFunction::main() {
	bool seen_proc = false; // whether we've seen the PROC opcode

	AMX *amx = jit_->GetAmx();
	AMX_HEADER *amxhdr = jit_->GetAmxHeader();

	cell data = reinterpret_cast<cell>(jit_->GetAmxData());
	cell code = reinterpret_cast<cell>(jit_->GetAmxCode());

	cell code_size = data - code;
	cell *cip = reinterpret_cast<cell*>(code + address_);

	while (cip < reinterpret_cast<cell*>(data)) {
		AmxOpcode opcode = static_cast<AmxOpcode>(*cip++);
		cell oper = *cip;

		// Label this instruction so we can refer to it when
		// doing jumps...
		SetLabel(cell(cip) - code - sizeof(cell));

		switch (opcode) {
		case OP_LOAD_PRI: // address
			// PRI = [address]
			mov(eax, dword_ptr[data + oper]);
			cip++;
			break;
		case OP_LOAD_ALT: // address
			// PRI = [address]
			mov(ecx, dword_ptr[data + oper]);
			cip++;
			break;
		case OP_LOAD_S_PRI: // offset
			// PRI = [FRM + offset]
			mov(eax, dword_ptr[ebp + oper]);
			cip++;
			break;
		case OP_LOAD_S_ALT: // offset
			// ALT = [FRM + offset]
			mov(ecx, dword_ptr[ebp + oper]);
			cip++;
			break;
		case OP_LREF_PRI: // address
			// PRI = [ [address] ]
			mov(edx, dword_ptr[data + oper]);
			mov(eax, dword_ptr[edx + data]);
			cip++;
			break;
		case OP_LREF_ALT: // address
			// ALT = [ [address] ]
			mov(edx, dword_ptr[data + oper]);
			mov(ecx, dword_ptr[edx + data]);
			cip++;
			break;
		case OP_LREF_S_PRI: // offset
			// PRI = [ [FRM + offset] ]
			mov(edx, dword_ptr[ebp + oper]);
			mov(eax, dword_ptr[edx + data]);
			cip++;
			break;
		case OP_LREF_S_ALT: // offset
			// PRI = [ [FRM + offset] ]
			mov(edx, dword_ptr[ebp + oper]);
			mov(ecx, dword_ptr[edx + data]);
			cip++;
			break;
		case OP_LOAD_I:
			// PRI = [PRI] (full cell)
			mov(eax, dword_ptr[eax + data]);
			break;
		case OP_LODB_I: // number
			// PRI = �number� bytes from [PRI] (read 1/2/4 bytes)
			switch (oper) {
			case 1:
				xor(eax, eax);
				mov(al, byte_ptr[eax + data]);
			case 2:
				xor(eax, eax);
				mov(ax, word_ptr[eax + data]);
			default:
				mov(eax, dword_ptr[eax + data]);
			}
			cip++;
			break;
		case OP_CONST_PRI: // value
			// PRI = value
			mov(eax, oper);
			cip++;
			break;
		case OP_CONST_ALT: // value
			// ALT = value
			mov(ecx, oper);
			cip++;
			break;
		case OP_ADDR_PRI: // offset
			// PRI = FRM + offset
			lea(eax, dword_ptr[oper - data + ebp]);
			cip++;
			break;
		case OP_ADDR_ALT: // offset
			// ALT = FRM + offset
			lea(ecx, dword_ptr[oper - data + ebp]);
			cip++;
			break;
		case OP_STOR_PRI: // address
			// [address] = PRI
			mov(dword_ptr[data + oper], eax);
			cip++;
			break;
		case OP_STOR_ALT: // address
			// [address] = ALT
			mov(dword_ptr[data + oper], ecx);
			cip++;
			break;
		case OP_STOR_S_PRI: // offset
			// [FRM + offset] = ALT
			mov(dword_ptr[ebp + oper], eax);
			cip++;
			break;
		case OP_STOR_S_ALT: // offset
			// [FRM + offset] = ALT
			mov(dword_ptr[ebp + oper], ecx);
			cip++;
			break;
		case OP_SREF_PRI: // address
			// [ [address] ] = PRI
			mov(edx, dword_ptr[data + oper]);
			mov(dword_ptr[edx + data], eax);
			cip++;
			break;
		case OP_SREF_ALT: // address
			// [ [address] ] = ALT
			mov(edx, dword_ptr[data + oper]);
			mov(dword_ptr[edx + data], ecx);
			cip++;
			break;
		case OP_SREF_S_PRI: // offset
			// [ [FRM + offset] ] = PRI
			mov(edx, dword_ptr[ebp + oper]);
			mov(dword_ptr[edx + data], eax);
			cip++;
			break;
		case OP_SREF_S_ALT: // offset
			// [ [FRM + offset] ] = ALT
			mov(edx, dword_ptr[ebp + oper]);
			mov(dword_ptr[edx + data], ecx);
			cip++;
			break;
		case OP_STOR_I:
			// [ALT] = PRI (full cell)
			mov(dword_ptr[ecx + data], eax);
			break;
		case OP_STRB_I: // number
			// �number� bytes at [ALT] = PRI (write 1/2/4 bytes)
			switch (oper) {
			case 1:
				xor(ecx, ecx);
				mov(byte_ptr[ecx + data], al);
			case 2:
				xor(ecx, ecx);
				mov(word_ptr[ecx + data], ax);
			default:
				mov(dword_ptr[ecx + data], eax);
			}
			cip++;
			break;
		case OP_LIDX:
			// PRI = [ ALT + (PRI x cell size) ]
			mov(eax, dword_ptr[ecx + (eax * sizeof(cell)) + data]);
			break;
		case OP_LIDX_B: // shift
			// PRI = [ ALT + (PRI << shift) ]
			mov(edx, eax);
			shl(edx, static_cast<unsigned char>(oper));
			mov(eax, dword_ptr[ecx + edx + data]);
			cip++;
			break;
		case OP_IDXADDR:
			// PRI = ALT + (PRI x cell size) (calculate indexed address)
			lea(eax, dword_ptr[ecx + (eax * sizeof(cell))]);
			break;
		case OP_IDXADDR_B: // shift
			// PRI = ALT + (PRI << shift) (calculate indexed address)
			shl(eax, static_cast<unsigned char>(oper));
			lea(eax, dword_ptr[ecx + eax]);
			cip++;
			break;
		case OP_ALIGN_PRI: // number
			// Little Endian: PRI ^= cell size - number
			xor(eax, sizeof(cell) - oper);
			cip++;
			break;
		case OP_ALIGN_ALT: // number
			// Little Endian: ALT ^= cell size - number
			xor(ecx, sizeof(cell) - oper);
			cip++;
			break;
		case OP_LCTRL: // index
			// PRI is set to the current value of any of the special registers.
			// The index parameter must be: 0=COD, 1=DAT, 2=HEA,
			// 3=STP, 4=STK, 5=FRM, 6=CIP (of the next instruction)
			switch (oper) {
			case 0:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amxhdr->cod)]);
				break;
			case 1:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amxhdr->dat)]);
				break;
			case 2:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amxhdr->hea)]);
				break;
			case 3:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amx->stp)]);
				break;
			case 4:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amx->stk)]);
				break;
			case 5:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amx->frm)]);
				break;
			case 6:
				mov(eax, dword_ptr[reinterpret_cast<int>(&amxhdr->cip)]);
				break;
			}
			cip++;
			break;
		case OP_SCTRL: // index
			// set the indexed special registers to the value in PRI.
			// The index parameter must be: 2=HEA, 4=STK, 5=FRM,
			// 6=CIP
			switch (oper) {
			case 2:
				mov(dword_ptr[reinterpret_cast<int>(&amxhdr->hea)], eax);
				break;
			case 4:
				mov(dword_ptr[reinterpret_cast<int>(&amx->stk)], eax);
				break;
			case 5:
				mov(dword_ptr[reinterpret_cast<int>(&amx->frm)], eax);
				break;
			}
			cip++;
			break;
		case OP_MOVE_PRI:
			// PRI = ALT
			mov(eax, ecx);
			break;
		case OP_MOVE_ALT:
			// ALT = PRI
			mov(ecx, eax);
			break;
		case OP_XCHG:
			// Exchange PRI and ALT
			xchg(eax, ecx);
			break;
		case OP_PUSH_PRI:
			// [STK] = PRI, STK = STK - cell size
			push(eax);
			break;
		case OP_PUSH_ALT:
			// [STK] = ALT, STK = STK - cell size
			push(ecx);
			break;
		case OP_PUSH_R: // value
			// obsolete
			break;
		case OP_PUSH_C: // value
			// [STK] = value, STK = STK - cell size
			push(oper);
			cip++;
			break;
		case OP_PUSH: // address
			// [STK] = [address], STK = STK - cell size
			push(dword_ptr[oper + data]);
			cip++;
			break;
		case OP_PUSH_S: // offset
			// [STK] = [FRM + offset], STK = STK - cell size
			push(dword_ptr[ebp + oper]);
			cip++;
			break;
		case OP_POP_PRI:
			// STK = STK + cell size, PRI = [STK]
			pop(eax);
			break;
		case OP_POP_ALT:
			// STK = STK + cell size, ALT = [STK]
			pop(ecx);
			break;
		case OP_STACK: // value
			// ALT = STK, STK = STK + value
			lea(ecx, dword_ptr[esp - data]);
			add(esp, oper);
			cip++;
			break;
		case OP_HEAP: // value
			// ALT = HEA, HEA = HEA + value
			mov(ecx, dword_ptr[reinterpret_cast<int>(&amx->hea)]);
			add(dword_ptr[reinterpret_cast<int>(&amx->hea)], oper);
			cip++;
			break;
		case OP_PROC:
			// [STK] = FRM, STK = STK - cell size, FRM = STK
			if (seen_proc) return; // next function begins here
			push(ebp);
			mov(ebp, esp);
			seen_proc = true;
			break;
		case OP_RET:
			// STK = STK + cell size, FRM = [STK],
			// CIP = [STK], STK = STK + cell size
			pop(ebp);
			ret();
			break;
		case OP_RETN:
			// FRM = [STK], STK = STK + cell size,
			// CIP = [STK], STK = STK + cell size,
			// STK = STK + [STK]
			// The RETN instruction removes a specified number of bytes
			// from the stack. The value to adjust STK with must be
			// pushed prior to the call.
			pop(ebp);
			ret();
			break;
		case OP_CALL: // offset
			// [STK] = CIP + 5, STK = STK - cell size
			// CIP = CIP + offset
			// The CALL instruction jumps to an address after storing the
			// address of the next sequential instruction on the stack.
			// The address jumped to is relative to the current CIP,
			// but the address on the stack is an absolute address.
			mov(edx, reinterpret_cast<int>(jit_->GetFunction(oper - code)->GetCode()));
			call(edx);
			add(esp, dword_ptr[esp]);
			add(esp, 4);
			cip++;
			break;
		case OP_CALL_PRI:
			// obsolete
			break;
		case OP_JUMP: // offset
			// CIP = CIP + offset (jump to the address relative from
			// the current position)
			jmp(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JREL: // offset
			// obsolete
			break;
		case OP_JZER: // offset
			// if PRI == 0 then CIP = CIP + offset
			cmp(eax, 0);
			jz(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JNZ: // offset
			// if PRI != 0 then CIP = CIP + offset
			cmp(eax, 0);
			jnz(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JEQ: // offset
			// if PRI == ALT then CIP = CIP + offset
			cmp(eax, ecx);
			je(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JNEQ: // offset
			// if PRI != ALT then CIP = CIP + offset
			cmp(eax, ecx);
			jne(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JLESS: // offset
			// if PRI < ALT then CIP = CIP + offset (unsigned)
			cmp(eax, ecx);
			jb(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JLEQ: // offset
			// if PRI <= ALT then CIP = CIP + offset (unsigned)
			cmp(eax, ecx);
			jbe(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JGRTR: // offset
			// if PRI > ALT then CIP = CIP + offset (unsigned)
			cmp(eax, ecx);
			ja(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JGEQ: // offset
			// if PRI >= ALT then CIP = CIP + offset (unsigned)
			cmp(eax, ecx);
			jae(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JSLESS: // offset
			// if PRI < ALT then CIP = CIP + offset (signed)
			cmp(eax, ecx);
			jl(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JSLEQ: // offset
			// if PRI <= ALT then CIP = CIP + offset (signed)
			cmp(eax, ecx);
			jle(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JSGRTR: // offset
			// if PRI > ALT then CIP = CIP + offset (signed)
			cmp(eax, ecx);
			jg(GetLabelName(oper - code));
			cip++;
			break;
		case OP_JSGEQ: // offset
			// if PRI >= ALT then CIP = CIP + offset (signed)
			cmp(eax, ecx);
			jge(GetLabelName(oper - code));
			cip++;
			break;
		case OP_SHL:
			// PRI = PRI << ALT
			shl(eax, bl);
			break;
		case OP_SHR:
			// PRI = PRI >> ALT (without sign extension)
			shr(eax, bl);
			break;
		case OP_SSHR:
			// PRI = PRI >> ALT with sign extension
			sar(eax, bl);
			break;
		case OP_SHL_C_PRI: // value
			// PRI = PRI << value
			shl(eax, static_cast<unsigned char>(oper));
			cip++;
			break;
		case OP_SHL_C_ALT: // value
			// ALT = ALT << value
			shl(ecx, static_cast<unsigned char>(oper));
			cip++;
			break;
		case OP_SHR_C_PRI: // value
			// PRI = PRI >> value (without sign extension)
			shr(eax, static_cast<unsigned char>(oper));
			cip++;
			break;
		case OP_SHR_C_ALT: // value
			// PRI = PRI >> value (without sign extension)
			shl(ecx, static_cast<unsigned char>(oper));
			cip++;
			break;
		case OP_SMUL:
			// PRI = PRI * ALT (signed multiply)
			xor(edx, edx);
			imul(ecx);
			break;
		case OP_SDIV:
			// PRI = PRI / ALT (signed divide), ALT = PRI mod ALT
			xor(edx, edx);
			idiv(ecx);
			mov(ecx, edx);
			break;
		case OP_SDIV_ALT:
			// PRI = ALT / PRI (signed divide), ALT = ALT mod PRI
			xchg(eax, ecx);
			xor(edx, edx);
			idiv(ecx);
			mov(ecx, edx);
			break;
		case OP_UMUL:
			// PRI = PRI * ALT (unsigned multiply)
			xor(edx, edx);
			mul(ecx);
			break;
		case OP_UDIV:
			// PRI = PRI / ALT (unsigned divide), ALT = PRI mod ALT
			xor(edx, edx);
			div(ecx);
			mov(ecx, edx);
			break;
		case OP_UDIV_ALT:
			// PRI = ALT / PRI (unsigned divide), ALT = ALT mod PRI
			xchg(eax, ecx);
			xor(edx, edx);
			div(ecx);
			mov(ecx, edx);
			break;
		case OP_ADD:
			// PRI = PRI + ALT
			add(eax, ecx);
			break;
		case OP_SUB:
			// PRI = PRI - ALT
			sub(eax, ecx);
			break;
		case OP_SUB_ALT:
			// PRI = ALT - PRI
			// or:
			// PRI = -(PRI - ALT)
			sub(eax, ecx);
			neg(eax);
			break;
		case OP_AND:
			// PRI = PRI & ALT
			and(eax, ecx);
			break;
		case OP_OR:
			// PRI = PRI | ALT
			or(eax, ecx);
			break;
		case OP_XOR:
			// PRI = PRI ^ ALT
			xor(eax, ecx);
			break;
		case OP_NOT:
			// PRI = !PRI
			test(eax, eax);
			setz(cl);
			movzx(eax, cl);
			break;
		case OP_NEG:
			// PRI = -PRI
			neg(eax);
			break;
		case OP_INVERT:
			// PRI = ~PRI
			not(eax);
			break;
		case OP_ADD_C: // value
			// PRI = PRI + value
			add(eax, oper);
			cip++;
			break;
		case OP_SMUL_C: // value
			// PRI = PRI * value
			imul(eax, oper);
			cip++;
			break;
		case OP_ZERO_PRI:
			// PRI = 0
			xor(eax, eax);
			break;
		case OP_ZERO_ALT:
			// ALT = 0
			xor(ecx, ecx);
			break;
		case OP_ZERO: // address
			// [address] = 0
			mov(dword_ptr[oper + data], 0);
			cip++;
			break;
		case OP_ZERO_S: // offset
			// [FRM + offset] = 0
			mov(dword_ptr[ebp + oper], 0);
			cip++;
			break;
		case OP_SIGN_PRI:
			// sign extent the byte in PRI to a cell
			movsx(eax, al);
			break;
		case OP_SIGN_ALT:
			// sign extent the byte in ALT to a cell
			movsx(ecx, bl);
			break;
		case OP_EQ:
			// PRI = PRI == ALT ? 1 : 0
			cmp(eax, ecx);
			sete(al);
			movzx(eax, al);
			break;
		case OP_NEQ:
			// PRI = PRI != ALT ? 1 : 0
			cmp(eax, ecx);
			setne(al);
			movzx(eax, al);
			break;
		case OP_LESS:
			// PRI = PRI < ALT ? 1 : 0 (unsigned)
			cmp(eax, ecx);
			setb(al);
			movzx(eax, al);
			break;
		case OP_LEQ:
			// PRI = PRI <= ALT ? 1 : 0 (unsigned)
			cmp(eax, ecx);
			setbe(al);
			movzx(eax, al);
			break;
		case OP_GRTR:
			// PRI = PRI > ALT ? 1 : 0 (unsigned)
			cmp(eax, ecx);
			seta(al);
			movzx(eax, al);
			break;
		case OP_GEQ:
			// PRI = PRI >= ALT ? 1 : 0 (unsigned)
			cmp(eax, ecx);
			setae(al);
			movzx(eax, al);
			break;
		case OP_SLESS:
			// PRI = PRI < ALT ? 1 : 0 (signed)
			cmp(eax, ecx);
			setl(al);
			movzx(eax, al);
			break;
		case OP_SLEQ:
			// PRI = PRI <= ALT ? 1 : 0 (signed)
			cmp(eax, ecx);
			setle(al);
			movzx(eax, al);
			break;
		case OP_SGRTR:
			// PRI = PRI > ALT ? 1 : 0 (signed)
			cmp(eax, ecx);
			setg(al);
			movzx(eax, al);
			break;
		case OP_SGEQ:
			// PRI = PRI >= ALT ? 1 : 0 (signed)
			cmp(eax, ecx);
			setge(al);
			movzx(eax, al);
			break;
		case OP_EQ_C_PRI: // value
			// PRI = PRI == value ? 1 : 0
			cmp(eax, oper);
			sete(al);
			movzx(eax, al);
			cip++;
			break;
		case OP_EQ_C_ALT: // value
			// PRI = ALT == value ? 1 : 0
			cmp(ecx, oper);
			sete(al);
			movzx(eax, al);
			cip++;
			break;
		case OP_INC_PRI:
			// PRI = PRI + 1
			inc(eax);
			break;
		case OP_INC_ALT:
			// ALT = ALT + 1
			inc(ecx);
			break;
		case OP_INC: // address
			// [address] = [address] + 1
			inc(dword_ptr[data + oper]);
			cip++;
			break;
		case OP_INC_S: // offset
			// [FRM + offset] = [FRM + offset] + 1
			inc(dword_ptr[ebp + oper]);
			cip++;
			break;
		case OP_INC_I:
			// [PRI] = [PRI] + 1
			inc(dword_ptr[eax + data]);
			break;
		case OP_DEC_PRI:
			// PRI = PRI - 1
			dec(eax);
			break;
		case OP_DEC_ALT:
			// ALT = ALT - 1
			dec(ecx);
			break;
		case OP_DEC: // address
			// [address] = [address] - 1
			dec(dword_ptr[data + oper]);
			cip++;
			break;
		case OP_DEC_S: // offset
			// [FRM + offset] = [FRM + offset] - 1
			dec(dword_ptr[ebp + oper]);
			cip++;
			break;
		case OP_DEC_I:
			// [PRI] = [PRI] - 1
			dec(dword_ptr[eax + data]);
			break;
		case OP_MOVS: // number
			// Copy memory from [PRI] to [ALT]. The parameter
			// specifies the number of bytes. The blocks should not
			// overlap.
			lea(esi, dword_ptr[data + eax]);
			lea(edi, dword_ptr[data + ecx]);
			push(ecx);
			if (oper % 4 == 0) {
				mov(ecx, oper / 4);
				rep_movsd(edi, esi, ecx);
			} else if (oper % 2 == 0) {
				mov(ecx, oper / 2);
				rep_movsw(edi, esi, ecx);
			} else {
				mov(ecx, oper);
				rep_movsb(edi, esi, ecx); 
			}
			pop(ecx);
			cip++;
			break;
		case OP_CMPS: // number
			// Compare memory blocks at [PRI] and [ALT]. The parameter
			// specifies the number of bytes. The blocks should not
			// overlap.
			lea(esi, dword_ptr[data + eax]);
			lea(edi, dword_ptr[data + ecx]);
			push(ecx);
			mov(ecx, oper);
			rep_cmpsb(edi, esi, ecx);
			pop(ecx);
			cip++;
			break;
		case OP_FILL: // number
			// Fill memory at [ALT] with value in [PRI]. The parameter
			// specifies the number of bytes, which must be a multiple
			// of the cell size.
			push(oper);                    // push "number"
			push(eax);                     // push "value"
			lea(edx, dword_ptr[ecx + data]);
			push(edx);                     // push "ptr"
			mov(edx, cell(std::memset));
			call(edx);                     // memcmp(ptr, value, number)
			add(esp, 12);
			cip++;
			break;
		case OP_HALT: // number
			// Abort execution (exit value in PRI), parameters other than 0
			// have a special meaning.
			cip++;
			break;
		case OP_BOUNDS: // value
			// Abort execution if PRI > value or if PRI < 0.
			cip++;
			break;
		case OP_SYSREQ_PRI:
			// call system service, service number in PRI
			cip++;
			break;
		case OP_SYSREQ_C: // index
		case OP_SYSREQ_D: // address
			// call system service
			push(esp);
			push(reinterpret_cast<int>(amx));
			switch (opcode) {
			case OP_SYSREQ_C:
				mov(edx, GetNativeAddress(amx, oper));
				break;
			case OP_SYSREQ_D:
				mov(edx, oper);
				break;
			}
			call(edx);
			add(esp, 8);
			cip++;
			break;
		case OP_FILE: // size ord name
			// obsolete
			break;
		case OP_LINE: // line ord
			// obsolete
			break;
		case OP_SYMBOL: // size offset flag name
			// obsolete
			break;
		case OP_SRANGE: // level size
			// obsolete
			break;
		case OP_JUMP_PRI:
			// obsolete
			break;
		case OP_SWITCH: { // offset
			// Compare PRI to the values in the case table (whose address
			// is passed as an offset from CIP) and jump to the associated
			// the address in the matching record.

			struct case_record {
				cell value;    // case value
				cell address;  // address to jump to (absolute)
			} *case_table;

			// Get pointer to the start of the case table.
			case_table = reinterpret_cast<case_record*>(oper + sizeof(cell));

			// The number of cases follows the CASETBL opcode (which follows the SWITCH).
			int num_cases = *(reinterpret_cast<cell*>(oper) + 1);

			// Get minimum and maximum values.
			cell *min_value = 0;
			cell *max_value = 0;
			for (int i = 0; i < num_cases; i++) {
				cell *value = &case_table[i + 1].value;
				if (min_value == 0 || *value < *min_value) {
					min_value = value;
				}
				if (max_value == 0 || *value > *max_value) {
					max_value = value;
				}
			}

			// Get address of the "default" record.
			cell default_addr = case_table[0].address - code;

			// Check if the value in eax is in the allowed range.
			// If not, jump to the default case (i.e. no match).
			cmp(eax, *min_value);
			jl(GetLabelName(default_addr));
			cmp(eax, *max_value);
			jg(GetLabelName(default_addr));

			// OK now sequentially compare eax with each value.
			// This is pretty slow so I probably should optimize
			// this in future...
			for (int i = 0; i < num_cases; i++) {
				cmp(eax, case_table[i + 1].value);
				je(GetLabelName(case_table[i + 1].address - code));
			}

			// No match found - go for default case.
			jmp(GetLabelName(default_addr));

			cip++;
			break;
		}
		case OP_CASETBL: // ...
			// A variable number of case records follows this opcode, where
			// each record takes two cells.
			cip += 2 * oper + 2;
			break;
		case OP_SWAP_PRI:
			// [STK] = PRI and PRI = [STK]
			xchg(dword_ptr[esp], eax);
			break;
		case OP_SWAP_ALT:
			// [STK] = ALT and ALT = [STK]
			xchg(dword_ptr[esp], ecx);
			break;
		case OP_PUSH_ADR: // offset
			// [STK] = FRM + offset, STK = STK - cell size
			lea(edx, dword_ptr[oper - data + ebp]);
			push(edx);
			cip++;
			break;
		case OP_NOP:
			// no-operation, for code alignment
			break;
		case OP_SYMTAG: // value
			// obsolete
			break;
		case OP_BREAK:
			// conditional breakpoint
			break;
		default:
			assert(0);
		}
	}
}

void JITFunction::SetLabel(cell address, const std::string &tag) {
	L(GetLabelName(address));
}

std::string JITFunction::GetLabelName(cell address, const std::string &tag) const {
	std::stringstream ss;
	ss << address << tag;
	return ss.str();
}

JIT::JIT(AMX *amx)
	: amx_(amx)
	, amxhdr_(reinterpret_cast<AMX_HEADER*>(amx_->base))
	, data_(amx_->data != 0 ? amx_->data : amx_->base + amxhdr_->dat)
	, code_(amx_->base + amxhdr_->cod)
{
}

JIT::~JIT() {
	for (ProcMap::const_iterator iterator = proc_map_.begin();
			iterator != proc_map_.end(); ++iterator) {
		delete iterator->second;
	}
}

JITFunction *JIT::GetFunction(ucell address) {
	ProcMap::const_iterator iterator = proc_map_.find(address);
	if (iterator != proc_map_.end()) {
		return iterator->second;
	} else {
		JITFunction *fn = AssembleFunction(address);
		proc_map_.insert(std::make_pair(address, fn)).second;
		return fn;
	}
}

JITFunction *JIT::AssembleFunction(ucell address) {
	JITFunction *fn = new JITFunction(this, address);
	fn->Assemble();
	return fn;
}

int JIT::CallPublicFunction(int index, cell *retval) {
	// Some instructions may set a non-zero error code to indicate
	// that a runtime error occured (e.g. array index out of bounds).
	amx_->error = AMX_ERR_NONE;

	// paramcount is the number of arguments passed to the public.
	int paramcount = amx_->paramcount;
	int parambytes = paramcount * sizeof(cell);

	ucell address = GetPublicAddress(amx_, index);
	if (address == 0) {
		amx_->error = AMX_ERR_INDEX;
	} else {
		// Get pointer to native code buffer.
		void *start = GetFunction(address)->GetCode();

		// Copy parameters from AMX stack and call the function.
		cell *args = reinterpret_cast<cell*>(data_ + amx_->stk);
		#if defined COMPILER_MSVC
			__asm push esi
			__asm push edi
			for (int i = paramcount - 1; i >= 0; --i) {
				__asm mov eax, dword ptr [i]
				__asm mov ecx, dword ptr [args]
				__asm push dword ptr [ecx + eax * 4]
			}
			__asm push dword ptr [parambytes]
			__asm call dword ptr [start]
			__asm add esp, dword ptr [parambytes]
			__asm add esp, 4
			__asm pop edi
			__asm pop esi
		#elif defined COMPILER_GCC
			__asm__ __volatile__ (
				"pushl %%esi\n"
				"pushl %%edi\n"
				: : : );
			for (int i = paramcount - 1; i >= 0; --i) {
				__asm__ __volatile__ (
					"pushl %0"
					: : "r"(args[i]) : );
			}
			__asm__ __volatile__ (
				"pushl %0\n"
				"calll *%1\n"
				"addl %0, %%esp\n"
				"addl $4, %%esp\n"
				"popl %%edi\n"
				"popl %%esi\n"
				: : "r"(parambytes), "r"(start) : );
		#endif
	}

	// Reset STK and parameter count.
	amx_->stk += parambytes;
	amx_->paramcount = 0;

	return amx_->error;
}

void JIT::DumpCode(std::ostream &stream) const {
	for (ProcMap::const_iterator iterator = proc_map_.begin();
			iterator != proc_map_.end(); ++iterator) {
		JITFunction *fn = iterator->second;
		stream.write(reinterpret_cast<char*>(fn->GetCode()), fn->GetCodeSize());
	}
}
