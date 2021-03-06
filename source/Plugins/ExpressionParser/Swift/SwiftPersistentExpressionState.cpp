//===-- SwiftPersistentExpressionState.cpp ----------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/IRExecutionUnit.h"
#include "SwiftPersistentExpressionState.h"
#include "SwiftExpressionVariable.h"

#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Core/Value.h"

#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/SwiftASTContext.h" // Needed for llvm::isa<SwiftASTContext>(...)

#include "swift/AST/Decl.h"
#include "swift/AST/Pattern.h"

#include "llvm/ADT/StringMap.h"

using namespace lldb;
using namespace lldb_private;

SwiftPersistentExpressionState::SwiftPersistentExpressionState () :
    lldb_private::PersistentExpressionState(LLVMCastKind::eKindClang),
    m_next_persistent_variable_id (0),
    m_next_persistent_error_id (0)
{
}

ExpressionVariableSP
SwiftPersistentExpressionState::CreatePersistentVariable (const lldb::ValueObjectSP &valobj_sp)
{
    return AddNewlyConstructedVariable(new SwiftExpressionVariable(valobj_sp));
}

ExpressionVariableSP
SwiftPersistentExpressionState::CreatePersistentVariable (ExecutionContextScope *exe_scope, 
                                                    const ConstString &name, 
                                                    const CompilerType& compiler_type,
                                                    lldb::ByteOrder byte_order, 
                                                    uint32_t addr_byte_size)
{
    return AddNewlyConstructedVariable(new SwiftExpressionVariable(exe_scope, name, compiler_type, byte_order, addr_byte_size));
}

void
SwiftPersistentExpressionState::RemovePersistentVariable (lldb::ExpressionVariableSP variable)
{
    if (!variable)
        return;

    RemoveVariable(variable);
    
    const char *name = variable->GetName().AsCString();
    
    if (*name != '$')
        return;
    name++;

    bool is_error = false;

    switch (*name)
    {
    case 'R':
        break;
    case 'E':
        is_error = true;
        break;
    default:
        return;
    }
    name++;

    uint32_t value = strtoul(name, NULL, 0);
    if (is_error)
    {
        if (value == m_next_persistent_error_id - 1)
            m_next_persistent_error_id--;
    }
    else
    {
        if (value == m_next_persistent_variable_id - 1)
            m_next_persistent_variable_id--;
    }
}

ConstString
SwiftPersistentExpressionState::GetNextPersistentVariableName (bool is_error)
{
    char name_cstr[256];
    
    const char *prefix = nullptr;
    
    if (is_error)
        prefix = "$E";
    else
        prefix = "$R";

    ::snprintf (name_cstr,
                sizeof(name_cstr),
                "%s%u",
                prefix,
                is_error? m_next_persistent_error_id++ : m_next_persistent_variable_id++);

    ConstString name(name_cstr);
    return name;
}

