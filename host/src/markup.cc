#include "markup.hh"

#include <sstream>

#include "dynamic.hh"
#include "fspec.hh"
#include "funcdata.hh"
#include "json.hh"

namespace host {

using namespace ghidra;

const char *tokenKindName(TokenKind kind) {
  switch (kind) {
    case TokenKind::keyword: return "keyword";
    case TokenKind::comment: return "comment";
    case TokenKind::type: return "type";
    case TokenKind::function: return "function";
    case TokenKind::variable: return "variable";
    case TokenKind::constant: return "constant";
    case TokenKind::scope: return "scope";
    case TokenKind::syntax: break;
  }
  return "syntax";
}

std::string MarkupLine::text() const {
  std::string text;
  for (const MarkupToken &token : tokens) text += token.text;
  return text;
}

std::string DecompiledFunction::text() const {
  std::string text;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) text += '\n';
    text += lines[i].text();
  }
  return text;
}

std::string DecompiledFunction::json() const {
  std::ostringstream out;
  out << "{\"address\":" << address << ",\"name\":";
  writeJsonString(out, name);
  out << ",\"lines\":[";
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) out << ',';
    out << '[';
    const std::vector<MarkupToken> &tokens = lines[i].tokens;
    for (size_t j = 0; j < tokens.size(); ++j) {
      const MarkupToken &token = tokens[j];
      if (j > 0) out << ',';
      out << '[';
      writeJsonString(out, token.text);
      out << ',' << (int)token.kind << ',';
      if (token.hasAddress) out << token.address;
      else out << "-1";
      out << ',';
      if (token.hasTarget) out << token.target;
      else out << "-1";
      if (token.variable >= 0 || token.commentType != 0) out << ',' << token.variable;
      if (token.commentType != 0) out << ',' << token.commentType;
      out << ']';
    }
    out << ']';
  }
  out << "],\"variables\":[";
  for (size_t i = 0; i < variables.size(); ++i) {
    const Variable &variable = variables[i];
    if (i > 0) out << ',';
    out << "{\"id\":";
    writeJsonString(out, variable.id);
    out << ",\"kind\":" << (variable.parameter ? "\"parameter\"" : "\"local\"") << ",\"name\":";
    writeJsonString(out, variable.name);
    out << ",\"storage\":";
    writeJsonString(out, variable.storage);
    if (variable.locked) out << ",\"locked\":true";
    out << '}';
  }
  out << "]}";
  return out.str();
}

void RecordingEmit::tagVariable(const std::string &name, syntax_highlight hl, const Varnode *vn,
                                const PcodeOp *op) {
  const Symbol *symbol = declaring;
  if (symbol == nullptr && (vn != nullptr || op != nullptr)) {
    auto found = pushed.find({vn, op});
    if (found != pushed.end()) symbol = found->second;
  }
  references.push_back({name, op, vn, nullptr, nullptr, 0, symbol});
  EmitPrettyPrint::tagVariable(name, hl, vn, op);
}

void RecordingEmit::tagOp(const std::string &name, syntax_highlight hl, const PcodeOp *op) {
  references.push_back({name, op});
  EmitPrettyPrint::tagOp(name, hl, op);
}

void RecordingEmit::tagFuncName(const std::string &name, syntax_highlight hl, const Funcdata *fd,
                                const PcodeOp *op) {
  references.push_back({name, op, nullptr, fd});
  EmitPrettyPrint::tagFuncName(name, hl, fd, op);
}

void RecordingEmit::tagType(const std::string &name, syntax_highlight hl, const Datatype *ct) {
  references.push_back({name});
  EmitPrettyPrint::tagType(name, hl, ct);
}

void RecordingEmit::tagField(const std::string &name, syntax_highlight hl, const Datatype *ct, int4 off,
                             const PcodeOp *op) {
  references.push_back({name, op});
  EmitPrettyPrint::tagField(name, hl, ct, off, op);
}

void RecordingEmit::tagBitField(const std::string &name, syntax_highlight hl, const Datatype *ct, int4 id,
                                const PcodeOp *op) {
  references.push_back({name, op});
  EmitPrettyPrint::tagBitField(name, hl, ct, id, op);
}

