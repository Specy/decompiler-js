// The native reference driver: decompiles one function of an ELF executable
// through the host layer, as the wasm build does, so the two can be compared.
//
//   decompile [--prototypes <file.h>] [--save <file.xml>] [--repeat <n>] [--json]
//             <spec directory> <elf> <function name or 0xaddress>
//
// Prints the C to stdout, and the times and warnings to stderr. --json prints
// the result as the wasm's program_decompile returns it instead: the lines
// with their tokens' kinds, addresses and targets. --prototypes passes a file
// of C declarations with the program image. --save also writes the
// program as a save file of Ghidra's XmlArchitecture, which Ghidra's own
// console (build/native/decomp_console) restores to cross-check this driver.
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "elf_image.hh"
#include "program.hh"

namespace {

const char *LANGUAGE = "x86:LE:64:default:gcc";

double millisecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("could not read " + path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char **argv) {
  std::string savePath, prototypesPath;
  int repeat = 1;
  bool json = false;
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; ++i) {
    std::string option = argv[i];
    if (option == "--json") json = true;
    else if (i + 1 == argc) break;
    else if (option == "--save") savePath = argv[++i];
    else if (option == "--prototypes") prototypesPath = argv[++i];
    else if (option == "--repeat") repeat = std::stoi(argv[++i]);
    else break;
  }
  if (argc - i != 3) {
    std::cerr << "usage: decompile [--prototypes <file.h>] [--save <file.xml>] [--repeat <n>] [--json] "
                 "<spec directory> <elf> <function name or 0xaddress>\n";
    return 2;
  }
  std::string specDirectory = argv[i], elfPath = argv[i + 1], target = argv[i + 2];

  try {
    std::string elf = readFile(elfPath);
    host::ProgramImage image = host::readElfImage(std::vector<uint8_t>(elf.begin(), elf.end()));
    if (!prototypesPath.empty()) image.prototypes = readFile(prototypesPath);

    uint64_t address = 0;
    bool found = false;
    if (target.rfind("0x", 0) == 0) {
      address = std::stoull(target, nullptr, 16);
      found = true;
    }
    for (const host::FunctionName &function : image.functions) {
      if (!found && function.name == target) {
        address = function.address;
        found = true;
      }
    }
    if (!found) throw std::runtime_error("no function named " + target);

    auto start = std::chrono::steady_clock::now();
    host::startLibrary({specDirectory});
    host::Program program(std::move(image), LANGUAGE);
    double build = millisecondsSince(start);
    if (!savePath.empty()) {
      std::ofstream out(savePath);
      program.writeSaveFile(out);
      if (!out) throw std::runtime_error("could not write " + savePath);
    }

    host::DecompiledFunction result;
    for (int run = 0; run < repeat; ++run) {
      auto decompileStart = std::chrono::steady_clock::now();
      result = program.decompile(address);
      std::cerr << (run == 0 ? "first" : "again") << " decompile: " << std::fixed
                << std::setprecision(1) << millisecondsSince(decompileStart) << " ms\n";
    }
    std::cerr << "architecture: " << std::fixed << std::setprecision(1) << build << " ms ("
              << host::ghidraVersion() << ")\n";
    for (const std::string &warning : program.warnings()) std::cerr << "warning: " << warning << "\n";
    std::cout << (json ? result.json() : result.text()) << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
