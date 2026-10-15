// The native reference driver: decompiles one function of an ELF executable
// through the host layer, as the wasm build does, so the two can be compared.
//
//   decompile [--save <file.xml>] [--repeat <n>] <spec directory> <elf> <function name or 0xaddress>
//
// Prints the C to stdout and the times to stderr. --save also writes the
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
  std::string savePath;
  int repeat = 1;
  int i = 1;
  for (; i + 1 < argc && argv[i][0] == '-'; i += 2) {
    std::string option = argv[i];
    if (option == "--save") savePath = argv[i + 1];
    else if (option == "--repeat") repeat = std::stoi(argv[i + 1]);
    else break;
  }
  if (argc - i != 3) {
    std::cerr << "usage: decompile [--save <file.xml>] [--repeat <n>] <spec directory> <elf> "
                 "<function name or 0xaddress>\n";
    return 2;
  }
  std::string specDirectory = argv[i], elfPath = argv[i + 1], target = argv[i + 2];

  try {
    std::string elf = readFile(elfPath);
    host::ProgramImage image = host::readElfImage(std::vector<uint8_t>(elf.begin(), elf.end()));

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

    std::string text;
    for (int run = 0; run < repeat; ++run) {
      auto decompileStart = std::chrono::steady_clock::now();
      text = program.decompile(address);
      std::cerr << (run == 0 ? "first" : "again") << " decompile: " << std::fixed
                << std::setprecision(1) << millisecondsSince(decompileStart) << " ms\n";
    }
    std::cerr << "architecture: " << std::fixed << std::setprecision(1) << build << " ms ("
              << host::ghidraVersion() << ")\n";
    if (!program.warnings().empty()) std::cerr << program.warnings();
    std::cout << text;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