bool
SwiftPersistentExpressionState::SwiftDeclMap::DeclsAreEquivalent(swift::Decl *lhs_decl, swift::Decl *rhs_decl)
{
    swift::DeclKind lhs_kind = lhs_decl->getKind();
    swift::DeclKind rhs_kind = rhs_decl->getKind();
    if (lhs_kind != rhs_kind)
        return false;
    
    bool equivalent = false;
    switch (lhs_kind)
    {
    // All the decls that define types of things should only allow one
    // instance, so in this case, erase what is there, and copy in the new version.
    case swift::DeclKind::Class:
    case swift::DeclKind::Struct:
    case swift::DeclKind::Enum:
    case swift::DeclKind::TypeAlias:
    case swift::DeclKind::Protocol:
        equivalent = true;
        break;
    // For functions, we check that the signature is the same, and only replace it if it
    // is, otherwise we just add it.
    case swift::DeclKind::Func:
    {
        swift::FuncDecl *lhs_func_decl = llvm::dyn_cast<swift::FuncDecl>(lhs_decl);
        swift::FuncDecl *rhs_func_decl = llvm::dyn_cast<swift::FuncDecl>(rhs_decl);
            // Okay, they have the same number of arguments, are they of the same type?
        llvm::MutableArrayRef<swift::Pattern *> lhs_patterns = lhs_func_decl->getBodyParamPatterns();
        llvm::MutableArrayRef<swift::Pattern *> rhs_patterns = rhs_func_decl->getBodyParamPatterns();
        size_t num_patterns = lhs_patterns.size();
        bool body_params_equal = true;
        if (num_patterns == rhs_patterns.size())
        {
            for (int idx = 0; idx < num_patterns; idx++)
            {
                swift::CanType lhs_type;
                swift::CanType rhs_type;
                swift::Pattern *lhs_pattern = lhs_patterns[idx];
                swift::Pattern *rhs_pattern = rhs_patterns[idx];
                
                if (lhs_pattern->hasType())
                    lhs_type = lhs_pattern->getType().getCanonicalTypeOrNull();
                if (rhs_pattern->hasType())
                    rhs_type = rhs_pattern->getType().getCanonicalTypeOrNull();
                if (lhs_type != rhs_type)
                {
                    body_params_equal = false;
                    break;
                }
            }
            if (body_params_equal)
            {
                // The bodies look the same, now try the return values:
                swift::CanType lhs_result_type = lhs_func_decl->getBodyResultType().getCanonicalTypeOrNull();
                swift::CanType rhs_result_type = rhs_func_decl->getBodyResultType().getCanonicalTypeOrNull();
                
                if (lhs_result_type == rhs_result_type)
                {
                    equivalent = true;
                }
            }
        }
    }
    break;
    // Not really sure how to compare operators, so we just do last one wins...
    case swift::DeclKind::InfixOperator:
    case swift::DeclKind::PrefixOperator:
    case swift::DeclKind::PostfixOperator:
        equivalent = true;
        break;
    default:
        equivalent = true;
        break;
    }
    return equivalent;
}

void
SwiftPersistentExpressionState::SwiftDeclMap::AddDecl (swift::ValueDecl *value_decl, bool check_existing, const ConstString &alias)
{
    std::string name_str;
    
    if (alias.IsEmpty())
    {
        name_str = (value_decl->getName().str());
    }
    else
    {
        name_str.assign(alias.GetCString());
    }
    
    if (!check_existing)
    {
        m_swift_decls.insert(std::make_pair(name_str, value_decl));
        return;
    }
    
    SwiftDeclMapTy::iterator map_end = m_swift_decls.end();
    std::pair<SwiftDeclMapTy::iterator, SwiftDeclMapTy::iterator> found_range = m_swift_decls.equal_range(name_str);
    
    if (found_range.first == map_end)
    {
        m_swift_decls.insert(std::make_pair(name_str, value_decl));
        return;
    }
    else
    {
        SwiftDeclMapTy::iterator cur_item;
        bool done = false;
        for (cur_item = found_range.first; !done && cur_item != found_range.second; cur_item++)
        {
            swift::ValueDecl *cur_decl = (*cur_item).second;
            if (DeclsAreEquivalent(cur_decl, value_decl))
            {
                m_swift_decls.erase (cur_item);
                break;
            }
        }
        
        m_swift_decls.insert(std::make_pair(name_str, value_decl));
    }
}

bool
SwiftPersistentExpressionState::SwiftDeclMap::FindMatchingDecls(const ConstString &name, std::vector<swift::ValueDecl *> &matches)
{
    std::vector<swift::ValueDecl *> found_elements;
    size_t start_num_items = matches.size();
    std::string name_str(name.AsCString());
    
    std::pair<SwiftDeclMapTy::iterator, SwiftDeclMapTy::iterator> found_range = m_swift_decls.equal_range(name_str);
    for (SwiftDeclMapTy::iterator cur_item = found_range.first; cur_item != found_range.second; cur_item++)
    {
        bool add_it = true;
        swift::ValueDecl *cur_decl = (*cur_item).second;
        
        for (size_t idx = 0; idx < start_num_items; idx++)
        {
            if (DeclsAreEquivalent(matches[idx], cur_decl))
            {
                add_it = false;
                break;
            }
        }
        if (add_it)
            matches.push_back((*cur_item).second);
    }
    return start_num_items != matches.size();
}

