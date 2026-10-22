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
/// A namespace or class a name is qualified with (PrintC::pushSymbolScope,
/// emitSymbolScope: `std` and `string` in `std::string::operator+=`) is a
/// scope, which the package's JavaScript joins, with the `::`s after it, to
/// the name it qualifies.
enum class TokenKind { keyword, comment, type, function, variable, constant, syntax, scope };

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
  /// For a name of one of the function's local variables or parameters, its
  /// index in DecompiledFunction::variables; -1 otherwise.
  int variable = -1;
  /// For a comment's token, the comment's type (Ghidra's Comment::comment_type:
  /// user2 for a caller's line comment, header for its function comment,
  /// warning and warningheader for the decompiler's own); 0 otherwise.
  uint32_t commentType = 0;
};

/// A local variable or parameter of a decompiled function.
struct Variable {
  /// Ghidra's identity for the variable, from its storage (variableId).
  std::string id;
  bool parameter = false;
  /// The name printed: the decompiler's, or the one the caller gave by id.
  std::string name;
  /// Where Ghidra keeps it, as Ghidra writes storage: a register (`EAX`), a
  /// stack location (`Stack[-0x98]`), registers joined (`RDX:RAX`), or a
  /// temporary's hash (`HASH:3f276f9932`).
  std::string storage;
  /// Named by a C declaration (a parameter of a prototype the image gave):
  /// a name given for it is not applied.
  bool locked = false;
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
  /// The local variables and parameters the tokens name, in the order first
  /// named.
  std::vector<Variable> variables;

  /// The lines joined with "\n".
  std::string text() const;
  /// {"address", "name", "lines": [[[text, kind, address, target], ...], ...],
  /// "variables": [{"id", "kind", "name", "storage", "locked"?}, ...]},
  /// address and target -1 when absent. A token naming a variable or
  /// belonging to a comment adds its variable's index (-1 for none), and a
  /// comment's token then its comment type: [text, kind, address, target,
  /// variable, commentType].
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
    /// For a variable, the symbol the printer printed its name from.
    const ghidra::Symbol *symbol = nullptr;
    /// For a comment, its type.
    uint32_t commentType = 0;
  };
  std::deque<Reference> references;
  /// The symbol the printer last pushed for each varnode and operation
  /// (MarkupPrintC::pushSymbol), which the tagVariable call printing it
  /// carries; Emit's tags carry no symbol.
  std::map<std::pair<const ghidra::Varnode *, const ghidra::PcodeOp *>, const ghidra::Symbol *> pushed;
  /// The symbol whose declaration is being printed, between beginVarDecl
  /// and endVarDecl; its name is pushed without a varnode or an operation.
  const ghidra::Symbol *declaring = nullptr;
  /// The type of the comment being printed (MarkupPrintC::emitLineComment).
  uint32_t commentType = 0;

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
  ghidra::int4 beginVarDecl(const ghidra::Symbol *sym) override;
  void endVarDecl(ghidra::int4 id) override;
  void clear(void) override;
};

/// Ghidra's identity for one of a function's local variables or parameters,
/// from its storage, the same for the same bytes whatever was decompiled
/// before and wherever the function is placed: "" for a symbol it cannot
/// identify.
///
///   stack:-0x98                    a variable tied to the stack, by its
///                                  offset alone, so it survives a change of
///                                  its size;
///   register:0x0:4@+0x8e           storage (space and offset), size, and its
///                                  first use as an offset from the entry;
///                                  @-0x1, the entry less one, for an input;
///   join(register:0x10:8+register:0x0:8):16@+0x19b
///                                  a value split across registers, by its
///                                  pieces (Ghidra numbers join addresses in
///                                  the order it meets them);
///   hash:0x3f276f9932@+0x27c       a temporary, by Ghidra's hash of its
///                                  data-flow (DynamicHash) and the address
///                                  of the operation hashed, and any variable
///                                  that Ghidra's own lookup by storage would
///                                  not find exactly.
std::string variableId(const ghidra::Symbol *symbol, ghidra::Funcdata *function);

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

  void pushSymbol(const ghidra::Symbol *sym, const ghidra::Varnode *vn, const ghidra::PcodeOp *op) override;
  void pushMismatchSymbol(const ghidra::Symbol *sym, ghidra::int4 off, ghidra::int4 sz, const ghidra::Varnode *vn,
                          const ghidra::PcodeOp *op) override;
  void emitLineComment(ghidra::int4 indent, const ghidra::Comment *comm) override;
};

}  // namespace host

#endif
