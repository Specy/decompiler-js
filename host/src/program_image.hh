// What a caller tells the decompiler about a program: its memory, the names
// of its functions and data, and where its strings are. Nothing here reads an
// executable format; the caller has already done that (an ELF reader, a
// disassembler's analysis).
#ifndef DECOMPILER_HOST_PROGRAM_IMAGE_HH
#define DECOMPILER_HOST_PROGRAM_IMAGE_HH

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace host {

/// A run of the program's memory at its link-time address.
struct Region {
  uint64_t address = 0;
  std::vector<uint8_t> bytes;
  bool readOnly = false;
  bool executable = false;
};

/// The namespaces and classes a name is in, outermost first, each printed as
/// given ({"std", "vector<std::string>"}); empty for the global scope.
using NamespacePath = std::vector<std::string>;

/// A function's entry point and the name the decompiler prints for it, in
/// its namespace path. symbol is the name its prototype binds by and
/// pointers name it by (its linkage name, `_ZSt20__throw_length_errorPKc`);
/// empty for name.
struct FunctionName {
  uint64_t address = 0;
  std::string name;
  NamespacePath path;
  std::string symbol;

  /// The name declarations bind by: symbol, or else name.
  const std::string &binding() const { return symbol.empty() ? name : symbol; }
};

/// A named object (a global variable, a table, a string's label) and its
/// length in bytes, 0 when unknown, in its namespace path; symbol as a
/// function's.
struct DataName {
  uint64_t address = 0;
  std::string name;
  uint64_t size = 0;
  NamespacePath path;
  std::string symbol;

  const std::string &binding() const { return symbol.empty() ? name : symbol; }
};

/// A string: length bytes of characters, its terminator included if it has
/// one.
struct StringRange {
  uint64_t address = 0;
  uint64_t length = 0;
};

/// A pointer-sized slot holding the address of a function: a GOT entry the
/// dynamic linker fills (`__libc_start_main@got`), whose value the file does
/// not hold. A call through the slot is a call of that function: the image's
/// function whose symbol (or name, without one) is symbol when symbol is
/// given, else the one of that name in that namespace path; or else one
/// known only by this name, path and symbol.
struct PointerName {
  uint64_t address = 0;
  std::string name;
  NamespacePath path;
  std::string symbol;
};

struct ProgramImage {
  std::vector<Region> regions;
  std::vector<FunctionName> functions;
  std::vector<DataName> data;
  std::vector<StringRange> strings;
  std::vector<PointerName> pointers;
  /// C declarations of the library functions the program calls, the types
  /// they need and its global variables' types (see declarations.hh).
  std::string prototypes;
};

/// Sorts the regions, functions, data, strings and pointers by address. Keeps
/// the first function, the first data name, the first string and the first
/// pointer given at each address, so a caller lists its preferred name first,
/// and drops nameless functions, data and pointers and empty strings. Throws
/// std::invalid_argument when regions overlap.
void normalize(ProgramImage &image);

/// Reads the image from the binary form the wasm exports take, every integer
/// little endian and every name UTF-8:
///
///   "DIMG", u32 version (3)
///   u32 count, then per region:   u64 address, u32 flags (1 read-only, 2 executable), u32 size, bytes
///   u32 count, then per function: u64 address, u32 size, name, path, symbol
///   u32 count, then per data:     u64 address, u64 size (0 unknown), u32 size, name, path, symbol
///   u32 count, then per string:   u64 address, u64 length
///   u32 count, then per pointer:  u64 address, u32 size, name, path, symbol
///   u32 size, the prototypes
///
/// where a path is a u32 count of namespaces, then per namespace a u32 size
/// and the name, outermost first, and a symbol a u32 size (0 for none) and
/// the name. Version 2, the same without paths and symbols, is read too.
///
/// Throws std::invalid_argument on a malformed buffer.
ProgramImage readProgramImage(const uint8_t *bytes, size_t size);

}  // namespace host

#endif