void RecordingEmit::tagComment(const std::string &name, syntax_highlight hl, const AddrSpace *spc, uintb off) {
  references.push_back({name, nullptr, nullptr, nullptr, spc, off, nullptr, commentType});
  EmitPrettyPrint::tagComment(name, hl, spc, off);
}

void RecordingEmit::tagLabel(const std::string &name, syntax_highlight hl, const AddrSpace *spc, uintb off) {
  references.push_back({name, nullptr, nullptr, nullptr, spc, off});
  EmitPrettyPrint::tagLabel(name, hl, spc, off);
}

void RecordingEmit::tagCaseLabel(const std::string &name, syntax_highlight hl, const PcodeOp *op, uintb value) {
  references.push_back({name, op});
  EmitPrettyPrint::tagCaseLabel(name, hl, op, value);
}

int4 RecordingEmit::beginVarDecl(const Symbol *sym) {
  declaring = sym;
  return EmitPrettyPrint::beginVarDecl(sym);
}

void RecordingEmit::endVarDecl(int4 id) {
  declaring = nullptr;
  EmitPrettyPrint::endVarDecl(id);
}

void RecordingEmit::clear(void) {
  references.clear();
  pushed.clear();
  declaring = nullptr;
  commentType = 0;
  EmitPrettyPrint::clear();
}

namespace {

std::string hex(uintb value) {
  std::ostringstream out;
  out << "0x" << std::hex << value;
  return out.str();
}

/// "-0x98" or "0x8": a stack offset, signed.
std::string stackOffset(const AddrSpace *space, uintb offset) {
  intb value = sign_extend((intb)offset, 8 * space->getAddrSize() - 1);
  return value < 0 ? "-" + hex((uintb)-value) : hex((uintb)value);
}

/// "+0x8e" or "-0x1": an address as an offset from the function's entry.
std::string fromEntry(const Address &address, const Funcdata *function) {
  int64_t offset = (int64_t)(address.getOffset() - function->getAddress().getOffset());
  return offset < 0 ? "-" + hex((uintb)-offset) : "+" + hex((uintb)offset);
}

/// "stack:-0x98" or "register:0x0": a space and an offset, a stack offset
/// signed; a join (a value split across registers) by its pieces,
/// "join(register:0x10:4+register:0x0:8)", since the join space's offsets are
/// handed out in the order joins are first met (AddrSpaceManager::findAddJoin),
/// which depends on what was decompiled before.
std::string storageId(const Address &address, const Architecture *glb) {
  AddrSpace *space = address.getSpace();
  if (space->getType() == IPTR_JOIN) {
    JoinRecord *join = glb->findJoin(address.getOffset());
    std::string text = "join(";
    for (int4 i = 0; i < join->numPieces(); ++i) {
      const VarnodeData &piece = join->getPiece(i);
      if (i > 0) text += "+";
      text += storageId(Address(piece.space, piece.offset), glb) + ":" + std::to_string(piece.size);
    }
    return text + ")";
  }
  if (space->getType() == IPTR_SPACEBASE) return space->getName() + ":" + stackOffset(space, address.getOffset());
  return space->getName() + ":" + hex(address.getOffset());
}

/// Ghidra's way of writing a variable's storage: a register's name (`EAX`),
/// a stack location (`Stack[-0x98]`), the registers of a join, most
/// significant first (`RDX:RAX`); space, offset and size for anything else.
std::string storageDescription(const Address &address, int4 size, const Architecture *glb) {
  AddrSpace *space = address.getSpace();
  if (space->getType() == IPTR_JOIN) {
    JoinRecord *join = glb->findJoin(address.getOffset());
    std::string text;
    for (int4 i = 0; i < join->numPieces(); ++i) {
      const VarnodeData &piece = join->getPiece(i);
      if (i > 0) text += ":";
      text += storageDescription(Address(piece.space, piece.offset), (int4)piece.size, glb);
    }
    return text;
  }
  if (space->getType() == IPTR_SPACEBASE) return "Stack[" + stackOffset(space, address.getOffset()) + "]";
  std::string name = glb->translate->getRegisterName(space, address.getOffset(), size);
  if (!name.empty()) return name;
  return space->getName() + ":" + hex(address.getOffset()) + ":" + std::to_string(size);
}

/// The high variable the symbol names: one of the varnodes at its storage.
HighVariable *highOf(const Symbol *symbol, SymbolEntry *entry, Funcdata *function) {
  std::vector<Varnode *> varnodes;
  function->findLinkedVarnodes(entry, varnodes);
  for (Varnode *varnode : varnodes)
    if (!varnode->isAnnotation() && varnode->getHigh()->getSymbol() == symbol) return varnode->getHigh();
  return nullptr;
}

/// "hash:0xH@+0xU" for one of the high's varnodes that Ghidra's DynamicHash
/// finds back as a varnode of the same high, its name representative first;
/// "" if none.
std::string hashId(HighVariable *high, Funcdata *function) {
  std::vector<Varnode *> candidates{high->getNameRepresentative()};
  for (int4 i = 0; i < high->numInstances(); ++i) candidates.push_back(high->getInstance(i));
  for (Varnode *candidate : candidates) {
    DynamicHash hash;
    hash.uniqueHash(candidate, function);
    if (hash.getHash() == 0) continue;
    DynamicHash check;
    Varnode *found = check.findVarnode(function, hash.getAddress(), hash.getHash());
    if (found == nullptr || found->isAnnotation() || found->getHigh() != high) continue;
    return "hash:" + hex(hash.getHash()) + "@" + fromEntry(hash.getAddress(), function);
  }
  return "";
}

}  // namespace