void
SwiftPersistentExpressionState::SwiftDeclMap::CopyDeclsTo (SwiftPersistentExpressionState::SwiftDeclMap &target_map)
{
    for (auto elem : m_swift_decls)
        target_map.AddDecl(elem.second, true, ConstString());
}

void
SwiftPersistentExpressionState::RegisterSwiftPersistentDecl (swift::ValueDecl *value_decl)
{
    m_swift_persistent_decls.AddDecl(value_decl, true, ConstString());
}

void
SwiftPersistentExpressionState::RegisterSwiftPersistentDeclAlias(swift::ValueDecl *value_decl, const ConstString &name)
{
    m_swift_persistent_decls.AddDecl(value_decl, true, name);
}

void
SwiftPersistentExpressionState::CopyInSwiftPersistentDecls (SwiftPersistentExpressionState::SwiftDeclMap &target_map)
{
    target_map.CopyDeclsTo(m_swift_persistent_decls);
}

bool
SwiftPersistentExpressionState::GetSwiftPersistentDecls (const ConstString &name, std::vector<swift::ValueDecl *> &matches)
{
    return m_swift_persistent_decls.FindMatchingDecls (name, matches);
}

void
SwiftPersistentExpressionState::RegisterExecutionUnit (lldb::IRExecutionUnitSP &execution_unit_sp)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    m_execution_units.insert(execution_unit_sp);
    
    if (log)
        log->Printf ("Registering JITted Functions:\n");
    
    for (const IRExecutionUnit::JittedFunction &jitted_function : execution_unit_sp->GetJittedFunctions())
    {
        if (jitted_function.m_name != execution_unit_sp->GetFunctionName() &&
            jitted_function.m_remote_addr != LLDB_INVALID_ADDRESS)
        {
            m_symbol_map[jitted_function.m_name.GetCString()] = jitted_function.m_remote_addr;
            if (log)
                log->Printf ("  Function: %s at 0x%" PRIx64 ".", jitted_function.m_name.GetCString(), jitted_function.m_remote_addr);
        }
    }
    
    if (log)
        log->Printf ("Registering JIIted Symbols:\n");
    
    for (const IRExecutionUnit::JittedGlobalVariable &global_var : execution_unit_sp->GetJittedGlobalVariables())
    {
        if (global_var.m_remote_addr != LLDB_INVALID_ADDRESS)
        {
            // Demangle the name before inserting it, so that lookups by the ConstStr of the demangled name
            // will find the mangled one (needed for looking up metadata pointers.)
            Mangled mangler(global_var.m_name);
            mangler.GetDemangledName(lldb::eLanguageTypeUnknown);
            m_symbol_map[global_var.m_name.GetCString()] = global_var.m_remote_addr;
            if (log)
                log->Printf ("  Symbol: %s at 0x%" PRIx64 ".", global_var.m_name.GetCString(), global_var.m_remote_addr);
        }
    }
}

void
SwiftPersistentExpressionState::RegisterSymbol (const ConstString &name, lldb::addr_t addr)
{
    m_symbol_map[name.GetCString()] = addr;
}

lldb::addr_t
SwiftPersistentExpressionState::LookupSymbol (const ConstString &name)
{
    SymbolMap::iterator si = m_symbol_map.find(name.GetCString());
    
    if (si != m_symbol_map.end())
        return si->second;
    else
        return LLDB_INVALID_ADDRESS;    
}
