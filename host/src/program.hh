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

#include "markup.hh"
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
/// as named symbols, its strings as character arrays, and its prototypes
/// bound to its functions by name. Errors from Ghidra surface as
/// std::runtime_error carrying Ghidra's explanation.
class Program {
  std::ostringstream messages;
  std::vector<std::string> declarationWarnings;
  std::unique_ptr<HostArchitecture> architecture;

 public:
  /// languageId names the processor and compiler, as Ghidra's language id
  /// followed by the compiler id: "x86:LE:64:default:gcc".
  Program(ProgramImage image, const std::string &languageId);
  ~Program();

  /// Ghidra's default for maxInstructions, DecompileOptions'
  /// SUGGESTED_MAX_INSTRUCTIONS and Architecture::max_instructions.
  static constexpr uint32_t DEFAULT_MAX_INSTRUCTIONS = 100000;

  /// The C for the function starting at the address, which must be one of
  /// the image's functions, as lines of linked tokens. The decompiler
  /// follows at most maxInstructions instructions of it.
  DecompiledFunction decompile(uint64_t address, uint32_t maxInstructions = DEFAULT_MAX_INSTRUCTIONS);

  /// What went wrong building the program without stopping it: each line
  /// Ghidra printed while building the architecture, then each declaration
  /// of the image's prototypes that could not be read or applied.
  std::vector<std::string> warnings() const;

  /// Writes the program as a save file of Ghidra's XmlArchitecture: the
  /// regions, the core types, the data-types and the symbols, which the
  /// decompiler's own console loads with `restore`, to check this layer
  /// against it.
  void writeSaveFile(std::ostream &out) const;
};

}  // namespace host

#endif