std::string variableId(const Symbol *symbol, Funcdata *function) {
  SymbolEntry *entry = symbol->getFirstWholeMap();
  if (entry == nullptr) return "";
  if (entry->isDynamic())
    return "hash:" + hex(entry->getHash()) + "@" + fromEntry(entry->getFirstUseAddress(), function);
  Address address = entry->getAddr();
  Address use = entry->getFirstUseAddress();
  // A temporary's storage, the unique space, is the decompiler's own and
  // changes with the code before it: a temporary is named by a hash of its
  // data-flow, as Ghidra's Java side names one (HighVariable.requiresDynamicStorage).
  bool temporary = address.getSpace()->getType() == IPTR_INTERNAL;
  // A variable tied to its storage for the whole function (a stack local)
  // by where it starts, whatever size the decompiler gives it.
  if (!temporary && ((symbol->getFlags() & Varnode::addrtied) != 0 || use.isInvalid()))
    return storageId(address, function->getArch());
  std::string byStorage = storageId(address, function->getArch()) + ":" + std::to_string(entry->getSize()) + "@" +
                          fromEntry(use, function);
  HighVariable *high = highOf(symbol, entry, function);
  // A parameter the code never reads has no varnode: by its storage.
  if (high == nullptr) return !temporary && symbol->getCategory() == Symbol::function_parameter ? byStorage : "";
  // By storage, size and first use when Ghidra's own lookup, as its Java side
  // names a local (ScopeLocal::recoverNameRecommendationsForSymbols), finds
  // exactly this variable: one instruction can write two varnodes of one
  // storage (`sub rax,...` after a MULTIEQUAL at its address). Otherwise by
  // a hash of its data-flow.
  if (!temporary) {
    Varnode *found = use == function->getAddress() - 1 ? function->findVarnodeInput(entry->getSize(), address)
                                                        : function->findVarnodeWritten(entry->getSize(), address, use);
    if (found != nullptr && !found->isAnnotation() && found->getHigh() == high) return byStorage;
  }
  return hashId(high, function);
}

