# Executable fixtures

All but `factcheck` copied unchanged from the fixtures of [`@specy/x86`](https://github.com/Specy/x86-js)
(`blink-js/tests/fixtures/elf/`), where they were built on Ubuntu with GCC 15.2.0 and
glibc 2.43-2ubuntu2.4, without debug information (the decompiler sees ELF symbols only):

```sh
gcc -O0 -fPIE -pie crackme.c -o crackme
cp crackme crackme-stripped && strip crackme-stripped
gcc -O0 -static -no-pie hello-atexit.c -o hello-atexit-static
```

`-O0` keeps the password comparison as a branch. `hello-atexit-static` is the static, non-PIE
build: thousands of glibc functions at fixed addresses.

The static binary contains LGPL-2.1-or-later glibc object code. Corresponding source:
https://archive.ubuntu.com/ubuntu/pool/main/g/glibc/ (glibc 2.43-2ubuntu2.4).
The fixture sources are supplied here to allow rebuilding and relinking against a modified glibc.

## factcheck

`factcheck.cpp`, a C++ program with the names C++ gives a reverse engineer: a `std::string` built
with `+=`, a `std::vector<std::string>`, a `std::map`, lambdas, a class hierarchy with a vtable, a
function's static with its guard variable, overloads. Built here, not taken from `@specy/x86`, on
Ubuntu with G++ 15.2.0 (15.2.0-16ubuntu1) and glibc 2.43-2ubuntu2.4, linked dynamically against
libstdc++6 16-20260322-1ubuntu1, the same bytes on every build:

```sh
g++ -O0 factcheck.cpp -o factcheck
```

The binary holds code instantiated from libstdc++'s headers, GPL-3.0-or-later with the GCC Runtime
Library Exception, which lets a program compiled with GCC be distributed under any terms.

`factcheck.names.tsv` names its symbols as a caller with a demangler does: each line a mangled
symbol of its functions and data, then the namespaces of its demangled name, outermost first, then
the name, separated by tabs. The names are what LLVM's demangler prints, with the standard
library's templates shortened (`std::string`, `std::vector<std::string>`, `std::map<std::string,
int>`) and without return types, parameters or `[abi:…]` tags, split at the `::`s outside brackets
and quotes; a vtable, typeinfo or guard variable is named after what it belongs to
(`game::Boss::vtable`, `std::runtime_error::typeinfo`, `greeting()::text::guard`).

## SHA256

```
7f77fd2c5eff322b728f0ba60d1ef3fcb6bfe5335a4a4b32ad46aebfad276ad9  crackme
5f6b640abab92e6b012c32b49226a78d18700353e8effd79339d77ac10f7edb3  crackme-stripped
d263d8683f3f6d0b6844a49529211b1a873acffd5bf001504abc2148219123ce  hello-atexit-static
596bdbc59005ffb00247d8147bae39187e27da60c0d0014ab145128639504ded  factcheck
```
