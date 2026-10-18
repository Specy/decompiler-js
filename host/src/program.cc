#include "program.hh"

#include <algorithm>
#include <chrono>
#include <map>
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

  /// Each pointer becomes an external reference at its slot, as Ghidra's Java
  /// side sends a GOT entry that points at an external function: a value of
  /// type `code *` that the decompiler resolves to the function at
  /// slotFunctions gives for its slot when the program calls through it
  /// (ActionDeindirect),
  /// whatever the slot's bytes hold. The slot's name is the data name given
  /// at its address, taken out of data, or else the decompiler's own name for
  /// a global `code *` there (`pcRam0000000000003fd8`).
  void addPointers(const std::vector<PointerName> &pointers, const std::map<uint64_t, uint64_t> &slotFunctions,
                   std::vector<DataName> &data) {
    Scope *global = symboltab->getGlobalScope();
    AddrSpace *space = getDefaultDataSpace();
    std::map<uint64_t, std::string> slotNames;
    std::vector<DataName> kept;
    std::set<uint64_t> slots;
    for (const PointerName &pointer : pointers) slots.insert(pointer.address);
    for (DataName &entry : data) {
      if (slots.count(entry.address) == 0) kept.push_back(std::move(entry));
      else slotNames.emplace(entry.address, std::move(entry.name));
    }
    data = std::move(kept);
    Datatype *codePointer = types->getTypePointer(space->getAddrSize(), types->getTypeCode(), space->getWordSize());
    for (const PointerName &pointer : pointers) {
      Address slot(space, pointer.address);
      if (global->queryFunction(slot) != nullptr) continue;  // the function's name wins
      auto named = slotNames.find(pointer.address);
      std::string name;
      if (named != slotNames.end()) {
        name = named->second;
      } else {
        int4 index = 0;
        name = global->buildVariableName(slot, Address(), codePointer, index, Varnode::persist);
      }
      global->addExternalRef(slot, Address(getDefaultCodeSpace(), slotFunctions.at(pointer.address)), name);
    }
  }

  /// Each data name becomes a global symbol whose name is locked, as Ghidra's
  /// Java side sends a labelled Data: a string's as an array of char with its
  /// type locked, too; another with a known size as undefined1, 2, 4 or 8, or
  /// an array of undefined1, the type left for the decompiler to refine; one
  /// without a size as a single undefined1. A string without a name gets no
  /// symbol: where a char pointer is expected the decompiler prints it as a
  /// literal anyway, as it does any string in read-only memory.
  void addData(const std::vector<DataName> &data, const std::vector<StringRange> &strings) {
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
      SymbolEntry *symbol = global->addSymbol(entry.name, type, address, Address());
      global->setAttribute(symbol->getSymbol(), locks);
    }
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

/// Clears a function's analysis when it goes out of scope, however its
/// decompilation ended. Nothing reuses it, since each decompilation starts
/// afresh, and kept it is costly: decompiling each of hello-atexit-static's
/// 1,087 functions once left 944 MB of analyses in the heap.
struct ClearAnalysisAfter {
  Architecture &architecture;
  Funcdata *function;
  ~ClearAnalysisAfter() {
    try {
      architecture.clearAnalysis(function);
    } catch (...) {
    }
  }
};

[[noreturn]] void rethrow() {
  try {
    throw;
  } catch (const LowlevelError &error) {
    throw std::runtime_error(error.explain);
  } catch (const DecoderError &error) {
    throw std::runtime_error(error.explain);
  }
}

/// The address of the function each pointer names, by name: the image's
/// function of that name, or else a placeholder added to the image's functions
/// past every address the image describes, with no bytes, so that the
/// prototypes bind to it by name like any function's. Placeholders are 16
/// bytes apart; their slots are recorded in placeholders.
std::map<uint64_t, uint64_t> pointedFunctions(ProgramImage &image, std::map<uint64_t, uint64_t> &placeholders) {
  std::map<std::string, uint64_t> byName;
  for (const FunctionName &function : image.functions) byName.emplace(function.name, function.address);
  uint64_t end = 0;
  for (const Region &region : image.regions) end = std::max(end, region.address + region.bytes.size());
  for (const FunctionName &function : image.functions) end = std::max(end, function.address + 1);
  for (const DataName &entry : image.data) end = std::max(end, entry.address + std::max<uint64_t>(entry.size, 1));
  for (const StringRange &string : image.strings) end = std::max(end, string.address + string.length);
  for (const PointerName &pointer : image.pointers) end = std::max(end, pointer.address + 8);
  uint64_t next = (end + 15) & ~(uint64_t)15;
  std::map<uint64_t, uint64_t> slotFunctions;
  for (const PointerName &pointer : image.pointers) {
    auto known = byName.find(pointer.name);
    if (known == byName.end()) {
      known = byName.emplace(pointer.name, next).first;
      image.functions.push_back({next, pointer.name});
      placeholders.emplace(next, pointer.address);
      next += 16;
    }
    slotFunctions.emplace(pointer.address, known->second);
  }
  return slotFunctions;
}

}  // namespace

Program::Program(ProgramImage image, const std::string &languageId) {
  try {
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
    // protoeval name what the architecture already uses.
    architecture->options->set(ELEM_READONLY.getId(), "on");
    architecture->readLoaderSymbols("::");
    architecture->addPointers(pointers, slotFunctions, data);
    architecture->addData(data, strings);
    for (const auto &[placeholder, slot] : placeholders) architecture->markup->aliasTarget(placeholder, slot);
    declarationWarnings = applyDeclarations(*architecture, architecture->organization, prototypes);
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

DecompiledFunction Program::decompile(uint64_t address, uint32_t maxInstructions, uint32_t timeLimitMs) {
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
    architecture->useTranslator();
    Action *action = architecture->allacts.getCurrent();
    action->reset(*function);
    {
      ArmedDeadline armed(timeLimitMs);
      if (action->perform(*function) < 0 || !function->isProcComplete())
        throw std::runtime_error("Decompilation of " + function->getName() + " did not complete");
    }
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
