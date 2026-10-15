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
/// Data: every defined STT_OBJECT symbol, its version dropped ("stdin"), with
/// its size, in the same order, then the strings of .rodata, each
/// NUL-terminated run of printable bytes, named "s_" and the first 24
/// characters as a word (numbered when taken) when there is a word, as the
/// editor's Listing names them. Strings: those runs, their NUL included.
/// Throws std::runtime_error on a malformed or unsupported file.
ProgramImage readElfImage(const std::vector<uint8_t> &file);

}  // namespace host

#endif
