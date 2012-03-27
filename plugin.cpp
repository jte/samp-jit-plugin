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
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include "amxname.h"
#include "configreader.h"
#include "jit.h"
#include "jump-x86.h"
#include "plugin.h"
#include "pluginversion.h"

#ifdef WIN32
	#include <Windows.h>
#else
	#ifndef _GNU_SOURCE
		#define _GNU_SOURCE 1 // for dladdr()
	#endif
	#include <dlfcn.h> 
#endif

extern void *pAMXFunctions;

typedef void (*logprintf_t)(const char *format, ...);
static logprintf_t logprintf;

static std::map<AMX*, jit::Jitter*> jitters;

static JumpX86 amx_Exec_hook;
static JumpX86 amx_GetAddr_hook;

static cell *opcode_list = 0;

static ConfigReader server_cfg("server.cfg");

static int AMXAPI amx_GetAddr_JIT(AMX *amx, cell amx_addr, cell **phys_addr) {
	AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);
	*phys_addr = reinterpret_cast<cell*>(amx->base + hdr->dat + amx_addr);
	return AMX_ERR_NONE;
}

static int AMXAPI amx_Exec_JIT(AMX *amx, cell *retval, int index) {
	#if defined __GNUC__
		if ((amx->flags & AMX_FLAG_BROWSE) == AMX_FLAG_BROWSE) {
			// amx_BrowseRelocate() wants the opcode list.
			assert(::opcode_list != 0);
			*retval = reinterpret_cast<cell>(::opcode_list);
			return AMX_ERR_NONE;
		}
	#endif	
	std::map<AMX*, jit::Jitter*>::iterator iterator = jitters.find(amx);
	if (iterator != jitters.end()) {
		return iterator->second->CallPublicFunction(index, retval);
	} else {
		JumpX86::ScopedRemove r(&amx_Exec_hook);
		return amx_Exec(amx, retval, index);
	}
}

static std::string GetModuleNameBySymbol(void *symbol) {
	char module[FILENAME_MAX] = "";
	if (symbol != 0) {
		#ifdef WIN32
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery(symbol, &mbi, sizeof(mbi));
			GetModuleFileName((HMODULE)mbi.AllocationBase, module, FILENAME_MAX);
		#else
			Dl_info info;
			dladdr(symbol, &info);
			strcpy(module, info.dli_fname);
		#endif
	}	
	return std::string(module);
}

static std::string GetFileName(const std::string &path) {
	std::string::size_type lastSep = path.find_last_of("/\\");
	if (lastSep != std::string::npos) {
		return path.substr(lastSep + 1);
	}
	return path;
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
	return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData) {
	logprintf = (logprintf_t)ppData[PLUGIN_DATA_LOGPRINTF];
	pAMXFunctions = reinterpret_cast<void*>(ppData[PLUGIN_DATA_AMX_EXPORTS]);
	
	void *funAddr = JumpX86::GetTargetAddress(((void**)pAMXFunctions)[PLUGIN_AMX_EXPORT_Exec]);
	if (funAddr != 0) {
		std::string module = GetFileName(GetModuleNameBySymbol(funAddr));
		if (!module.empty() && module != "samp-server.exe" && module != "samp03svr") {
			logprintf("  JIT must be loaded before %s", module.c_str());
			return false;
		}
	}

	std::size_t stack_size = server_cfg.GetOption("jit_stack", 0);
	if (stack_size != 0) {
		jit::Jitter::SetStackSize(stack_size);
	}

	logprintf("  JIT plugin v%s is OK.", PLUGIN_VERSION_STRING);
	return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
	for (std::map<AMX*, jit::Jitter*>::iterator it = jitters.begin(); it != jitters.end(); ++it) {
		delete it->second;
	}
}

