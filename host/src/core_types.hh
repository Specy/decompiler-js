// The decompiler's core data-types named as Ghidra's Java side names them, so
// the C reads like Ghidra's (undefined8, int, long, ulong, char) rather than
// like the decompiler's own console (xunknown8, int4, int8, uint8).
#ifndef DECOMPILER_HOST_CORE_TYPES_HH
#define DECOMPILER_HOST_CORE_TYPES_HH

#include "type.hh"
#include "xml.hh"

namespace host {

/// Registers the core types Ghidra's PcodeDataTypeManager.generateCoreTypes
/// sends a decompiler process, with the sizes of the compiler specification's
/// <data_organization> (or Ghidra's DataOrganizationImpl defaults where it is
/// silent), then caches them. compilerSpec is the <compiler_spec> element,
/// or null for the defaults.
void buildGhidraCoreTypes(ghidra::TypeFactory &types, const ghidra::Element *compilerSpec);

}  // namespace host

#endif
