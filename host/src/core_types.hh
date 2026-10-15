// The decompiler's core data-types named as Ghidra's Java side names them, so
// the C reads like Ghidra's (undefined8, int, long, ulong, char) rather than
// like the decompiler's own console (xunknown8, int4, int8, uint8).
#ifndef DECOMPILER_HOST_CORE_TYPES_HH
#define DECOMPILER_HOST_CORE_TYPES_HH

#include "type.hh"
#include "xml.hh"

namespace host {

/// The parts of Ghidra's DataOrganization the core types and the C type names
/// depend on, with DataOrganizationImpl's defaults.
struct DataOrganization {
  int charSize = 1;
  bool signedChar = true;
  int wideCharSize = 2;
  int shortSize = 2;
  int intSize = 4;
  int longSize = 4;
  int longLongSize = 8;
  int floatSize = 4;
  int doubleSize = 8;
  int longDoubleSize = 8;
};

/// The sizes a compiler specification's <data_organization> gives, or the
/// defaults where it is silent. compilerSpec is the <compiler_spec> element,
/// or null for the defaults.
DataOrganization readDataOrganization(const ghidra::Element *compilerSpec);

/// Registers the core types Ghidra's PcodeDataTypeManager.generateCoreTypes
/// sends a decompiler process for that data organization, then caches them.
void buildGhidraCoreTypes(ghidra::TypeFactory &types, const DataOrganization &org);

}  // namespace host

#endif