namespace {

TokenKind kindOf(uintb color) {
  switch (color) {
    case Emit::keyword_color:
    case Emit::error_color:  // a calling convention's name, when the model is unknown
      return TokenKind::keyword;
    case Emit::comment_color: return TokenKind::comment;
    case Emit::type_color: return TokenKind::type;
    case Emit::funcname_color: return TokenKind::function;
    case Emit::var_color:
    case Emit::param_color:
    case Emit::global_color:
    case Emit::special_color:
      return TokenKind::variable;
    case Emit::const_color: return TokenKind::constant;
    default: return TokenKind::syntax;
  }
}

/// A string literal as PrintC prints one: "...", or with an L, u, U or u8
/// prefix.
bool isStringLiteral(const std::string &text) {
  size_t quote = text.find('"');
  return quote != std::string::npos && quote <= 2 && text.back() == '"';
}

/// Turns the markup stream into lines, taking each token's references from
/// the recorder in order.
class LineBuilder {
  Architecture *glb;
  std::deque<RecordingEmit::Reference> &references;
  const std::map<uint64_t, uint64_t> &aliases;
  Funcdata *function;
  /// Each symbol's index in variables, -1 for one that is not reported.
  std::map<const Symbol *, int> variableIndex;

  /// The index in variables of the local variable or parameter the symbol
  /// is, added the first time it is named; -1 for a global, an equate, or a
  /// symbol without an id.
  int variableOf(const Symbol *symbol) {
    if (symbol == nullptr || symbol->getScope() != function->getScopeLocal()) return -1;
    int4 category = symbol->getCategory();
    if (category != Symbol::no_category && category != Symbol::function_parameter) return -1;
    auto known = variableIndex.find(symbol);
    if (known != variableIndex.end()) return known->second;
    int index = -1;
    Variable variable;
    variable.id = variableId(symbol, function);
    if (!variable.id.empty()) {
      SymbolEntry *entry = symbol->getFirstWholeMap();
      variable.parameter = category == Symbol::function_parameter;
      variable.name = symbol->getDisplayName();
      // A temporary is kept by its hash, as Ghidra's Java side writes
      // dynamic storage: HASH and the id's hash, without its "0x".
      if (entry->isDynamic() || entry->getAddr().getSpace()->getType() == IPTR_INTERNAL)
        variable.storage = "HASH:" + variable.id.substr(7, variable.id.find('@') - 7);
      else
        variable.storage = storageDescription(entry->getAddr(), entry->getSize(), glb);
      variable.locked = symbol->isNameLocked();
      index = (int)variables.size();
      variables.push_back(std::move(variable));
    }
    variableIndex.emplace(symbol, index);
    return index;
  }

  void newLine(int4 indent) {
    lines.emplace_back();
    if (indent > 0) {
      MarkupToken spaces;
      spaces.text.assign(indent, ' ');
      lines.back().tokens.push_back(std::move(spaces));
    }
  }

  void add(MarkupToken token) {
    if (!token.text.empty()) lines.back().tokens.push_back(std::move(token));
  }

  /// The address a global variable's varnode, or a constant pointing at one,
  /// names: the start of the symbol holding it, or the address itself.
  bool dataTarget(const Varnode *varnode, bool literal, uint64_t &target) const {
    AddrSpace *space = glb->getDefaultDataSpace();
    uintb offset;
    if (varnode->isConstant())
      offset = AddrSpace::addressToByte(varnode->getOffset(), space->getWordSize());
    else if (varnode->getSpace() == space)
      offset = varnode->getOffset();
    else
      return false;
    target = offset;
    if (literal) return true;
    SymbolEntry *entry = glb->symboltab->getGlobalScope()->queryContainer(Address(space, offset), 1, Address());
    if (entry != nullptr) target = entry->getAddr().getOffset();
    return true;
  }

