// Decompiling a program held in memory: the host layer's entry points, shared
// by the native driver and the wasm exports.
#ifndef DECOMPILER_HOST_PROGRAM_HH
#define DECOMPILER_HOST_PROGRAM_HH

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "program_image.hh"

namespace host {

/// Starts Ghidra's decompiler library and adds the directories holding
/// .ldefs files and the specification files they name. Call it once, before
/// the first Program. Ghidra reads the directories when the first Program is
/// built, so every directory must be filled by then.
void startLibrary(const std::vector<std::string> &specDirectories);

/// The Ghidra release the library was built from, e.g. "Ghidra_12.1.4_build 8b6bbb857acc".
std::string ghidraVersion();

class HostArchitecture;

/// One program image under a SleighArchitecture. Errors from Ghidra surface
/// as std::runtime_error carrying Ghidra's explanation.
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
};

}  // namespace host

#endif
