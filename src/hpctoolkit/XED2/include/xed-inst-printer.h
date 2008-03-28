/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2007 Intel Corporation 
All rights reserved. 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/// @file xed-inst-printer.h 
/// @author Mark Charney   <mark.charney@intel.com> 


#ifndef _XED_INST_PRINTER_H_
# define _XED_INST_PRINTER_H_
#include "xed-types.h"
#include "xed-decoded-inst.h"
#include "xed-syntax-enum.h"


/// Disassemble the decoded instruction using the ATT SYSV syntax. The
/// output buffer must be at least 16 bytes long. Returns true if
/// disassembly proceeded without errors.
///@ingroup PRINT
XED_DLL_EXPORT bool
xed_format_att(xed_decoded_inst_t* xedd,
               char* out_buffer,
               uint32_t buffer_len,
               uint64_t runtime_instruction_address);

/// Disassemble the decoded instruction using the Intel syntax. The
/// output buffer must be at least 16 bytes long. Returns true if
/// disassembly proceeded without errors.
///@ingroup PRINT
XED_DLL_EXPORT bool
xed_format_intel(xed_decoded_inst_t* xedd,
                 char* out_buffer,
                 uint32_t buffer_len,
                 uint64_t runtime_instruction_address);

/// Disassemble the decoded instruction using the XED syntax providing all
/// operand resources (implicit, explicit, suppressed). The output buffer
/// must be at least 25 bytes long. Returns true if disassembly proceeded
/// without errors. 
///@ingroup PRINT
XED_DLL_EXPORT bool
xed_format_xed(xed_decoded_inst_t* xedd,
               char* out_buffer,
               uint32_t buffer_len,
               uint64_t runtime_instruction_address);


/// Disassemble the decoded instruction using the specified syntax.
/// The output buffer must be at least 25 bytes long. Returns true if
/// disassembly proceeded without errors.
///@ingroup PRINT
XED_DLL_EXPORT bool
xed_format(xed_syntax_enum_t syntax,
           xed_decoded_inst_t* xedd,
           char* out_buffer,
           int  buffer_len,
           uint64_t runtime_instruction_address);


#endif
////////////////////////////////////////////////////////////////////////////
//Local Variables:
//pref: "../../xed-inst-printer.c"
//End:
