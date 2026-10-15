#include "declarations.hh"

#include <cctype>
#include <set>
#include <sstream>

#include "funcdata.hh"
#include "grammar.hh"

namespace host {

using namespace ghidra;

namespace {

struct Token {
  std::string text;
  int line;
};

struct Declaration {
  std::vector<Token> tokens;
  int line = 0;
  /// Where it is in the source, its ';' left out.
  size_t start = 0, end = 0;
};

bool identifierStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool identifierPart(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

/// The tokens of source, without comments or preprocessor lines, cut into
/// declarations at each ';' outside braces. A declaration missing its final
/// ';' still counts. An unterminated comment is reported.
std::vector<Declaration> split(const std::string &source, std::vector<std::string> &warnings) {
  std::vector<Declaration> declarations;
  Declaration current;
  int depth = 0;
  int line = 1;
  bool lineStart = true;
  size_t i = 0, n = source.size();
  auto add = [&](size_t start) {
    std::string text = source.substr(start, i - start);
    if (text == ";" && depth == 0) {
      if (!current.tokens.empty()) declarations.push_back(std::move(current));
      current = Declaration();
      return;
    }
    if (current.tokens.empty()) {
      current.line = line;
      current.start = start;
    }
    current.end = i;
    if (text == "{") ++depth;
    if (text == "}" && depth > 0) --depth;
    current.tokens.push_back({std::move(text), line});
  };
  while (i < n) {
    char c = source[i];
    if (c == '\n') {
      ++line;
      lineStart = true;
      ++i;
      continue;
    }
    if (std::isspace((unsigned char)c)) {
      ++i;
      continue;
    }
    if (lineStart && c == '#') {  // a preprocessor line, with its continuations
      while (i < n && source[i] != '\n') {
        if (source[i] == '\\' && i + 1 < n && source[i + 1] == '\n') {
          ++line;
          ++i;
        }
        ++i;
      }
      continue;
    }
    lineStart = false;
    if (c == '/' && i + 1 < n && source[i + 1] == '*') {
      size_t end = source.find("*/", i + 2);
      if (end == std::string::npos) {
        warnings.push_back("line " + std::to_string(line) + ": Unterminated comment");
        break;
      }
      for (size_t at = i; at < end; ++at)
        if (source[at] == '\n') ++line;
      i = end + 2;
      continue;
    }
    if (c == '/' && i + 1 < n && source[i + 1] == '/') {
      while (i < n && source[i] != '\n') ++i;
      continue;
    }
    size_t start = i;
    if (identifierStart(c) || std::isdigit((unsigned char)c)) {
      while (i < n && (identifierPart(source[i]) || source[i] == '.')) ++i;
    } else if (c == '\'' || c == '"') {
      for (++i; i < n && source[i] != c && source[i] != '\n'; ++i)
        if (source[i] == '\\') ++i;
      if (i < n && source[i] == c) ++i;
    } else if (source.compare(i, 3, "...") == 0) {
      i += 3;
    } else if (source.compare(i, 2, "::") == 0) {
      i += 2;
    } else {
      ++i;
    }
    add(start);
  }
  if (!current.tokens.empty()) declarations.push_back(std::move(current));
  return declarations;
}

/// Removes the attributes CParse does not read from tokens, saying whether
/// one of them marked the declaration noreturn.
bool removeAttributes(std::vector<Token> &tokens) {
  bool noreturn = false;
  std::vector<Token> kept;
  auto marksNoreturn = [](const std::string &word) { return word == "noreturn" || word == "__noreturn__"; };
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string &word = tokens[i].text;
    if (word == "_Noreturn") {
      noreturn = true;
      continue;
    }
    bool attribute = (word == "__attribute__" || word == "__attribute") && i + 1 < tokens.size() &&
                     tokens[i + 1].text == "(";
    bool bracketed = word == "[" && i + 1 < tokens.size() && tokens[i + 1].text == "[";
    if (attribute || bracketed) {
      // Skip the balanced (( ... )) or [[ ... ]] that follows.
      const char *open = attribute ? "(" : "[";
      const char *close = attribute ? ")" : "]";
      size_t at = attribute ? i + 1 : i;
      int depth = 0;
      for (; at < tokens.size(); ++at) {
        if (tokens[at].text == open) ++depth;
        else if (tokens[at].text == close && --depth == 0) break;
        else if (marksNoreturn(tokens[at].text)) noreturn = true;
      }
      i = at;
      continue;
    }
    Token token = tokens[i];
    if (word == "__restrict" || word == "__restrict__") token.text = "restrict";
    else if (word == "__const" || word == "__const__") token.text = "const";
    else if (word == "__inline" || word == "__inline__") token.text = "inline";
    else if (word == "__volatile__") token.text = "volatile";
    kept.push_back(std::move(token));
  }
  tokens = std::move(kept);
  return noreturn;
}

/// Replaces each run of C type words (`unsigned long int`) with the name of
/// the core type of that size and signedness.
void nameTypes(std::vector<Token> &tokens, TypeFactory &types, const DataOrganization &org) {
  static const std::set<std::string> words = {"signed", "unsigned", "short", "long", "int",
                                              "char", "double", "float", "_Bool"};
  std::vector<Token> named;
  for (size_t i = 0; i < tokens.size();) {
    if (words.count(tokens[i].text) == 0) {
      named.push_back(tokens[i++]);
      continue;
    }
    int line = tokens[i].line;
    bool isUnsigned = false, isSigned = false, isChar = false, isDouble = false, isFloat = false,
         isBool = false;
    int shorts = 0, longs = 0;
    for (; i < tokens.size() && words.count(tokens[i].text) != 0; ++i) {
      const std::string &word = tokens[i].text;
      if (word == "unsigned") isUnsigned = true;
      else if (word == "signed") isSigned = true;
      else if (word == "short") ++shorts;
      else if (word == "long") ++longs;
      else if (word == "char") isChar = true;
      else if (word == "double") isDouble = true;
      else if (word == "float") isFloat = true;
      else if (word == "_Bool") isBool = true;
    }
    std::string name;
    if (isBool) name = "bool";
    else if (isFloat) name = "float";
    else if (isDouble) name = types.getBase(longs > 0 ? org.longDoubleSize : org.doubleSize, TYPE_FLOAT)->getName();
    else if (isChar && isUnsigned) name = types.getBase(org.charSize, TYPE_UINT)->getName();
    else if (isChar && isSigned) name = types.getBaseNoChar(org.charSize, TYPE_INT)->getName();
    else if (isChar) name = "char";
    else {
      int size = shorts > 0 ? org.shortSize : longs > 1 ? org.longLongSize : longs == 1 ? org.longSize : org.intSize;
      name = types.getBase(size, isUnsigned ? TYPE_UINT : TYPE_INT)->getName();
    }
    named.push_back({name, line});
  }
  tokens = std::move(named);
}

/// Rewrites each reference to a struct or union by its tag (`struct _IO_FILE`
/// not followed by a body) as the bare name CParse reads, creating the type,
/// incomplete, when it is not defined yet. CParse lexes a known tag as a type
/// name, which its grammar does not take after `struct`.
void nameTags(std::vector<Token> &tokens, TypeFactory &types) {
  std::vector<Token> named;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string &tag = tokens[i].text;
    bool reference = (tag == "struct" || tag == "union") && i + 1 < tokens.size() &&
                     identifierStart(tokens[i + 1].text[0]) &&
                     !(i + 2 < tokens.size() && tokens[i + 2].text == "{");
    if (!reference) {
      named.push_back(tokens[i]);
      continue;
    }
    const std::string &name = tokens[i + 1].text;
    if (types.findByName(name) == nullptr) {
      if (tag == "struct") types.getTypeStruct(name);
      else types.getTypeUnion(name);
    }
    named.push_back(tokens[++i]);
  }
  tokens = std::move(named);
}

/// The tokens as CParse reads them. A line break goes only after ';', '{' or
/// ',' (keeping lines within CParse's buffer): CParse's lexer fails on an
/// identifier that ends a line.
std::string join(const std::vector<Token> &tokens) {
  std::string text;
  for (const Token &token : tokens) {
    if (!text.empty()) text += (text.back() == ';' || text.back() == '{' || text.back() == ',') ? '\n' : ' ';
    text += token.text;
  }
  return text;
}

/// The declaration as written, on one line, to quote in a message.
std::string quote(const std::string &source, const Declaration &declaration) {
  std::string text;
  for (size_t i = declaration.start; i < declaration.end; ++i) {
    char c = source[i];
    if (std::isspace((unsigned char)c)) {
      if (!text.empty() && text.back() != ' ') text += ' ';
    } else {
      text += c;
    }
  }
  if (text.size() > 80) text = text.substr(0, 77) + "...";
  return text + ";";
}

/// CParse's message without its position, which is in the stream it was given
/// rather than the source, and with what it read last instead: "Syntax error
/// after `size_tt`".
std::string parseError(const std::string &error) {
  std::istringstream lines(error);
  std::string message, read;
  std::getline(lines, message);
  std::getline(lines, read);
  size_t position = message.find(" at line ");
  if (position != std::string::npos) message.erase(position);
  size_t last = read.find_last_not_of(" \t");
  if (last == std::string::npos) return message + " at the start";
  read.erase(last + 1);
  size_t first = read.find_last_of(" \t");
  return message + " after `" + read.substr(first == std::string::npos ? 0 : first + 1) + "`";
}

std::vector<Symbol *> symbolsNamed(Architecture &architecture, const std::string &name) {
  std::vector<Symbol *> symbols;
  architecture.symboltab->getGlobalScope()->queryByName(name, symbols);
  return symbols;
}

/// Applies one parsed declarator; returns a message when it cannot be.
std::string apply(Architecture &architecture, TypeDeclarator *declarator, bool noreturn) {
  if (!declarator->isValid()) return "Not a valid declaration";
  const std::string &name = declarator->getIdentifier();
  if (declarator->hasProperty(CParse::f_typedef)) {
    Datatype *type = declarator->buildType(&architecture);
    if (name.empty()) return "Missing identifier for typedef";
    if (type->getMetatype() == TYPE_STRUCT || type->getMetatype() == TYPE_UNION) {
      if (type->getName().empty()) architecture.types->setName(type, name);
      else architecture.types->getTypedef(type, name, 0, 0);
    } else {
      architecture.types->getTypedef(type, name, 0, 0);
    }
    return noreturn ? "Only a function can be marked noreturn" : "";
  }
  PrototypePieces pieces;
  if (declarator->getPrototype(pieces, &architecture)) {
    for (Symbol *symbol : symbolsNamed(architecture, name)) {
      FunctionSymbol *function = dynamic_cast<FunctionSymbol *>(symbol);
      if (function == nullptr) continue;
      FuncProto &prototype = function->getFunction()->getFuncProto();
      prototype.setPieces(pieces);
      if (noreturn) prototype.setNoReturn(true);
    }
    return "";
  }
  if (noreturn) return "Only a function can be marked noreturn";
  if (name.empty()) return "";  // a struct, union or enum
  Datatype *type = declarator->buildType(&architecture);
  for (Symbol *symbol : symbolsNamed(architecture, name)) {
    if (dynamic_cast<FunctionSymbol *>(symbol) != nullptr) continue;
    Scope *scope = symbol->getScope();
    scope->retypeSymbol(symbol, type);
    scope->setAttribute(symbol, Varnode::typelock);
  }
  return "";
}

}  // namespace

