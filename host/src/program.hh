// Decompiling a program held in memory: the host layer's entry points, shared
// by the native driver and the wasm exports.
#ifndef DECOMPILER_HOST_PROGRAM_HH
#define DECOMPILER_HOST_PROGRAM_HH

#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "program_image.hh"

namespace host {

/// Starts Ghidra's decompiler library on the first call, and adds the
/// directories holding .ldefs files and the specification files they name.
/// Ghidra reads the .ldefs files when the first Program is built and never
/// again, so every directory must be added and filled by then.
void startLibrary(const std::vector<std::string> &specDirectories);

/// The Ghidra release the library was built from, e.g. "Ghidra_12.1.4_build 8b6bbb857acc".
std::string ghidraVersion();

class HostArchitecture;

/// One program image under a SleighArchitecture configured as Ghidra's Java
/// side configures its decompiler process: Ghidra's names for the core types,
/// read-only memory propagating as constants, the image's functions and data
/// as named symbols and its strings as character arrays. Errors from Ghidra
/// surface as std::runtime_error carrying Ghidra's explanation.
class Program {
  std::ostringstream messages;
  std::unique_ptr<HostArchitecture> architecture;

 public:
  /// languageId names the processor and compiler, as Ghidra's language id
  /// followed by the compiler id: "x86:LE:64:default:gcc".
  Program(ProgramImage image, const std::string &languageId);
  ~Program();

  /// The C for the function starting at the address, which must be one of
  /// the image's functions.
  std::string decompile(uint64_t address);

  /// Messages Ghidra printed while building the architecture.
  std::string warnings() const { return messages.str(); }

  /// Writes the program as a save file of Ghidra's XmlArchitecture: the
  /// regions, the core types, the data-types and the symbols, which the
  /// decompiler's own console loads with `restore`, to check this layer
  /// against it.
  void writeSaveFile(std::ostream &out) const;
};

}  // namespace host

#endif
