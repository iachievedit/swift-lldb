//===-- CommandObjectLanguage.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectLanguage.h"

#include "lldb/Host/Host.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"

#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"

using namespace lldb;
using namespace lldb_private;

CommandObjectLanguage::CommandObjectLanguage (CommandInterpreter &interpreter) :
CommandObjectMultiword (interpreter,
                        "language",
                        "A set of commands for managing language-specific functionality.'.",
                        "language <language-name> <subcommand> [<subcommand-options>]"
                        )
{
    //Let the LanguageRuntime populates this command with subcommands
    LanguageRuntime::InitializeCommands(this);
}

void
CommandObjectLanguage::GenerateHelpText (Stream &output_stream) {
    CommandObjectMultiword::GenerateHelpText(output_stream);
}

CommandObjectLanguage::~CommandObjectLanguage ()
{
}
