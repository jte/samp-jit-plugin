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

#include <cstddef>
#include <map>

#include "jit.h"
#include "jump-x86.h"
#include "plugin.h"
#include "pluginversion.h"

using namespace jit;

typedef void (*logprintf_t)(const char *format, ...);

static logprintf_t logprintf;

static std::map<AMX*, JIT*> jits;

// This implementation of amx_GetAddr can accept ANY amx_addr, even out of the data section.
static int AMXAPI amx_GetAddr_JIT(AMX *amx, cell amx_addr, cell **phys_addr) {
	AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);
	*phys_addr = reinterpret_cast<cell*>(amx->base + hdr->dat + amx_addr);
	return AMX_ERR_NONE;
}

// amx_Exec_JIT compiles a public function (if needed) and runs the generated JIT code.
static int AMXAPI amx_Exec_JIT(AMX *amx, cell *retval, int index) {
	if (index >= -1) {
		std::map<AMX*, JIT*>::iterator iterator = jits.find(amx);
		if (iterator != jits.end()) {
			return iterator->second->CallPublicFunction(index, retval);
		}
	}
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
	return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData) {
	logprintf = (logprintf_t)ppData[PLUGIN_DATA_LOGPRINTF];

	new JumpX86(((void**)ppData[PLUGIN_DATA_AMX_EXPORTS])[PLUGIN_AMX_EXPORT_Exec], (void*)amx_Exec_JIT);
	new JumpX86(((void**)ppData[PLUGIN_DATA_AMX_EXPORTS])[PLUGIN_AMX_EXPORT_GetAddr], (void*)amx_GetAddr_JIT);

	logprintf("  JIT plugin v%s is OK.", PLUGIN_VERSION_STRING);

	return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
	// nothing
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX *amx) {
	JIT *jit = new JIT(amx);
	jits.insert(std::make_pair(amx, jit));
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX *amx) {
	JIT *jit = jits[amx];
	jits.erase(amx);
	delete jit;
	return AMX_ERR_NONE;
}
