// The native reference driver: decompiles one function of an ELF executable
// through the host layer, as the wasm build does, so the two can be compared.
//
//   decompile [--prototypes <file.h>] [--save <file.xml>] [--repeat <n>] [--json]
//             [--max-instructions <n>] [--time-limit <ms>] [--image]
//             [--names <file.tsv>] [--namespaces minimal|all|none]
//             <spec directory> <elf> <function name or 0xaddress>
//
// Prints the C to stdout, and the times and warnings to stderr. --image reads
// a program image in the binary form the wasm's program_open takes
// (host/src/program_image.hh) instead of an ELF, prototypes included, to
// decompile natively what a caller of the package passed it. --json prints
// the result as the wasm's program_decompile returns it instead: the lines
// with their tokens' kinds, addresses and targets. --prototypes passes a file
// of C declarations with the program image. --save also writes the
// program as a save file of Ghidra's XmlArchitecture, which Ghidra's own
// console (build/native/decomp_console) restores to cross-check this driver.
// --max-instructions and --time-limit bound each decompilation as the wasm's
// decompile options do, by default Ghidra's 100,000 and 30,000 ms.
// --names renames the image's functions, data and pointers from a table of
// lines, each a symbol's name, then the namespaces it is in, outermost
// first, then its name in them, separated by tabs
// (`_ZNSt6vectorIiSaIiEE4sizeEv`, `std`, `vector<int>`, `size`): the symbol's
// name becomes the entry's symbol. --namespaces is the strategy the wasm's
// openProgram takes, by default minimal.
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

/// A --names table: each symbol's namespaces, outermost first, then its name.
std::map<std::string, std::vector<std::string>> readNames(const std::string &path) {
  std::map<std::string, std::vector<std::string>> names;
  std::istringstream lines(readFile(path));
  for (std::string line; std::getline(lines, line);) {
    if (line.empty()) continue;
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1)
      fields.push_back(line.substr(start, tab - start));
    fields.push_back(line.substr(start));
    if (fields.size() < 2) throw std::runtime_error("a line of " + path + " names nothing: " + line);
    std::string symbol = fields.front();
    fields.erase(fields.begin());
    names[symbol] = std::move(fields);
  }
  return names;
}

/// Gives an entry of the image the name, path and symbol the table has for
/// its name.
template <typename Entry>
void rename(Entry &entry, const std::map<std::string, std::vector<std::string>> &names) {
  auto found = names.find(entry.name);
  if (found == names.end()) return;
  entry.symbol = entry.name;
  entry.path.assign(found->second.begin(), found->second.end() - 1);
  entry.name = found->second.back();
}

}  // namespace

int main(int argc, char **argv) {
  std::string savePath, prototypesPath, namesPath, namespaces = "minimal";
  int repeat = 1;
  bool json = false;
  bool imageFile = false;
  uint32_t maxInstructions = host::Program::DEFAULT_MAX_INSTRUCTIONS;
  uint32_t timeLimitMs = host::Program::DEFAULT_TIME_LIMIT_MS;
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; ++i) {
    std::string option = argv[i];
    if (option == "--json") json = true;
    else if (option == "--image") imageFile = true;
    else if (i + 1 == argc) break;
    else if (option == "--save") savePath = argv[++i];
    else if (option == "--prototypes") prototypesPath = argv[++i];
    else if (option == "--names") namesPath = argv[++i];
    else if (option == "--namespaces") namespaces = argv[++i];
    else if (option == "--repeat") repeat = std::stoi(argv[++i]);
    else if (option == "--max-instructions") maxInstructions = (uint32_t)std::stoul(argv[++i]);
    else if (option == "--time-limit") timeLimitMs = (uint32_t)std::stoul(argv[++i]);
    else break;
  }
  if (argc - i != 3) {
    std::cerr << "usage: decompile [--prototypes <file.h>] [--save <file.xml>] [--repeat <n>] [--json] "
                 "[--max-instructions <n>] [--time-limit <ms>] [--image] [--names <file.tsv>] "
                 "[--namespaces minimal|all|none] <spec directory> <elf> <function name or 0xaddress>\n";
    return 2;
  }
  std::string specDirectory = argv[i], elfPath = argv[i + 1], target = argv[i + 2];

  try {
    std::string elf = readFile(elfPath);
    host::ProgramImage image =
        imageFile ? host::readProgramImage(reinterpret_cast<const uint8_t *>(elf.data()), elf.size())
                  : host::readElfImage(std::vector<uint8_t>(elf.begin(), elf.end()));
    if (!prototypesPath.empty()) image.prototypes = readFile(prototypesPath);
    if (!namesPath.empty()) {
      auto names = readNames(namesPath);
      for (host::FunctionName &function : image.functions) rename(function, names);
      for (host::DataName &entry : image.data) rename(entry, names);
      for (host::PointerName &pointer : image.pointers) rename(pointer, names);
    }

    uint64_t address = 0;
    bool found = false;
    if (target.rfind("0x", 0) == 0) {
      address = std::stoull(target, nullptr, 16);
      found = true;
    }
    for (const host::FunctionName &function : image.functions) {
      if (!found && (function.name == target || function.symbol == target)) {
        address = function.address;
        found = true;
      }
    }
    if (!found) throw std::runtime_error("no function named " + target);

    auto start = std::chrono::steady_clock::now();
    host::startLibrary({specDirectory});
    host::Program program(std::move(image), LANGUAGE, namespaces);
    double build = millisecondsSince(start);
    if (!savePath.empty()) {
      std::ofstream out(savePath);
      program.writeSaveFile(out);
      if (!out) throw std::runtime_error("could not write " + savePath);
    }

    host::DecompiledFunction result;
    for (int run = 0; run < repeat; ++run) {
      auto decompileStart = std::chrono::steady_clock::now();
      result = program.decompile(address, maxInstructions, timeLimitMs);
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
