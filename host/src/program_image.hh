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

/// A function's entry point and the name the decompiler prints for it.
struct FunctionName {
  uint64_t address = 0;
  std::string name;
};

/// A named object (a global variable, a table, a string's label) and its
/// length in bytes, 0 when unknown.
struct DataName {
  uint64_t address = 0;
  std::string name;
  uint64_t size = 0;
};

/// A string: length bytes of characters, its terminator included if it has
/// one.
struct StringRange {
  uint64_t address = 0;
  uint64_t length = 0;
};

struct ProgramImage {
  std::vector<Region> regions;
  std::vector<FunctionName> functions;
  std::vector<DataName> data;
  std::vector<StringRange> strings;
};

/// Sorts the regions, functions, data and strings by address. Keeps the first
/// function, the first data name and the first string given at each address,
/// so a caller lists its preferred name first, and drops nameless functions
/// and data and empty strings. Throws std::invalid_argument when regions
/// overlap.
void normalize(ProgramImage &image);

/// Reads the image from the binary form the wasm exports take, every integer
/// little endian and every name UTF-8:
///
///   "DIMG", u32 version (1)
///   u32 count, then per region:   u64 address, u32 flags (1 read-only, 2 executable), u32 size, bytes
///   u32 count, then per function: u64 address, u32 size, name
///   u32 count, then per data:     u64 address, u64 size (0 unknown), u32 size, name
///   u32 count, then per string:   u64 address, u64 length
///
/// Throws std::invalid_argument on a malformed buffer.
ProgramImage readProgramImage(const uint8_t *bytes, size_t size);

}  // namespace host

#endif
