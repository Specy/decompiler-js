// The system calls of Linux on x86-64, by number: what the decompiler calls a
// `syscall` instruction once it knows the number in RAX (Program).
#ifndef DECOMPILER_HOST_SYSTEM_CALLS_HH
#define DECOMPILER_HOST_SYSTEM_CALLS_HH

#include <cstdint>

namespace host {

/// One system call, as the kernel numbers and names it.
struct SystemCall {
  uint32_t number = 0;
  /// The kernel's name for it (`write`); the decompiler prints `sys_write`.
  const char *name = nullptr;
  /// Its parameters as a C parameter list, in the kernel's order and with
  /// their types spelt in the C types of their sizes, a struct's address as a
  /// `void *` (`unsigned int fd, const char *buf, unsigned long count`),
  /// `void` for none; nullptr where none is declared, the decompiler then
  /// working them out from the registers the program sets before the
  /// instruction.
  const char *parameters = nullptr;
  /// Whether it never returns to the instruction after it (exit, exit_group,
  /// rt_sigreturn).
  bool noreturn = false;
};

/// The names of the system call tables Program takes: "none", Ghidra's own
/// behaviour, each `syscall` an operation without inputs; "linux", Linux's
/// system calls on x86-64.
extern const char *const SYSTEM_CALL_TABLES[2];

/// Linux's system call of that number on x86-64, or nullptr for a number it
/// does not have.
const SystemCall *linuxSystemCall(uint64_t number);

}  // namespace host

#endif