  /// The start of the global a PTRSUB off the global space base names, which
  /// PrintC prints without a varnode: a global's address (`&completed.0`), an
  /// array or a function passed as a pointer (`__libc_start_main(main,...`).
  bool spacebaseTarget(const PcodeOp *op, uint64_t &target) const {
    if (op == nullptr || op->code() != CPUI_PTRSUB) return false;
    const Varnode *base = op->getIn(0);
    const Varnode *offset = op->getIn(1);
    if (!offset->isConstant()) return false;
    Datatype *type = base->getHighTypeReadFacing(op);
    if (type->getMetatype() != TYPE_PTR) return false;
    Datatype *pointed = static_cast<TypePointer *>(type)->getPtrTo();
    if (pointed->getMetatype() != TYPE_SPACEBASE) return false;
    Address address =
        static_cast<TypeSpacebase *>(pointed)->getAddress(offset->getOffset(), base->getSize(), op->getAddr());
    if (address.getSpace() != glb->getDefaultDataSpace()) return false;  // a stack location is no global
    target = address.getOffset();
    SymbolEntry *entry = glb->symboltab->getGlobalScope()->queryContainer(address, 1, Address());
    if (entry != nullptr) target = entry->getAddr().getOffset();
    return true;
  }

  /// The entry of the function a function name names. A declaration's and a
  /// function pointer's name come with their Funcdata, a call's with its CALL,
  /// whose first input encodes the call's FuncCallSpecs. (PrintC also prints
  /// a constant pool's instance_of as a function name, its Funcdata pointer
  /// then being a Varnode's; there is no constant pool in machine code.)
  bool functionTarget(const RecordingEmit::Reference &reference, uint64_t &target) const {
    const PcodeOp *op = reference.op;
    if (reference.function != nullptr && (op == nullptr || op->code() != CPUI_CPOOLREF)) {
      target = reference.function->getAddress().getOffset();
      return true;
    }
    if (op == nullptr || (op->code() != CPUI_CALL && op->code() != CPUI_CALLIND)) return false;
    const Varnode *callee = op->getIn(0);
    Address entry;
    if (callee->getSpace()->getType() == IPTR_FSPEC)
      entry = FuncCallSpecs::getFspecFromConst(callee->getAddr())->getEntryAddress();
    else if (op->code() == CPUI_CALL)
      entry = callee->getAddr();
    if (entry.isInvalid() || entry.getSpace() != glb->getDefaultCodeSpace()) return false;
    target = entry.getOffset();
    return true;
  }

  /// functionTarget, with the printer's aliases applied.
  bool aliasedFunctionTarget(const RecordingEmit::Reference &reference, uint64_t &target) const {
    if (!functionTarget(reference, target)) return false;
    auto alias = aliases.find(target);
    if (alias != aliases.end()) target = alias->second;
    return true;
  }

  MarkupToken token(uint4 element, const std::string &text, uintb color) {
    if (references.empty() || references.front().text != text)
      throw LowlevelError("The markup is out of step with the printer at \"" + text + "\"");
    RecordingEmit::Reference reference = references.front();
    references.pop_front();
    MarkupToken token;
    token.text = text;
    token.kind = kindOf(color);
    token.commentType = reference.commentType;
    if (reference.op != nullptr) {
      token.hasAddress = true;
      token.address = reference.op->getAddr().getOffset();
    } else if (element == ELEM_COMMENT && reference.space != nullptr) {
      token.hasAddress = true;
      token.address = reference.offset;
    }
    if (element == ELEM_FUNCNAME) token.hasTarget = aliasedFunctionTarget(reference, token.target);
    else if (element == ELEM_LABEL && reference.space != nullptr) {
      token.hasTarget = true;
      token.target = reference.offset;
    } else if (element == ELEM_VARIABLE && reference.varnode != nullptr) {
      bool literal = color == Emit::const_color && isStringLiteral(text);
      if (color == Emit::global_color || literal)
        token.hasTarget = dataTarget(reference.varnode, literal, token.target);
    } else if (element == ELEM_VARIABLE && color == Emit::global_color) {
      token.hasTarget = spacebaseTarget(reference.op, token.target);
    }
    if (element == ELEM_VARIABLE && color != Emit::global_color) token.variable = variableOf(reference.symbol);
    return token;
  }

 public:
  std::vector<MarkupLine> lines{1};
  std::vector<Variable> variables;

  LineBuilder(Architecture *architecture, std::deque<RecordingEmit::Reference> &recorded,
              const std::map<uint64_t, uint64_t> &targetAliases, Funcdata *decompiled)
      : glb(architecture), references(recorded), aliases(targetAliases), function(decompiled) {}

