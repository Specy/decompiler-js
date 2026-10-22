#include "program.hh"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#include "core_types.hh"
#include "declarations.hh"
#include "image_load_image.hh"
#include "libdecomp.hh"

#ifndef GHIDRA_VERSION
#define GHIDRA_VERSION "unknown"
#endif

namespace host {

using namespace ghidra;

/// The global scope and each namespace scope of a program, their symbols
/// numbered by one count the program keeps. Ghidra numbers a scope's symbols
/// by the low 16 bits of the scope's id and a count of the scope's own
/// (ScopeInternal::addSymbolInternal), and a function's local scope takes its
/// function's id: the k-th functions of two namespaces whose ids share those
/// bits had one id, and the second one's local scope failed to attach
/// ("Duplicate scope id"). Ghidra's Java side sends each symbol's id; here no
/// two symbols share one, however many namespaces there are.
class NamespaceScope : public ScopeInternal {
  uint8 &symbolCount;

 protected:
  Scope *buildSubScope(uint8 id, const string &name) override {
    return new NamespaceScope(id, name, glb, symbolCount);
  }

  void addSymbolInternal(Symbol *symbol) override {
    nextUniqueId = symbolCount++;
    ScopeInternal::addSymbolInternal(symbol);
  }

 public:
  NamespaceScope(uint8 id, const string &name, Architecture *architecture, uint8 &symbolCount)
      : ScopeInternal(id, name, architecture), symbolCount(symbolCount) {}
};

/// The architecture of a program image: Ghidra's SleighArchitecture with a
/// load image served from memory, as its RawBinaryArchitecture and
/// XmlArchitecture are with one served from a file, and Ghidra's Java names
/// for the core types.
class HostArchitecture : public SleighArchitecture {
  ProgramImage pendingImage;

  /// The architecture each of SleighArchitecture's shared translators (one
  /// per language) was last pointed at.
  static std::map<const Translate *, const HostArchitecture *> &translatorUsers() {
    static std::map<const Translate *, const HostArchitecture *> users;
    return users;
  }

  // SleighArchitecture resets the language's translator to this
  // architecture's load image and context.
  Translate *buildTranslator(DocumentStorage &store) override {
    Translate *translator = SleighArchitecture::buildTranslator(store);
    translatorUsers()[translator] = this;
    return translator;
  }

  void buildLoader(DocumentStorage &store) override {
    collectSpecFiles(*errorstream);
    loader = new ImageLoadImage(std::move(pendingImage), getTarget());
  }

  void resolveArchitecture(void) override {
    archid = getTarget();
    SleighArchitecture::resolveArchitecture();
  }

  /// How many symbols the global scope and the namespaces have numbered
  /// (NamespaceScope).
  uint8 symbolCount = 0;

  /// The id of the next namespace scopeOf makes; 0 is the global scope's.
  uint8 nextScopeId = 1;

  /// Each namespace scopeOf has made, by its parent and its name.
  std::map<std::pair<Scope *, std::string>, Scope *> namespaces;

  // As Architecture::buildDatabase, but the global scope a NamespaceScope,
  // and the database not taking a namespace's id for a hash of its name
  // (idByNameHash), since scopeOf gives the ids.
  Scope *buildDatabase(DocumentStorage &store) override {
    symboltab = new Database(this, false);
    Scope *global = new NamespaceScope(0, "", this, symbolCount);
    symboltab->attachScope(global, nullptr);
    return global;
  }

  // buildSpecFile has registered the compiler specification by then.
  void buildCoreTypes(DocumentStorage &store) override {
    organization = readDataOrganization(store.getTag("compiler_spec"));
    buildGhidraCoreTypes(*types, organization);
  }

  void postSpecFile(void) override {
    Architecture::postSpecFile();
    static_cast<ImageLoadImage *>(loader)->attachToSpace(getDefaultCodeSpace());
  }

