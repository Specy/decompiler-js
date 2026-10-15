// The C functions the wasm module exports. Addresses cross as doubles, exact
// for every address below 2^53; strings as UTF-8. A function that fails
// returns null (or 0) and leaves its message for decompiler_error().
#include <emscripten/emscripten.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "json.hh"
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

/// Builds the architecture for a program image in the binary form
/// readProgramImage reads (host/src/program_image.hh).
EMSCRIPTEN_KEEPALIVE host::Program *program_open(const uint8_t *image, uint32_t size,
                                                 const char *languageId) {
  try {
    return new host::Program(host::readProgramImage(image, size), languageId);
  } catch (const std::exception &error) {
    fail(error);
    return nullptr;
  }
}

/// What went wrong building the program without stopping it (see
/// Program::warnings), as a JSON array of strings, to be freed with free().
EMSCRIPTEN_KEEPALIVE char *program_warnings(host::Program *program) {
  return copyString(host::jsonStrings(program->warnings()));
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