std::vector<std::string> applyDeclarations(Architecture &architecture, const DataOrganization &organization,
                                           const std::string &source) {
  std::vector<std::string> warnings;
  for (Declaration &declaration : split(source, warnings)) {
    std::string quoted = quote(source, declaration);
    std::string problem;
    try {
      bool noreturn = removeAttributes(declaration.tokens);
      nameTypes(declaration.tokens, *architecture.types, organization);
      nameTags(declaration.tokens, *architecture.types);
      if (declaration.tokens.empty()) continue;
      std::istringstream text(join(declaration.tokens) + " ;");
      CParse parser(&architecture, 4096);
      if (!parser.parseStream(text, CParse::doc_declaration)) {
        problem = parseError(parser.getError());
        for (size_t i = 0; i + 1 < declaration.tokens.size(); ++i)
          if (declaration.tokens[i].text == "(" && declaration.tokens[i + 1].text == ")")
            problem = "An empty parameter list cannot be read: write (void) or the parameters";
      } else {
        std::vector<TypeDeclarator *> *declarators = parser.getResultDeclarations();
        if (declarators == nullptr || declarators->empty()) problem = "Not a declaration";
        else
          for (TypeDeclarator *declarator : *declarators) {
            problem = apply(architecture, declarator, noreturn);
            if (!problem.empty()) break;
          }
      }
    } catch (const LowlevelError &error) {
      problem = error.explain;
    }
    if (!problem.empty())
      warnings.push_back("line " + std::to_string(declaration.line) + ": " + problem + " (in `" + quoted + "`)");
  }
  return warnings;
}

}  // namespace host
