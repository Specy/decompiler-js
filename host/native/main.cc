// The native reference driver: decompiles one function of an ELF executable
// through the host layer, as the wasm build does, so the two can be compared.
//
//   decompile [--xml <image.xml>] [--repeat <n>] <spec directory> <elf> <function name or 0xaddress>
//
// Prints the C to stdout and the times to stderr. --xml also writes the
// program image as a Ghidra XML load image, which Ghidra's own console
// (build/native/decomp_console) can load to cross-check this driver.
#include <algorithm>
#include <chrono>
#include <cstdio>
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

std::string escape(const std::string &text) {
  std::string out;
  for (char c : text) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

void writeChunk(std::ostream &out, uint64_t address, const std::vector<uint8_t> &bytes,
                bool readOnly) {
  out << "<bytechunk space=\"ram\" offset=\"0x" << address << "\""
      << (readOnly ? " readonly=\"true\"" : "") << ">\n";
  for (size_t i = 0; i < bytes.size(); ++i) {
    out << std::setw(2) << (int)bytes[i];
    if (i % 32 == 31 || i + 1 == bytes.size()) out << '\n';
  }
  out << "</bytechunk>\n";
}

// LoadImageXml refuses a read that runs past its bytes, where the host
// layer's loader (like Ghidra's file loaders) reads zeros, and SLEIGH reads
// 16 bytes for every instruction. So each region is followed by up to 16
// zero bytes of its own, which makes the two loaders answer alike.
void writeXmlImage(const host::ProgramImage &image, const std::string &path) {
  std::ofstream out(path);
  out << "<binaryimage arch=\"" << LANGUAGE << "\">\n" << std::hex << std::setfill('0');
  for (size_t i = 0; i < image.regions.size(); ++i) {
    const host::Region &region = image.regions[i];
    writeChunk(out, region.address, region.bytes, region.readOnly);
    uint64_t end = region.address + region.bytes.size();
    uint64_t padding = 16;
    if (i + 1 < image.regions.size()) padding = std::min<uint64_t>(16, image.regions[i + 1].address - end);
    if (padding > 0) writeChunk(out, end, std::vector<uint8_t>(padding, 0), false);
  }
  for (const host::FunctionName &function : image.functions)
    out << "<symbol space=\"ram\" offset=\"0x" << function.address << "\" name=\""
        << escape(function.name) << "\"/>\n";
  out << "</binaryimage>\n";
  if (!out) throw std::runtime_error("could not write " + path);
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main(int argc, char **argv) {
  std::string xmlPath;
  int repeat = 1;
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i += 2) {
    std::string option = argv[i];
    if (i + 1 >= argc) break;
    if (option == "--xml") xmlPath = argv[i + 1];
    else if (option == "--repeat") repeat = std::stoi(argv[i + 1]);
    else break;
  }
  if (argc - i != 3) {
    std::cerr << "usage: decompile [--xml <image.xml>] [--repeat <n>] <spec directory> <elf> "
                 "<function name or 0xaddress>\n";
    return 2;
  }
  std::string specDirectory = argv[i], elfPath = argv[i + 1], target = argv[i + 2];

  try {
    std::ifstream in(elfPath, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + elfPath);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    host::ProgramImage image = host::readElfImage(file);
    if (!xmlPath.empty()) writeXmlImage(image, xmlPath);

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
