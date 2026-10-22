// Decompiling a program held in memory: the host layer's entry points, shared
// by the native driver and the wasm exports.
#ifndef DECOMPILER_HOST_PROGRAM_HH
#define DECOMPILER_HOST_PROGRAM_HH

#include <cstdint>
#include <map>
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

/// What a caller adds to one decompilation of a function: names for its
/// local variables and parameters, by the ids the markup reports
/// (variableId), and comments, printed as Ghidra prints a function's PRE
/// (user2) and plate (header) comments.
struct Annotations {
  struct Local {
    std::string id;
    std::string name;
  };
  struct LineComment {
    uint64_t address = 0;
    std::string text;
  };
  /// Applied after the analysis, in order, each name made unique in the
  /// function's scope. An id naming no variable, or a variable named by a
  /// declaration, is passed over.
  std::vector<Local> locals;
  /// Each printed before the statement whose code holds its address.
  std::vector<LineComment> comments;
  /// Printed above the function's declaration; none when empty.
  std::string functionComment;
  /// Lists the comments that no statement holds the address of under
  /// Ghidra's "Comments that could not be placed in the function body:"
  /// above the function, rather than leaving them out (PrintC's
  /// setDisplayUnplaced).
  bool unplacedComments = false;
};

/// How far the decompiler qualifies a name with its namespaces, Ghidra's
/// namespacestrategy option: "minimal", as far as tells it apart where it is
/// used (`std::string::operator+=` in main, `max_size` inside another member
/// of its class); "all", in full unless it is in the scope where it is used;
/// "none", never.
extern const char *const NAMESPACE_STRATEGIES[3];

/// One program image under a SleighArchitecture configured as Ghidra's Java
/// side configures its decompiler process: Ghidra's names for the core types,
/// read-only memory propagating as constants, the image's functions and data
/// as named symbols in the namespaces their paths name, its strings as
/// character arrays, its pointers as external references to the functions
/// they name, and its prototypes bound to its functions and data by their
/// symbols (by their names, for those without one). A function known only
/// through a pointer is placed past the image's addresses, and its name's
/// target is the pointer's slot. Errors from Ghidra surface as
/// std::runtime_error carrying Ghidra's explanation.
class Program {
  std::ostringstream messages;
  std::vector<std::string> declarationWarnings;
  /// The placeholder address of each function known only through a pointer,
  /// and the pointer's slot.
  std::map<uint64_t, uint64_t> placeholders;
  std::unique_ptr<HostArchitecture> architecture;

 public:
  /// languageId names the processor and compiler, as Ghidra's language id
  /// followed by the compiler id: "x86:LE:64:default:gcc". namespaces is one
  /// of NAMESPACE_STRATEGIES.
  Program(ProgramImage image, const std::string &languageId, const std::string &namespaces = "minimal");
  ~Program();

  /// Ghidra's default for maxInstructions, DecompileOptions'
  /// SUGGESTED_MAX_INSTRUCTIONS and Architecture::max_instructions.
  static constexpr uint32_t DEFAULT_MAX_INSTRUCTIONS = 100000;
  /// Ghidra's default for timeLimitMs, DecompileOptions' SUGGESTED_DECOMPILE_TIMEOUT_SECS.
  static constexpr uint32_t DEFAULT_TIME_LIMIT_MS = 30000;

  /// The C for the function starting at the address, which must be one of
  /// the image's functions, as lines of linked tokens. The decompiler
  /// follows at most maxInstructions instructions of it ("main has more than
  /// 100000 instructions") and gives up once it has worked for timeLimitMs
  /// milliseconds ("main took longer than 30 seconds to decompile"), checking
  /// the time before each pass of each of its actions
  /// (patches/0001-action-checkpoint.patch). The annotations name variables
  /// and add comments to this decompilation only.
  DecompiledFunction decompile(uint64_t address, uint32_t maxInstructions = DEFAULT_MAX_INSTRUCTIONS,
                               uint32_t timeLimitMs = DEFAULT_TIME_LIMIT_MS,
                               const Annotations &annotations = Annotations());

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
