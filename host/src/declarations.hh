// C declarations a caller passes with a program (a library's prototypes, the
// typedefs they need, the types of its global variables), read with the
// decompiler's own C parser (CParse, grammar.y) and applied by name.
#ifndef DECOMPILER_HOST_DECLARATIONS_HH
#define DECOMPILER_HOST_DECLARATIONS_HH

#include <string>
#include <vector>

#include "architecture.hh"
#include "core_types.hh"

namespace host {

/// Applies each declaration of source to the architecture's symbols:
///
/// - a function declaration (`int puts(const char *s);`) becomes the
///   prototype of every function of that name, locked as Ghidra locks a
///   prototype from a data type archive; one naming no function of the
///   program is skipped;
/// - a typedef, struct, union or enum is added to the data-types;
/// - a variable declaration (`extern FILE *stdin;`) types every data symbol
///   of that name, the type locked.
///
/// Comments and preprocessor lines are ignored. Before parsing, each
/// declaration is brought within what CParse reads: `_Noreturn`,
/// `[[noreturn]]` or `__attribute__((noreturn))` marks the function as not
/// returning (FuncProto::setNoReturn) and is removed, as is any other
/// `__attribute__((...))` or `[[...]]`; `__restrict`, `__const` and
/// `__inline` become their standard spellings; the C names of integer and
/// floating-point types spelt in several words (`unsigned long`, `long long`,
/// `long double`) or with another type's word (`short int`) become the core
/// type of their size, as the data organization gives it (`ulong`,
/// `longlong`, `longdouble`), and `_Bool` becomes `bool`; and a struct or
/// union referred to by its tag (`typedef struct _IO_FILE FILE;`) is referred
/// to by its name, created incomplete if it is not defined yet; and a
/// function named after one of the compiler specification's calling
/// conventions (`syscall`), which CParse reads as the convention, is read
/// under a stand-in name and applied under its own. CParse cannot
/// define a struct or union already referred to: define it first. Nor does
/// it read an empty parameter list (write `(void)`) or an array without a
/// size.
///
/// Returns one message per declaration that could not be read or applied,
/// with its line in source, e.g. "line 3: Undefined identifier: size_t (in
/// `size_t strlen(const char *s);`)". Never throws for a bad declaration.
std::vector<std::string> applyDeclarations(ghidra::Architecture &architecture,
                                           const DataOrganization &organization,
                                           const std::string &source);

}  // namespace host

#endif