  void walk(Decoder &decoder) {
    uint4 element = decoder.openElement();
    std::string content;
    uintb color = Emit::no_color;
    int4 indent = 0;
    for (;;) {
      uint4 attribute = decoder.getNextAttributeId();
      if (attribute == 0) break;
      if (attribute == ATTRIB_CONTENT.getId()) content = decoder.readString();
      else if (attribute == ATTRIB_COLOR.getId()) color = decoder.readUnsignedInteger();
      else if (attribute == ATTRIB_INDENT.getId()) indent = (int4)decoder.readSignedInteger();
    }
    if (element == ELEM_BREAK) {
      newLine(indent);
    } else if (element == ELEM_SYNTAX) {
      MarkupToken syntax;
      syntax.text = content;
      // PrintC prints a qualifying namespace's name, and nothing else, as
      // syntax in the global variables' color.
      syntax.kind = color == Emit::global_color ? TokenKind::scope : kindOf(color);
      add(std::move(syntax));
    } else if (element == ELEM_VARIABLE || element == ELEM_OP || element == ELEM_FUNCNAME ||
               element == ELEM_TYPE || element == ELEM_FIELD || element == ELEM_BITFIELD ||
               element == ELEM_COMMENT || element == ELEM_LABEL || element == ELEM_VALUE) {
      add(token(element, content, color));
    } else {
      while (decoder.peekElement() != 0) walk(decoder);
    }
    decoder.closeElement(element);
  }
};

bool blank(const MarkupLine &line) {
  for (const MarkupToken &token : line.tokens)
    if (token.text.find_first_not_of(' ') != std::string::npos) return false;
  return true;
}

}  // namespace

MarkupPrintC::MarkupPrintC(Architecture *architecture) : PrintC(architecture) {
  delete emit;
  emit = recorder = new RecordingEmit();
  emit->setOutputStream(&stream);
  resetDefaults();
}

// A declaration pushes its symbol without a varnode or an operation, and
// RecordingEmit::declaring names it.
void MarkupPrintC::pushSymbol(const Symbol *sym, const Varnode *vn, const PcodeOp *op) {
  if (vn != nullptr || op != nullptr) recorder->pushed[{vn, op}] = sym;
  PrintC::pushSymbol(sym, vn, op);
}

// At offset 0, the symbol's name with an underscore before it (`_uStack_60`);
// elsewhere an unnamed location, not the symbol.
void MarkupPrintC::pushMismatchSymbol(const Symbol *sym, int4 off, int4 sz, const Varnode *vn, const PcodeOp *op) {
  if (off == 0 && (vn != nullptr || op != nullptr)) recorder->pushed[{vn, op}] = sym;
  PrintC::pushMismatchSymbol(sym, off, sz, vn, op);
}

void MarkupPrintC::emitLineComment(int4 indent, const Comment *comm) {
  recorder->commentType = comm->getType();
  PrintC::emitLineComment(indent, comm);
  recorder->commentType = 0;
}

DecompiledFunction MarkupPrintC::print(Funcdata *function) {
  recorder->references.clear();
  recorder->pushed.clear();
  recorder->declaring = nullptr;
  recorder->commentType = 0;
  stream.str("");
  stream.clear();
  docFunction(function);
  recorder->pushed.clear();

  std::istringstream input(stream.str());
  PackedDecode decoder(glb);
  decoder.ingestStream(input);
  LineBuilder builder(glb, recorder->references, aliases, function);
  builder.walk(decoder);
  if (!recorder->references.empty())
    throw LowlevelError("The markup is out of step with the printer: tokens left over");

  DecompiledFunction result;
  result.address = function->getAddress().getOffset();
  result.name = function->getDisplayName();
  result.lines = std::move(builder.lines);
  result.variables = std::move(builder.variables);
  // The printer starts a function with a line break and ends it with one.
  if (!result.lines.empty() && blank(result.lines.front())) result.lines.erase(result.lines.begin());
  while (!result.lines.empty() && blank(result.lines.back())) result.lines.pop_back();
  return result;
}

}  // namespace host
