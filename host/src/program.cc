#include "program.hh"

#include <stdexcept>

#include "image_load_image.hh"
#include "libdecomp.hh"

#ifndef GHIDRA_VERSION
#define GHIDRA_VERSION "unknown"
#endif

namespace host {

using namespace ghidra;

/// The architecture of a program image: Ghidra's SleighArchitecture with a
/// load image served from memory, as its RawBinaryArchitecture and
/// XmlArchitecture are with one served from a file.
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

  void postSpecFile(void) override {
    Architecture::postSpecFile();
    static_cast<ImageLoadImage *>(loader)->attachToSpace(getDefaultCodeSpace());
  }

 public:
  HostArchitecture(ProgramImage image, const std::string &languageId, std::ostream *errors)
      : SleighArchitecture("image", languageId, errors), pendingImage(std::move(image)) {}
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
    architecture.reset(new HostArchitecture(std::move(image), languageId, &messages));
    DocumentStorage store;
    architecture->init(store);
    architecture->readLoaderSymbols("::");
  } catch (...) {
    rethrow();
  }
}

Program::~Program() = default;

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

}  // namespace host
