#include "elf_image.hh"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace host {

namespace {

constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PF_X = 1;
constexpr uint32_t PF_W = 2;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_RELA = 4;
constexpr uint32_t SHT_DYNSYM = 11;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STB_WEAK = 2;

class Reader {
  const std::vector<uint8_t> &file;

 public:
  explicit Reader(const std::vector<uint8_t> &bytes) : file(bytes) {}

  void need(uint64_t offset, uint64_t size) const {
    if (offset > file.size() || size > file.size() - offset)
      throw std::runtime_error("ELF file is truncated");
  }
  uint64_t le(uint64_t offset, int size) const {
    need(offset, size);
    uint64_t value = 0;
    for (int i = size - 1; i >= 0; --i) value = (value << 8) | file[offset + i];
    return value;
  }
  uint8_t u8(uint64_t offset) const { return (uint8_t)le(offset, 1); }
  uint16_t u16(uint64_t offset) const { return (uint16_t)le(offset, 2); }
  uint32_t u32(uint64_t offset) const { return (uint32_t)le(offset, 4); }
  uint64_t u64(uint64_t offset) const { return le(offset, 8); }
  std::string cstring(uint64_t offset) const {
    need(offset, 1);
    auto end = std::find(file.begin() + offset, file.end(), 0);
    if (end == file.end()) throw std::runtime_error("ELF string is unterminated");
    return std::string(file.begin() + offset, end);
  }
};

struct Section {
  std::string name;
  uint32_t type, link;
  uint64_t address, offset, size, entsize;
};

struct Symbol {
  std::string name;
  uint8_t type, binding;
  uint16_t shndx;
  uint64_t value, size;
};

std::vector<Symbol> readSymbols(const Reader &in, const std::vector<Section> &sections,
                                const Section &table) {
  if (table.link >= sections.size()) throw std::runtime_error("ELF symbol table has no strings");
  const Section &strings = sections[table.link];
  std::vector<Symbol> symbols;
  for (uint64_t at = table.offset; at + 24 <= table.offset + table.size; at += 24) {
    Symbol symbol;
    uint32_t name = in.u32(at);
    uint8_t info = in.u8(at + 4);
    symbol.type = info & 0xf;
    symbol.binding = info >> 4;
    symbol.shndx = in.u16(at + 6);
    symbol.value = in.u64(at + 8);
    symbol.size = in.u64(at + 16);
    symbol.name = name < strings.size ? in.cstring(strings.offset + name) : std::string();
    symbols.push_back(symbol);
  }
  return symbols;
}

int bindingRank(uint8_t binding) {
  if (binding == STB_GLOBAL) return 0;
  if (binding == STB_WEAK) return 1;
  if (binding == STB_LOCAL) return 2;
  return 3;
}

/// A symbol's name without its version: "stdin@GLIBC_2.2.5" is "stdin".
std::string unversioned(const std::string &name) { return name.substr(0, name.find('@')); }

bool printable(uint8_t byte) { return (byte >= 0x20 && byte < 0x7f) || byte == '\t' || byte == '\n' || byte == '\r'; }

/// The editor's Listing names a string "s_" and its first 24 characters as a word.
std::string stringLabel(const std::string &text) {
  std::string word;
  bool gap = false;
  for (char c : text.substr(0, 24)) {
    if (std::isalnum((unsigned char)c)) {
      if (gap && !word.empty()) word += '_';
      word += c;
      gap = false;
    } else {
      gap = true;
    }
  }
  return word.empty() ? std::string() : "s_" + word;
}

}  // namespace

