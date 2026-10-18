#include "markup.hh"

#include <sstream>

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
      out << ']';
    }
    out << ']';
  }
  out << "]}";
  return out.str();
}

void RecordingEmit::tagVariable(const std::string &name, syntax_highlight hl, const Varnode *vn,
                                const PcodeOp *op) {
  references.push_back({name, op, vn});
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
  references.push_back({name, nullptr, nullptr, nullptr, spc, off});
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

void RecordingEmit::clear(void) {
  references.clear();
  EmitPrettyPrint::clear();
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
    }
    return token;
  }

 public:
  std::vector<MarkupLine> lines{1};

  LineBuilder(Architecture *architecture, std::deque<RecordingEmit::Reference> &recorded,
              const std::map<uint64_t, uint64_t> &targetAliases)
      : glb(architecture), references(recorded), aliases(targetAliases) {}

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
      syntax.kind = kindOf(color);
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

DecompiledFunction MarkupPrintC::print(Funcdata *function) {
  recorder->references.clear();
  stream.str("");
  stream.clear();
  docFunction(function);

  std::istringstream input(stream.str());
  PackedDecode decoder(glb);
  decoder.ingestStream(input);
  LineBuilder builder(glb, recorder->references, aliases);
  builder.walk(decoder);
  if (!recorder->references.empty())
    throw LowlevelError("The markup is out of step with the printer: tokens left over");

  DecompiledFunction result;
  result.address = function->getAddress().getOffset();
  result.name = function->getDisplayName();
  result.lines = std::move(builder.lines);
  // The printer starts a function with a line break and ends it with one.
  if (!result.lines.empty() && blank(result.lines.front())) result.lines.erase(result.lines.begin());
  while (!result.lines.empty() && blank(result.lines.back())) result.lines.pop_back();
  return result;
}

}  // namespace host
