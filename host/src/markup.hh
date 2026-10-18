// The decompiler's output as lines of tokens, each linked to the instruction
// it came from and the function or data it names.
#ifndef DECOMPILER_HOST_MARKUP_HH
#define DECOMPILER_HOST_MARKUP_HH

#include <cstdint>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "printc.hh"

namespace host {

/// What a token is, from Ghidra's syntax highlight class (Emit::syntax_highlight,
/// Java's ClangToken colors): parameters, globals and special variables are
/// variables; the unknown-convention highlight is a keyword's; everything
/// without a class (punctuation, operators, labels, field names) is syntax.
enum class TokenKind { keyword, comment, type, function, variable, constant, syntax };

const char *tokenKindName(TokenKind kind);

struct MarkupToken {
  std::string text;
  TokenKind kind = TokenKind::syntax;
  /// The address of the instruction whose p-code operation the token belongs
  /// to (a variable, operator, function name, field or case label carrying
  /// one), or for a comment the address it is attached to.
  bool hasAddress = false;
  uint64_t address = 0;
  /// The address of what a name refers to: a function's entry (or, for a
  /// function known only through a pointer, the pointer's slot), a global's
  /// or string's start, a label's code. A global printed as its address or a
  /// function passed as a pointer refers to its start too.
  bool hasTarget = false;
  uint64_t target = 0;
};

/// One line: its tokens' texts, the indent's spaces first, make up the line.
struct MarkupLine {
  std::vector<MarkupToken> tokens;
  std::string text() const;
};

struct DecompiledFunction {
  uint64_t address = 0;
  std::string name;
  /// The lines as Ghidra lays them out, without the empty first and last
  /// ones the printer puts around a function.
  std::vector<MarkupLine> lines;

  /// The lines joined with "\n".
  std::string text() const;
  /// {"address", "name", "lines": [[[text, kind, address, target], ...], ...]},
  /// address and target -1 when absent.
  std::string json() const;
};

/// Ghidra's pretty printer, recording what each token was printed for. The
/// printer passes every token it is given, in order, to an EmitMarkup it
/// owns, which encodes only op references and, for a function name, nothing
/// that identifies the function; so each tag call is recorded here with its
/// pointers and matched, in order, to the token it becomes in the markup.
class RecordingEmit : public ghidra::EmitPrettyPrint {
 public:
  struct Reference {
    std::string text;
    const ghidra::PcodeOp *op = nullptr;
    const ghidra::Varnode *varnode = nullptr;
    const ghidra::Funcdata *function = nullptr;
    const ghidra::AddrSpace *space = nullptr;  // comments and labels
    ghidra::uintb offset = 0;
  };
  std::deque<Reference> references;

  RecordingEmit() { setMarkup(true); }

  void tagVariable(const std::string &name, syntax_highlight hl, const ghidra::Varnode *vn,
                   const ghidra::PcodeOp *op) override;
  void tagOp(const std::string &name, syntax_highlight hl, const ghidra::PcodeOp *op) override;
  void tagFuncName(const std::string &name, syntax_highlight hl, const ghidra::Funcdata *fd,
                   const ghidra::PcodeOp *op) override;
  void tagType(const std::string &name, syntax_highlight hl, const ghidra::Datatype *ct) override;
  void tagField(const std::string &name, syntax_highlight hl, const ghidra::Datatype *ct, ghidra::int4 off,
                const ghidra::PcodeOp *op) override;
  void tagBitField(const std::string &name, syntax_highlight hl, const ghidra::Datatype *ct, ghidra::int4 id,
                   const ghidra::PcodeOp *op) override;
  void tagComment(const std::string &name, syntax_highlight hl, const ghidra::AddrSpace *spc,
                  ghidra::uintb off) override;
  void tagLabel(const std::string &name, syntax_highlight hl, const ghidra::AddrSpace *spc,
                ghidra::uintb off) override;
  void tagCaseLabel(const std::string &name, syntax_highlight hl, const ghidra::PcodeOp *op,
                    ghidra::uintb value) override;
  void clear(void) override;
};

/// Ghidra's C printer with a RecordingEmit for its emitter.
class MarkupPrintC : public ghidra::PrintC {
  RecordingEmit *recorder;
  std::ostringstream stream;  // the markup of the function being printed
  std::map<uint64_t, uint64_t> aliases;

 public:
  explicit MarkupPrintC(ghidra::Architecture *architecture);

  /// Makes a function name whose entry is at entry target another address:
  /// a function known only through a pointer targets the pointer's slot
  /// rather than the placeholder it was given.
  void aliasTarget(uint64_t entry, uint64_t target) { aliases[entry] = target; }

  /// The function, decompiled already, as lines of linked tokens.
  DecompiledFunction print(ghidra::Funcdata *function);
};

}  // namespace host

#endif