ProgramImage readElfImage(const std::vector<uint8_t> &file) {
  Reader in(file);
  in.need(0, 64);
  if (in.u32(0) != 0x464c457f) throw std::runtime_error("not an ELF file");
  if (in.u8(4) != 2 || in.u8(5) != 1)
    throw std::runtime_error("only little-endian ELF64 files are supported");

  uint64_t phoff = in.u64(32), shoff = in.u64(40);
  uint16_t phentsize = in.u16(54), phnum = in.u16(56);
  uint16_t shentsize = in.u16(58), shnum = in.u16(60), shstrndx = in.u16(62);

  ProgramImage image;
  for (uint16_t i = 0; i < phnum; ++i) {
    uint64_t at = phoff + (uint64_t)i * phentsize;
    if (in.u32(at) != PT_LOAD) continue;
    uint32_t flags = in.u32(at + 4);
    uint64_t offset = in.u64(at + 8), address = in.u64(at + 16);
    uint64_t filesz = in.u64(at + 32), memsz = in.u64(at + 40);
    if (memsz == 0) continue;
    in.need(offset, filesz);
    Region region;
    region.address = address;
    region.bytes.assign(file.begin() + offset, file.begin() + offset + filesz);
    region.bytes.resize(std::max(filesz, memsz), 0);
    region.readOnly = (flags & PF_W) == 0;
    region.executable = (flags & PF_X) != 0;
    image.regions.push_back(std::move(region));
  }

  std::vector<Section> sections;
  for (uint16_t i = 0; i < shnum; ++i) {
    uint64_t at = shoff + (uint64_t)i * shentsize;
    Section section;
    section.type = in.u32(at + 4);
    section.address = in.u64(at + 16);
    section.offset = in.u64(at + 24);
    section.size = in.u64(at + 32);
    section.link = in.u32(at + 40);
    section.entsize = in.u64(at + 56);
    section.name = std::to_string(in.u32(at));  // resolved below
    sections.push_back(section);
  }
  if (shstrndx < sections.size()) {
    const Section &names = sections[shstrndx];
    for (Section &section : sections) {
      uint32_t name = (uint32_t)std::stoul(section.name);
      section.name = name < names.size ? in.cstring(names.offset + name) : std::string();
    }
  }

  // Defined functions from the symbol tables, best binding first at each address.
  struct Candidate {
    int rank;
    size_t order;
    FunctionName function;
  };
  std::vector<Candidate> candidates;
  struct DataCandidate {
    int rank;
    size_t order;
    DataName data;
  };
  std::vector<DataCandidate> objects;
  std::map<uint64_t, std::string> slotNames;  // GOT slot -> imported symbol
  for (const Section &section : sections) {
    if (section.type != SHT_SYMTAB && section.type != SHT_DYNSYM) continue;
    for (const Symbol &symbol : readSymbols(in, sections, section)) {
      if (symbol.shndx == 0 || symbol.value == 0 || symbol.name.empty()) continue;
      if (symbol.type == STT_FUNC)
        candidates.push_back({bindingRank(symbol.binding), candidates.size(),
                              {symbol.value, symbol.name}});
      else if (symbol.type == STT_OBJECT)
        objects.push_back({bindingRank(symbol.binding), objects.size(),
                           {symbol.value, unversioned(symbol.name), symbol.size}});
    }
  }
  for (const Section &section : sections) {
    if (section.type != SHT_RELA || section.link >= sections.size()) continue;
    const Section &table = sections[section.link];
    if (table.type != SHT_SYMTAB && table.type != SHT_DYNSYM) continue;
    std::vector<Symbol> symbols = readSymbols(in, sections, table);
    for (uint64_t at = section.offset; at + 24 <= section.offset + section.size; at += 24) {
      uint64_t symbolIndex = in.u64(at + 8) >> 32;
      if (symbolIndex == 0 || symbolIndex >= symbols.size()) continue;
      const std::string &name = symbols[symbolIndex].name;
      if (!name.empty()) slotNames.emplace(in.u64(at), name);
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.order < b.order;
  });
  for (Candidate &candidate : candidates) image.functions.push_back(std::move(candidate.function));

  // PLT stubs: [endbr64] [bnd] jmp *disp32(%rip), named by the GOT slot they jump through.
  for (const Section &section : sections) {
    if (section.name != ".plt" && section.name != ".plt.sec" && section.name != ".plt.got")
      continue;
    uint64_t step = section.entsize != 0 ? section.entsize : 16;
    for (uint64_t stub = 0; stub + step <= section.size; stub += step) {
      uint64_t at = section.offset + stub;
      uint64_t i = 0;
      if (in.u32(at) == 0xfa1e0ff3) i += 4;  // endbr64
      if (in.u8(at + i) == 0xf2) i += 1;      // bnd
      if (in.u8(at + i) != 0xff || in.u8(at + i + 1) != 0x25) continue;
      int32_t displacement = (int32_t)in.u32(at + i + 2);
      uint64_t next = section.address + stub + i + 6;
      auto slot = slotNames.find(next + (int64_t)displacement);
      if (slot != slotNames.end()) image.functions.push_back({section.address + stub, slot->second});
    }
  }

  // Data: the objects, best binding first at each address, then the strings
  // of .rodata (NUL-terminated runs of printable bytes, their NUL included),
  // each named as the editor's Listing names one when it has a word in it.
  std::stable_sort(objects.begin(), objects.end(), [](const DataCandidate &a, const DataCandidate &b) {
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.order < b.order;
  });
  std::set<std::string> taken;
  for (DataCandidate &object : objects) {
    taken.insert(object.data.name);
    image.data.push_back(std::move(object.data));
  }
  for (const Section &section : sections) {
    if (section.name != ".rodata") continue;
    in.need(section.offset, section.size);
    uint64_t start = 0;
    bool inRun = false;
    for (uint64_t i = 0; i < section.size; ++i) {
      uint8_t byte = file[section.offset + i];
      if (byte == 0 && inRun) {
        std::string text(file.begin() + section.offset + start, file.begin() + section.offset + i);
        image.strings.push_back({section.address + start, i + 1 - start});
        std::string name = stringLabel(text);
        if (!name.empty()) {
          std::string unique = name;
          for (int n = 2; taken.count(unique) != 0; ++n) unique = name + "_" + std::to_string(n);
          taken.insert(unique);
          image.data.push_back({section.address + start, unique, i + 1 - start});
        }
      }
      if (byte == 0 || !printable(byte)) {
        inRun = false;
      } else if (!inRun) {
        inRun = true;
        start = i;
      }
    }
  }

  normalize(image);
  return image;
}

}  // namespace host