static void dummy() {}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX *amx) {
	typedef int (AMXAPI *amx_Exec_t)(AMX *amx, cell *retval, int index);
	amx_Exec_t amx_Exec = (amx_Exec_t)((void**)pAMXFunctions)[PLUGIN_AMX_EXPORT_Exec];

	typedef int (AMXAPI *amx_GetAddr_t)(AMX *amx, cell amx_addr, cell **phys_addr);
	amx_GetAddr_t amx_GetAddr = (amx_GetAddr_t)((void**)pAMXFunctions)[PLUGIN_AMX_EXPORT_GetAddr];

	typedef uint16_t *(AMXAPI *amx_Align16_t)(uint16_t *v);
	((void**)pAMXFunctions)[PLUGIN_AMX_EXPORT_Align16] = (amx_Align16_t)dummy;

	typedef uint32_t *(AMXAPI *amx_Align32_t)(uint32_t *v);
	((void**)pAMXFunctions)[PLUGIN_AMX_EXPORT_Align32] = (amx_Align32_t)dummy;

	#if defined __GNUC__
		// Get opcode list before we hook amx_Exec().
		if (::opcode_list == 0) {
			amx->flags |= AMX_FLAG_BROWSE;
			amx_Exec(amx, reinterpret_cast<cell*>(&opcode_list), 0);
			amx->flags &= ~AMX_FLAG_BROWSE;
		}		
	#endif

	if (!amx_Exec_hook.IsInstalled()) {
		amx_Exec_hook.Install(
			(void*)amx_Exec,
			(void*)amx_Exec_JIT);
	}
	if (!amx_GetAddr_hook.IsInstalled()) {
		amx_GetAddr_hook.Install(
			(void*)amx_GetAddr,
			(void*)amx_GetAddr_JIT);
	}	

	try {
		// Prepare a file for assembly listing if "jit_listing" option is activated.		
		std::FILE *stream = 0;
		if (::server_cfg.GetOption("jit_listing", false)) {
			std::string amx_path = GetAmxName(amx);
			if (!amx_path.empty()) {
				std::string asm_path;
				std::string::size_type dot = amx_path.find_last_of(".");
				if (dot != std::string::npos 
						&& std::strcmp(amx_path.c_str() + dot, ".amx") == 0) {
					// Strip extension.
					asm_path.assign(amx_path.begin(), amx_path.begin() + dot);
				} else {
					asm_path.assign(amx_path);					
				}
				asm_path.append(".asm");
				stream = std::fopen(asm_path.c_str(), "w");
				if (stream != 0) {
					std::fprintf(stream, "; Assembly code generated from %s\n\n", amx_path.c_str());
				}
			}
		}		

		// Create a new Jitter instance and compile the script.
		jit::Jitter *jitter = new jit::Jitter(amx, ::opcode_list);
		jitters.insert(std::make_pair(amx, jitter));
		jitter->Compile(stream);

		// Close listing file.
		if (stream != 0) {
			std::fclose(stream);
		}
	} catch (const jit::JitError &) {
		try {
			logprintf("[jit] An error occured, this script will run without JIT!");
			throw;
		} catch (const jit::CompileError &e) {
			const jit::AmxInstruction &instr = e.GetInstruction();

			// Get instruction address.
			AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);
			cell address = reinterpret_cast<cell>(instr.GetIP()) 
						 - reinterpret_cast<cell>(amx->base + hdr->cod);
			try {
				throw;
			} catch (const jit::InvalidInstructionError &) {			
				logprintf("[jit] Error: Invalid instruction at address %08x:", address);					
			} catch (const jit::UnsupportedInstructionError &) {
				logprintf("[jit] Error: Unsupported instruction at address %08x:", address);
			} catch (const jit::ObsoleteInstructionError &) {
				logprintf("[jit] Error: Obsolete instruction at address %08x:", address);
			}

			// Print first few cells of the problem instruction for debugging.
			const cell *ip = instr.GetIP();
			logprintf("  %08x %08x %08x %08x %08x %08x ...", 
					*ip, *(ip + 1), *(ip + 2), *(ip + 3), *(ip + 4), *(ip + 5));
			return AMX_ERR_INVINSTR;
		}
	} catch (...) {
		logprintf("[jit] Error: Unknown error");
	}

	return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX *amx) {
	std::map<AMX*, jit::Jitter*>::iterator it = jitters.find(amx);
	if (it != jitters.end()) {
		delete it->second;
		jitters.erase(it);		
	}
	return AMX_ERR_NONE;
}
