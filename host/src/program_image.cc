#include "program_image.hh"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace host {

namespace {

/// Keeps the first entry kept by keep at each address, then sorts by address.
template <typename T, typename Keep>
void firstPerAddress(std::vector<T> &entries, Keep keep) {
  std::vector<T> unique;
  std::set<uint64_t> seen;
  for (T &entry : entries) {
    if (!keep(entry) || !seen.insert(entry.address).second) continue;
    unique.push_back(std::move(entry));
  }
  std::stable_sort(unique.begin(), unique.end(),
                   [](const T &a, const T &b) { return a.address < b.address; });
  entries = std::move(unique);
}

class BufferReader {
  const uint8_t *at;
  const uint8_t *end;

 public:
  BufferReader(const uint8_t *bytes, size_t size) : at(bytes), end(bytes + size) {}

  const uint8_t *take(size_t size) {
    if ((size_t)(end - at) < size) throw std::invalid_argument("The program image is truncated");
    const uint8_t *taken = at;
    at += size;
    return taken;
  }
  uint64_t le(int size) {
    const uint8_t *bytes = take(size);
    uint64_t value = 0;
    for (int i = size - 1; i >= 0; --i) value = (value << 8) | bytes[i];
    return value;
  }
  uint32_t u32() { return (uint32_t)le(4); }
  uint64_t u64() { return le(8); }
  std::string text() {
    uint32_t size = u32();
    return std::string(reinterpret_cast<const char *>(take(size)), size);
  }
  bool done() const { return at == end; }
};

}  // namespace

void normalize(ProgramImage &image) {
  std::stable_sort(image.regions.begin(), image.regions.end(),
                   [](const Region &a, const Region &b) { return a.address < b.address; });
  for (size_t i = 1; i < image.regions.size(); ++i) {
    const Region &previous = image.regions[i - 1];
    if (previous.address + previous.bytes.size() > image.regions[i].address)
      throw std::invalid_argument("The program image's regions overlap");
  }
  firstPerAddress(image.functions, [](const FunctionName &f) { return !f.name.empty(); });
  firstPerAddress(image.data, [](const DataName &d) { return !d.name.empty(); });
  firstPerAddress(image.strings, [](const StringRange &s) { return s.length > 0; });
}

ProgramImage readProgramImage(const uint8_t *bytes, size_t size) {
  BufferReader in(bytes, size);
  if (std::memcmp(in.take(4), "DIMG", 4) != 0) throw std::invalid_argument("Not a program image");
  if (in.u32() != 1) throw std::invalid_argument("Unsupported program image version");
  ProgramImage image;
  for (uint32_t n = in.u32(); n > 0; --n) {
    Region region;
    region.address = in.u64();
    uint32_t flags = in.u32();
    region.readOnly = (flags & 1) != 0;
    region.executable = (flags & 2) != 0;
    uint32_t length = in.u32();
    const uint8_t *data = in.take(length);
    region.bytes.assign(data, data + length);
    image.regions.push_back(std::move(region));
  }
  for (uint32_t n = in.u32(); n > 0; --n) {
    FunctionName function;
    function.address = in.u64();
    function.name = in.text();
    image.functions.push_back(std::move(function));
  }
  for (uint32_t n = in.u32(); n > 0; --n) {
    DataName data;
    data.address = in.u64();
    data.size = in.u64();
    data.name = in.text();
    image.data.push_back(std::move(data));
  }
  for (uint32_t n = in.u32(); n > 0; --n) {
    StringRange string;
    string.address = in.u64();
    string.length = in.u64();
    image.strings.push_back(string);
  }
  image.prototypes = in.text();
  if (!in.done()) throw std::invalid_argument("The program image has bytes past its end");
  return image;
}

}  // namespace host
