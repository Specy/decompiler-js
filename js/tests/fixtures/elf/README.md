# Executable fixtures

Copied unchanged from the fixtures of [`@specy/x86`](https://github.com/Specy/x86-js)
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

## SHA256

```
7f77fd2c5eff322b728f0ba60d1ef3fcb6bfe5335a4a4b32ad46aebfad276ad9  crackme
5f6b640abab92e6b012c32b49226a78d18700353e8effd79339d77ac10f7edb3  crackme-stripped
d263d8683f3f6d0b6844a49529211b1a873acffd5bf001504abc2148219123ce  hello-atexit-static
```