 public:
  HostArchitecture(ProgramImage image, const std::string &languageId, std::ostream *errors)
      : SleighArchitecture("image", languageId, errors), pendingImage(std::move(image)) {
    markup = new MarkupPrintC(this);
    for (PrintLanguage *&language : printlist)
      if (language == print) {
        delete language;
        language = markup;
      }
    print = markup;
  }

  /// The printer, Ghidra's C printer recording what each token stands for.
  MarkupPrintC *markup;

  DataOrganization organization;

  ~HostArchitecture() override {
    auto user = translatorUsers().find(translate);
    if (user != translatorUsers().end() && user->second == this) translatorUsers().erase(user);
  }

  /// Points the language's translator, which every program of the language
  /// shares, back at this program's bytes and context when another program
  /// has been opened or decompiled since this one last used it
  /// (patches/0002-sleigh-resume.patch).
  void useTranslator() {
    const HostArchitecture *&user = translatorUsers()[translate];
    if (user == this) return;
    const_cast<Sleigh *>(static_cast<const Sleigh *>(translate))->resume(loader, context);
    user = this;
  }

  const ImageLoadImage &image() const { return *static_cast<const ImageLoadImage *>(loader); }

  /// The addresses each namespace's functions start at and its data cover,
  /// registered with it by ownAddresses().
  std::map<Scope *, RangeList> owned;

  /// Where each function, pointer and datum of the image starts, by space.
  std::map<AddrSpace *, std::set<uint64_t>> starts;

  /// The namespace scope a path names, each namespace made as needed, as
  /// Ghidra's readLoaderSymbols makes them from a name split at its `::`s,
  /// but with the next of the host's ids, as Ghidra's Java side sends its
  /// namespaces' ids, where readLoaderSymbols hashes the name and its
  /// parent's id (Scope::hashScopeName), which two names can share; the
  /// global scope for an empty path. A namespace's name is never split.
  Scope *scopeOf(const NamespacePath &path) {
    Scope *scope = symboltab->getGlobalScope();
    for (const std::string &name : path) {
      Scope *&child = namespaces[{scope, name}];
      if (child == nullptr) child = symboltab->findCreateScope(nextScopeId++, name, scope);
      scope = child;
    }
    return scope;
  }

  /// Records where the image's functions, pointers and data start, which
  /// own() stops a namespace's bytes at.
  void findStarts(const std::vector<PointerName> &pointers, const std::vector<DataName> &data) {
    for (const FunctionName &function : image().functions()) starts[getDefaultCodeSpace()].insert(function.address);
    for (const PointerName &pointer : pointers) starts[getDefaultDataSpace()].insert(pointer.address);
    for (const DataName &entry : data) starts[getDefaultDataSpace()].insert(entry.address);
  }

  /// Records that the namespace scope owns the bytes from first to last, or
  /// up to where the next function, pointer or datum starts: a namespace
  /// owning that one's address would hide it from lookups by address, which
  /// search from the owner up and stop at the first scope owning it
  /// (Scope::stackFunction, stackContainer).
  void own(Scope *scope, AddrSpace *space, uint64_t first, uint64_t last) {
    if (scope == symboltab->getGlobalScope()) return;
    const std::set<uint64_t> &entries = starts[space];
    auto next = entries.upper_bound(first);
    if (next != entries.end() && *next <= last) last = *next - 1;
    owned[scope].insertRange(space, first, last);
  }

  /// Registers with each namespace the addresses it owns, so that a lookup
  /// by address (Scope::queryFunction, queryContainer, through
  /// Database::mapScope) starts from that namespace and finds its symbols on
  /// the way up to the global scope. A namespace readLoaderSymbols creates
  /// owns no address, which hides its functions from every lookup by
  /// address (a call's target, decompile's entry); Ghidra's Java side
  /// answers those lookups itself.
  void ownAddresses() {
    for (const auto &[scope, ranges] : owned) symboltab->setRange(scope, ranges);
  }

