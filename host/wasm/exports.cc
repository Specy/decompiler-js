// The C functions the wasm module exports. Addresses cross as doubles, exact
// for every address below 2^53; strings as UTF-8. A function that fails
// returns null (or 0) and leaves its message for decompiler_error().
#include <emscripten/emscripten.h>
#include <malloc.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <sstream>
#include <stdexcept>
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

/// {"address", "error"}, to be freed with free().
char *errorJson(uint64_t entry, const std::exception &error) {
  std::ostringstream json;
  json << "{\"address\":" << entry << ",\"error\":";
  host::writeJsonString(json, error.what());
  json << "}";
  return copyString(json.str());
}

/// The function decompiled, as JSON to be freed with free(), or the error.
char *decompileJson(host::Program *program, uint64_t entry, uint32_t maxInstructions, uint32_t timeLimitMs,
                    const host::Annotations &annotations) {
  try {
    return copyString(
        program
            ->decompile(entry, maxInstructions == 0 ? host::Program::DEFAULT_MAX_INSTRUCTIONS : maxInstructions,
                        timeLimitMs == 0 ? host::Program::DEFAULT_TIME_LIMIT_MS : timeLimitMs, annotations)
            .json());
  } catch (const std::exception &error) {
    return errorJson(entry, error);
  }
}

/// Reads one decompilation's annotations, little endian:
///
///   "DANN", u32 version (1)
///   u32 count, then per local: u32 size and the id, u32 size and the name
///   u32 count, then per comment: u64 address, u32 size and the text
///   u32 size and the function comment
///   u32 flags: 1 lists the comments that could not be placed
host::Annotations readAnnotations(const uint8_t *bytes, uint32_t size) {
  host::Annotations annotations;
  uint32_t at = 0;
  auto need = [&](uint32_t count) {
    if (size - at < count) throw std::invalid_argument("The annotations are truncated");
  };
  auto u32 = [&]() {
    need(4);
    uint32_t value;
    std::memcpy(&value, bytes + at, 4);
    at += 4;
    return value;
  };
  auto u64 = [&]() {
    need(8);
    uint64_t value;
    std::memcpy(&value, bytes + at, 8);
    at += 8;
    return value;
  };
  auto text = [&]() {
    uint32_t length = u32();
    need(length);
    std::string value(reinterpret_cast<const char *>(bytes + at), length);
    at += length;
    return value;
  };
  need(4);
  if (std::memcmp(bytes, "DANN", 4) != 0) throw std::invalid_argument("Not annotations");
  at = 4;
  if (u32() != 1) throw std::invalid_argument("Unknown annotations version");
  for (uint32_t count = u32(); count > 0; --count) {
    std::string id = text();
    annotations.locals.push_back({id, text()});
  }
  for (uint32_t count = u32(); count > 0; --count) {
    uint64_t address = u64();
    annotations.comments.push_back({address, text()});
  }
  annotations.functionComment = text();
  annotations.unplacedComments = (u32() & 1) != 0;
  if (at != size) throw std::invalid_argument("The annotations have bytes left over");
  return annotations;
}

}  // namespace

extern "C" {

/// The Ghidra release the module was built from.
EMSCRIPTEN_KEEPALIVE const char *decompiler_version() {
  static const std::string version = host::ghidraVersion();
  return version.c_str();
}

/// The message of the last failure.
EMSCRIPTEN_KEEPALIVE const char *decompiler_error() { return lastError.c_str(); }

/// The bytes malloc has handed out and not had back.
EMSCRIPTEN_KEEPALIVE double decompiler_heap_used() { return (double)mallinfo().uordblks; }

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
/// readProgramImage reads (host/src/program_image.hh), its names qualified
/// with their namespaces by the strategy namespaces names: "minimal", "all"
/// or "none" (host::NAMESPACE_STRATEGIES).
EMSCRIPTEN_KEEPALIVE host::Program *program_open(const uint8_t *image, uint32_t size, const char *languageId,
                                                 const char *namespaces) {
  try {
    return new host::Program(host::readProgramImage(image, size), languageId, namespaces);
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

/// The function starting at the address, decompiled following at most
/// maxInstructions instructions (0 for Ghidra's default, 100,000) for at most
/// timeLimitMs milliseconds (0 for Ghidra's default, 30 seconds), as JSON to
/// be freed with free(): {"address", "name", "lines", "variables"}, each line
/// an array of tokens [text, kind, address, target] with kind an index into
/// keyword, comment, type, function, variable, constant, syntax, scope and
/// address and target -1 when absent, a variable's token adding its index in
/// variables and a comment's its index (-1) and its comment type
/// (host/src/markup.hh); or {"address", "error"}.
EMSCRIPTEN_KEEPALIVE char *program_decompile(host::Program *program, double address, uint32_t maxInstructions,
                                             uint32_t timeLimitMs) {
  return decompileJson(program, (uint64_t)address, maxInstructions, timeLimitMs, host::Annotations());
}

/// program_decompile with annotations (readAnnotations): names for local
/// variables and parameters by id, comments, a function comment, and whether
/// comments that could not be placed are listed. Malformed annotations are
/// an error.
EMSCRIPTEN_KEEPALIVE char *program_decompile_annotated(host::Program *program, double address,
                                                       uint32_t maxInstructions, uint32_t timeLimitMs,
                                                       const uint8_t *annotations, uint32_t size) {
  uint64_t entry = (uint64_t)address;
  host::Annotations read;
  try {
    read = readAnnotations(annotations, size);
  } catch (const std::exception &error) {
    return errorJson(entry, error);
  }
  return decompileJson(program, entry, maxInstructions, timeLimitMs, read);
}

EMSCRIPTEN_KEEPALIVE void program_close(host::Program *program) { delete program; }

}  // extern "C"
