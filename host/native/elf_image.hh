// Reads a little-endian ELF64 executable into a ProgramImage, for the native
// reference driver. The package itself reads no executable format: its caller
// builds the ProgramImage.
#ifndef DECOMPILER_HOST_NATIVE_ELF_IMAGE_HH
#define DECOMPILER_HOST_NATIVE_ELF_IMAGE_HH

#include <cstdint>
#include <vector>

#include "program_image.hh"

namespace host {

/// Regions: each PT_LOAD segment at its link-time address, its file bytes
/// followed by zeros up to its memory size, read-only unless PF_W, executable
/// when PF_X. Functions: every defined STT_FUNC symbol of .symtab and
/// .dynsym, GLOBAL before WEAK before LOCAL at a shared address, then each
/// PLT stub named after the symbol of the relocation filling its GOT slot.
/// Throws std::runtime_error on a malformed or unsupported file.
ProgramImage readElfImage(const std::vector<uint8_t> &file);

}  // namespace host

#endif