  /// Each function of the image becomes a function symbol in the namespace
  /// its path names, by its name, which is never split; done where Ghidra's
  /// readLoaderSymbols would split names at `::`.
  void addFunctions(SymbolsByBinding &symbols) {
    loadersymbols_parsed = true;
    AddrSpace *space = getDefaultCodeSpace();
    for (const FunctionName &function : image().functions()) {
      Scope *scope = scopeOf(function.path);
      FunctionSymbol *symbol = scope->addFunction(Address(space, function.address), function.name);
      own(scope, space, function.address, function.address);
      symbols[function.binding()].push_back(symbol);
    }
    ownAddresses();
  }

  /// Each pointer becomes an external reference at its slot, as Ghidra's Java
  /// side sends a GOT entry that points at an external function: a value of
  /// type `code *` that the decompiler resolves to the function at
  /// slotFunctions gives for its slot when the program calls through it
  /// (ActionDeindirect),
  /// whatever the slot's bytes hold. The slot's name is the data name given
  /// at its address, taken out of data, in its namespace, or else the
  /// decompiler's own name for a global `code *` there
  /// (`pcRam0000000000003fd8`).
  void addPointers(const std::vector<PointerName> &pointers, const std::map<uint64_t, uint64_t> &slotFunctions,
                   std::vector<DataName> &data, SymbolsByBinding &symbols) {
    Scope *global = symboltab->getGlobalScope();
    AddrSpace *space = getDefaultDataSpace();
    std::map<uint64_t, DataName> slotNames;
    std::vector<DataName> kept;
    std::set<uint64_t> slots;
    for (const PointerName &pointer : pointers) slots.insert(pointer.address);
    for (DataName &entry : data) {
      if (slots.count(entry.address) == 0) kept.push_back(std::move(entry));
      else slotNames.emplace(entry.address, std::move(entry));
    }
    data = std::move(kept);
    Datatype *codePointer = types->getTypePointer(space->getAddrSize(), types->getTypeCode(), space->getWordSize());
    for (const PointerName &pointer : pointers) {
      Address slot(space, pointer.address);
      if (global->queryFunction(slot) != nullptr) continue;  // the function's name wins
      Address function(getDefaultCodeSpace(), slotFunctions.at(pointer.address));
      auto named = slotNames.find(pointer.address);
      if (named == slotNames.end()) {
        int4 index = 0;
        std::string name = global->buildVariableName(slot, Address(), codePointer, index, Varnode::persist);
        global->addExternalRef(slot, function, name);
        continue;
      }
      const DataName &entry = named->second;
      Scope *scope = scopeOf(entry.path);
      symbols[entry.binding()].push_back(scope->addExternalRef(slot, function, entry.name));
      own(scope, space, pointer.address, pointer.address + space->getAddrSize() - 1);
    }
    ownAddresses();
  }

