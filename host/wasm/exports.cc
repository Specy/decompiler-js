// The C functions the wasm module exports. Addresses cross as doubles, exact
// for every address below 2^53; strings as UTF-8. A function that fails
// returns null (or 0) and leaves its message for decompiler_error().
#include <emscripten/emscripten.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "program.hh"

namespace {

std::string lastError;

char *copyString(const std::string &text) {
  char *copy = static_cast<char *>(std::malloc(text.size() + 1));
  if (copy != nullptr) std::memcpy(copy, text.c_str(), text.size() + 1);
  return copy;
}

void fail(const std::exception &error) { lastError = error.what(); }

}  // namespace

extern "C" {

/// The Ghidra release the module was built from.
EMSCRIPTEN_KEEPALIVE const char *decompiler_version() {
  static const std::string version = host::ghidraVersion();
  return version.c_str();
}

/// The message of the last failure.
EMSCRIPTEN_KEEPALIVE const char *decompiler_error() { return lastError.c_str(); }

/// Adds a directory of the in-memory filesystem holding a .ldefs file and the
/// specification files it names, starting the library on the first call.
/// The files must be written before the first program_open.
EMSCRIPTEN_KEEPALIVE int decompiler_add_specs(const char *directory) {
  try {
    host::startLibrary({directory});
    return 1;
  } catch (const std::exception &error) {
    fail(error);
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE host::ProgramImage *image_new() { return new host::ProgramImage(); }

EMSCRIPTEN_KEEPALIVE void image_delete(host::ProgramImage *image) { delete image; }

/// flags: 1 read-only, 2 executable.
EMSCRIPTEN_KEEPALIVE void image_add_region(host::ProgramImage *image, double address,
                                           const uint8_t *bytes, uint32_t length, int flags) {
  host::Region region;
  region.address = (uint64_t)address;
  region.bytes.assign(bytes, bytes + length);
  region.readOnly = (flags & 1) != 0;
  region.executable = (flags & 2) != 0;
  image->regions.push_back(std::move(region));
}

EMSCRIPTEN_KEEPALIVE void image_add_function(host::ProgramImage *image, double address,
                                             const char *name) {
  image->functions.push_back({(uint64_t)address, name});
}

/// Builds the architecture for the image, which it takes over: the image is
/// freed whether or not this succeeds.
EMSCRIPTEN_KEEPALIVE host::Program *program_open(host::ProgramImage *image, const char *languageId) {
  host::ProgramImage taken = std::move(*image);
  delete image;
  try {
    return new host::Program(std::move(taken), languageId);
  } catch (const std::exception &error) {
    fail(error);
    return nullptr;
  }
}

/// The C of the function at the address, to be freed with free(), or null.
EMSCRIPTEN_KEEPALIVE char *program_decompile(host::Program *program, double address) {
  try {
    return copyString(program->decompile((uint64_t)address));
  } catch (const std::exception &error) {
    fail(error);
    return nullptr;
  }
}

EMSCRIPTEN_KEEPALIVE void program_close(host::Program *program) { delete program; }

}  // extern "C"
