#include "program.hh"

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
      : SleighArchitecture("image", languageId, errors), pendingImage(std::move(image)) {}

  DataOrganization organization;

  const ImageLoadImage &image() const { return *static_cast<const ImageLoadImage *>(loader); }

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

[[noreturn]] void rethrow() {
  try {
    throw;
  } catch (const LowlevelError &error) {
    throw std::runtime_error(error.explain);
  } catch (const DecoderError &error) {
    throw std::runtime_error(error.explain);
  }
}

}  // namespace

Program::Program(ProgramImage image, const std::string &languageId) {
  try {
    normalize(image);
    std::vector<DataName> data = std::move(image.data);
    std::vector<StringRange> strings = std::move(image.strings);
    std::string prototypes = std::move(image.prototypes);
    image.data.clear();
    image.strings.clear();
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
    architecture->addData(data, strings);
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

std::string Program::decompile(uint64_t address) {
  try {
    Address entry(architecture->getDefaultCodeSpace(), address);
    Funcdata *function = architecture->symboltab->getGlobalScope()->queryFunction(entry);
    if (function == nullptr) {
      std::ostringstream message;
      message << "No function starts at 0x" << std::hex << address;
      throw std::runtime_error(message.str());
    }
    if (function->isProcStarted()) architecture->clearAnalysis(function);
    Action *action = architecture->allacts.getCurrent();
    action->reset(*function);
    if (action->perform(*function) < 0 || !function->isProcComplete())
      throw std::runtime_error("Decompilation of " + function->getName() + " did not complete");

    std::ostringstream text;
    architecture->print->setOutputStream(&text);
    architecture->print->docFunction(function);
    architecture->print->setOutputStream(nullptr);
    return text.str();
  } catch (const std::runtime_error &) {
    throw;
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
