// What a caller tells the decompiler about a program: its memory and its
// function names. Nothing here reads an executable format; the caller has
// already done that (an ELF reader, a disassembler's analysis).
#ifndef DECOMPILER_HOST_PROGRAM_IMAGE_HH
#define DECOMPILER_HOST_PROGRAM_IMAGE_HH

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

struct ProgramImage {
  std::vector<Region> regions;
  std::vector<FunctionName> functions;
};

/// Sorts the regions and the functions by address and keeps one name per
/// address, the first given, so a caller lists its preferred name first.
/// Regions must not overlap.
void normalize(ProgramImage &image);

}  // namespace host

#endif