  /// Each data name becomes a symbol in the namespace its path names, its
  /// name locked, as Ghidra's
  /// Java side sends a labelled Data: a string's as an array of char with its
  /// type locked, too; another with a known size as undefined1, 2, 4 or 8, or
  /// an array of undefined1, the type left for the decompiler to refine; one
  /// without a size as a single undefined1. A string without a name gets no
  /// symbol: where a char pointer is expected the decompiler prints it as a
  /// literal anyway, as it does any string in read-only memory.
  void addData(const std::vector<DataName> &data, const std::vector<StringRange> &strings,
               SymbolsByBinding &symbols) {
    Scope *global = symboltab->getGlobalScope();
    AddrSpace *space = getDefaultDataSpace();
    std::map<uint64_t, uint64_t> stringLengths;
    for (const StringRange &string : strings) stringLengths.emplace(string.address, string.length);
    Datatype *character = types->getTypeChar(types->getSizeOfChar());
    for (const DataName &entry : data) {
      Address address(space, entry.address);
      if (global->queryFunction(address) != nullptr) continue;  // the function's name wins
      Datatype *type;
      uint4 locks = Varnode::namelock;
      auto string = stringLengths.find(entry.address);
      if (string != stringLengths.end()) {
        type = types->getTypeArray((int4)string->second, character);
        locks |= Varnode::typelock;
      } else if (entry.size == 1 || entry.size == 2 || entry.size == 4 || entry.size == 8) {
        type = types->getBase((int4)entry.size, TYPE_UNKNOWN);
      } else if (entry.size > 0) {
        type = types->getTypeArray((int4)entry.size, types->getBase(1, TYPE_UNKNOWN));
      } else {
        type = types->getBase(1, TYPE_UNKNOWN);
      }
      Scope *scope = scopeOf(entry.path);
      SymbolEntry *symbol = scope->addSymbol(entry.name, type, address, Address());
      scope->setAttribute(symbol->getSymbol(), locks);
      symbols[entry.binding()].push_back(symbol->getSymbol());
      own(scope, space, entry.address, entry.address + std::max<uint64_t>(type->getSize(), 1) - 1);
    }
    ownAddresses();
  }
};

void startLibrary(const std::vector<std::string> &specDirectories) {
  static bool started = false;
  if (!started) {
    startDecompilerLibrary(specDirectories);
    started = true;
    return;
  }
  for (const std::string &directory : specDirectories)
    SleighArchitecture::specpaths.addDir2Path(directory);
}

std::string ghidraVersion() { return GHIDRA_VERSION; }

const char *const NAMESPACE_STRATEGIES[3] = {"minimal", "all", "none"};

namespace {

using Clock = std::chrono::steady_clock;

/// Thrown by checkDeadline once the decompilation under way has passed its
/// deadline. It is a LowlevelError so that wherever Ghidra catches a failed
/// step and carries on (a jump table it could not recover), it cleans up as
/// for any other; the next action's checkpoint throws again, so the
/// decompilation ends however often it is caught.
struct TimeLimitError : public LowlevelError {
  TimeLimitError() : LowlevelError("The time limit has passed") {}
};

/// The deadline of the decompilation under way, which Ghidra's actions check
/// through Action::checkpoint (patches/0001-action-checkpoint.patch). The
/// library runs one decompilation at a time.
struct Deadline {
  bool armed = false;
  bool passed = false;
  Clock::time_point end;
};

Deadline deadline;

void checkDeadline(Funcdata &) {
  if (!deadline.armed) return;
  if (!deadline.passed && Clock::now() < deadline.end) return;
  deadline.passed = true;
  throw TimeLimitError();
}

/// Arms the deadline for one decompilation, and disarms it however that ends.
struct ArmedDeadline {
  explicit ArmedDeadline(uint32_t limitMs) {
    Action::checkpoint = &checkDeadline;
    deadline.passed = false;
    deadline.end = Clock::now() + std::chrono::milliseconds(limitMs);
    deadline.armed = true;
  }
  ~ArmedDeadline() { deadline.armed = false; }
};

/// "30 seconds", "1 second", "250 ms".
std::string duration(uint32_t milliseconds) {
  if (milliseconds % 1000 != 0) return std::to_string(milliseconds) + " ms";
  return std::to_string(milliseconds / 1000) + (milliseconds == 1000 ? " second" : " seconds");
}

/// What FlowInfo throws past Architecture::max_instructions (flow.cc).
const char *const TOO_MANY_INSTRUCTIONS = "Flow exceeded maximum allowable instructions";

/// The comment types a caller's annotations add (Annotations), which the
/// comment database keeps across decompilations until they are cleared;
/// Architecture::clearAnalysis clears only the decompiler's warnings.
const uint4 CALLER_COMMENTS = Comment::user1 | Comment::user2 | Comment::user3 | Comment::header;

/// Clears a function's analysis, and the comments its annotations added,
/// when it goes out of scope, however its decompilation ended. Nothing
/// reuses the analysis, since each decompilation starts afresh, and kept it
/// is costly: decompiling each of hello-atexit-static's 1,087 functions once
/// left 944 MB of analyses in the heap.
struct ClearAnalysisAfter {
  Architecture &architecture;
  Funcdata *function;
  ~ClearAnalysisAfter() {
    try {
      architecture.clearAnalysis(function);
      architecture.commentdb->clearType(function->getAddress(), CALLER_COMMENTS);
    } catch (...) {
    }
  }
};

/// A comment's text as a C comment holds it: each `*/`, which would end the
/// comment early, written `* /`.
std::string commentText(const std::string &text) {
  std::string escaped;
  for (size_t i = 0; i < text.size(); ++i) {
    escaped += text[i];
    if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') escaped += ' ';
  }
  return escaped;
}

/// Puts the annotations' comments in the comment database for the function,
/// as Ghidra's Java side sends a function's comments for each decompilation
/// (comment_ghidra.cc): a line comment as a PRE comment (user2), which the
/// printer places before the statement whose code holds its address
/// (CommentSorter::findPosition), and the function comment as its plate
/// comment (header), above the declaration and before the decompiler's own
/// header warnings, which the analysis adds after it. An empty text adds
/// nothing.
void addComments(Architecture &architecture, const Funcdata &function, const Annotations &annotations) {
  Address entry = function.getAddress();
  architecture.commentdb->clearType(entry, CALLER_COMMENTS);
  for (const Annotations::LineComment &comment : annotations.comments) {
    if (comment.text.empty()) continue;
    architecture.commentdb->addComment(Comment::user2, entry,
                                       Address(architecture.getDefaultCodeSpace(), comment.address),
                                       commentText(comment.text));
  }
  if (!annotations.functionComment.empty())
    architecture.commentdb->addComment(Comment::header, entry, entry, commentText(annotations.functionComment));
}

/// Renames each local variable or parameter whose id (variableId) the
/// annotations name, after the analysis and before printing, each name made
/// unique in the function's scope (`name_00` after a `name`), in the order
/// given. The code and every other name stay as the analysis left them:
/// Ghidra's own route, name recommendations that ActionNameVars applies
/// during the analysis, renumbers other default names and misses
/// parameters the code never reads and temporaries whose hash changes after
/// it. A parameter named by a declaration (its name locked) keeps its name,
/// and an id naming no variable is passed over. Clearing the analysis
/// removes the local scope's symbols but those whose type is locked, and
/// takes back the names of those it keeps (ScopeInternal::clearUnlocked), so
/// no name carries over to the next decompilation.
void nameVariables(Funcdata &function, const std::vector<Annotations::Local> &locals) {
  if (locals.empty()) return;
  ScopeLocal *scope = function.getScopeLocal();
  std::map<std::string, Symbol *> byId;
  auto add = [&](Symbol *symbol) {
    if (symbol == nullptr) return;
    int4 category = symbol->getCategory();
    if (category != Symbol::no_category && category != Symbol::function_parameter) return;
    std::string id = variableId(symbol, &function);
    if (!id.empty()) byId.emplace(id, symbol);
  };
  for (MapIterator iter = scope->begin(); iter != scope->end(); ++iter) add((*iter)->getSymbol());
  for (auto iter = scope->beginDynamic(); iter != scope->endDynamic(); ++iter) add((*iter).getSymbol());
  for (int4 i = 0; i < scope->getCategorySize(Symbol::function_parameter); ++i)
    add(scope->getCategorySymbol(Symbol::function_parameter, i));
  for (const Annotations::Local &local : locals) {
    auto found = byId.find(local.id);
    if (found == byId.end()) continue;
    Symbol *symbol = found->second;
    if (symbol->isNameLocked() || symbol->getName() == local.name) continue;
    scope->renameSymbol(symbol, scope->makeNameUnique(local.name));
  }
}

[[noreturn]] void rethrow() {
  try {
    throw;
  } catch (const LowlevelError &error) {
    throw std::runtime_error(error.explain);
  } catch (const DecoderError &error) {
    throw std::runtime_error(error.explain);
  }
}

/// The address of the function each pointer names: the image's first
/// function whose symbol (or name, without one) is the pointer's symbol when
/// it has one, else whose name and namespace path are the pointer's; or else
/// a placeholder with the pointer's name, path and symbol, added to the
/// image's functions past every address the image describes, with no bytes,
/// so that the prototypes bind to it like any function's. Placeholders are
/// 16 bytes apart; their slots are recorded in placeholders.
std::map<uint64_t, uint64_t> pointedFunctions(ProgramImage &image, std::map<uint64_t, uint64_t> &placeholders) {
  using QualifiedName = std::pair<NamespacePath, std::string>;
  std::map<std::string, uint64_t> bySymbol;
  std::map<QualifiedName, uint64_t> byName;
  for (const FunctionName &function : image.functions) {
    bySymbol.emplace(function.binding(), function.address);
    byName.emplace(QualifiedName(function.path, function.name), function.address);
  }
  uint64_t end = 0;
  for (const Region &region : image.regions) end = std::max(end, region.address + region.bytes.size());
  for (const FunctionName &function : image.functions) end = std::max(end, function.address + 1);
  for (const DataName &entry : image.data) end = std::max(end, entry.address + std::max<uint64_t>(entry.size, 1));
  for (const StringRange &string : image.strings) end = std::max(end, string.address + string.length);
  for (const PointerName &pointer : image.pointers) end = std::max(end, pointer.address + 8);
  uint64_t next = (end + 15) & ~(uint64_t)15;
  std::map<uint64_t, uint64_t> slotFunctions;
  for (const PointerName &pointer : image.pointers) {
    QualifiedName name(pointer.path, pointer.name);
    std::optional<uint64_t> known;
    if (pointer.symbol.empty()) {
      auto function = byName.find(name);
      if (function != byName.end()) known = function->second;
    } else {
      auto function = bySymbol.find(pointer.symbol);
      if (function != bySymbol.end()) known = function->second;
    }
    uint64_t address;
    if (known) {
      address = *known;
    } else {
      address = next;
      next += 16;
      FunctionName placeholder{address, pointer.name, pointer.path, pointer.symbol};
      bySymbol.emplace(placeholder.binding(), address);
      byName.emplace(name, address);
      image.functions.push_back(std::move(placeholder));
      placeholders.emplace(address, pointer.address);
    }
    slotFunctions.emplace(pointer.address, address);
  }
  return slotFunctions;
}

}  // namespace

Program::Program(ProgramImage image, const std::string &languageId, const std::string &namespaces) {
  try {
    if (std::find(std::begin(NAMESPACE_STRATEGIES), std::end(NAMESPACE_STRATEGIES), namespaces) ==
        std::end(NAMESPACE_STRATEGIES))
      throw std::invalid_argument("Unknown namespace strategy: " + namespaces);
    normalize(image);
    std::map<uint64_t, uint64_t> slotFunctions = pointedFunctions(image, placeholders);
    std::vector<DataName> data = std::move(image.data);
    std::vector<StringRange> strings = std::move(image.strings);
    std::vector<PointerName> pointers = std::move(image.pointers);
    std::string prototypes = std::move(image.prototypes);
    image.data.clear();
    image.strings.clear();
    image.pointers.clear();
    image.prototypes.clear();
    architecture.reset(new HostArchitecture(std::move(image), languageId, &messages));
    DocumentStorage store;
    architecture->init(store);
    // What Ghidra's DecompileOptions sends whatever the user's settings
    // (encode: readonly, setlanguage, protoeval); the others it sends only
    // when they differ from the decompiler's own defaults. setlanguage and
    // protoeval name what the architecture already uses. The namespace
    // strategy is the caller's, minimal (the default) unless it says
    // otherwise.
    architecture->options->set(ELEM_READONLY.getId(), "on");
    architecture->options->set(ELEM_NAMESPACESTRATEGY.getId(), namespaces);
    SymbolsByBinding symbols;
    architecture->findStarts(pointers, data);
    architecture->addFunctions(symbols);
    architecture->addPointers(pointers, slotFunctions, data, symbols);
    architecture->addData(data, strings, symbols);
    for (const auto &[placeholder, slot] : placeholders) architecture->markup->aliasTarget(placeholder, slot);
    declarationWarnings = applyDeclarations(*architecture, architecture->organization, symbols, prototypes);
  } catch (const std::invalid_argument &error) {
    throw std::runtime_error(error.what());
  } catch (...) {
    rethrow();
  }
}

Program::~Program() = default;

std::vector<std::string> Program::warnings() const {
  std::vector<std::string> all;
  std::istringstream lines(messages.str());
  for (std::string line; std::getline(lines, line);)
    if (!line.empty()) all.push_back(line);
  all.insert(all.end(), declarationWarnings.begin(), declarationWarnings.end());
  return all;
}

DecompiledFunction Program::decompile(uint64_t address, uint32_t maxInstructions, uint32_t timeLimitMs,
                                      const Annotations &annotations) {
  Funcdata *function = nullptr;
  deadline.passed = false;
  try {
    architecture->max_instructions = maxInstructions;
    Address entry(architecture->getDefaultCodeSpace(), address);
    function = placeholders.count(address) != 0 ? nullptr
                                                : architecture->symboltab->getGlobalScope()->queryFunction(entry);
    if (function == nullptr) {
      std::ostringstream message;
      message << "No function starts at 0x" << std::hex << address;
      throw std::runtime_error(message.str());
    }
    if (function->isProcStarted()) architecture->clearAnalysis(function);
    ClearAnalysisAfter cleared{*architecture, function};
    addComments(*architecture, *function, annotations);
    architecture->markup->setDisplayUnplaced(annotations.unplacedComments);
    architecture->useTranslator();
    Action *action = architecture->allacts.getCurrent();
    action->reset(*function);
    {
      ArmedDeadline armed(timeLimitMs);
      if (action->perform(*function) < 0 || !function->isProcComplete())
        throw std::runtime_error("Decompilation of " + function->getName() + " did not complete");
    }
    nameVariables(*function, annotations.locals);
    return architecture->markup->print(function);
  } catch (const std::runtime_error &) {
    throw;
  } catch (const LowlevelError &error) {
    // Ghidra may have caught the TimeLimitError and thrown another error
    // about the step it interrupted.
    if (deadline.passed)
      throw std::runtime_error(function->getName() + " took longer than " + duration(timeLimitMs) +
                               " to decompile");
    if (error.explain == TOO_MANY_INSTRUCTIONS)
      throw std::runtime_error(function->getName() + " has more than " + std::to_string(maxInstructions) +
                               " instructions");
    throw std::runtime_error(error.explain);
  } catch (...) {
    rethrow();
  }
}

void Program::writeSaveFile(std::ostream &out) const {
  try {
    // A function symbol nothing has asked for yet is saved as a shell, which
    // the console restores without its display name; asking for each
    // function first saves them whole.
    Scope *global = architecture->symboltab->getGlobalScope();
    for (const FunctionName &function : architecture->image().functions())
      global->queryFunction(Address(architecture->getDefaultCodeSpace(), function.address));
    out << "<xml_savefile name=\"image\" target=\"" << architecture->getTarget()
        << "\" adjustvma=\"0\">\n";
    architecture->image().writeXml(out);
    std::ostringstream state;
    XmlEncode encoder(state);
    architecture->types->encodeCoreTypes(encoder);
    architecture->encode(encoder);
    // ScopeInternal::encode leaves out the <symbollist> of a scope without
    // symbols (a function's local scope before it is decompiled), which
    // ScopeInternal::decode requires from an XML stream.
    std::string xml = state.str();
    for (size_t at = xml.find("<scope "); at != std::string::npos; at = xml.find("<scope ", at + 1)) {
      size_t close = xml.find("</scope>", at);
      if (close == std::string::npos) break;
      if (xml.substr(at, close - at).find("<symbollist") == std::string::npos)
        xml.insert(close, "<symbollist/>");
    }
    out << xml << "\n</xml_savefile>\n";
  } catch (...) {
    rethrow();
  }
}

}  // namespace host
