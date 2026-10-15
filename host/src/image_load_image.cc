#include "image_load_image.hh"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace host {

using namespace ghidra;

ImageLoadImage::ImageLoadImage(ProgramImage programImage, const std::string &arch)
    : LoadImage("image"), image(std::move(programImage)), archType(arch) {
  normalize(image);
}

void ImageLoadImage::loadFill(uint1 *ptr, int4 size, const Address &addr) {
  uint64_t first = addr.getOffset();
  uint64_t end = first + (uint64_t)size;  // exclusive
  // The last region starting at or before the first byte, then every region
  // the read reaches; bytes between them read as zeros.
  size_t index = std::upper_bound(image.regions.begin(), image.regions.end(), first,
                                  [](uint64_t a, const Region &r) { return a < r.address; }) -
                 image.regions.begin();
  bool firstMapped = false;
  if (addr.getSpace() == space && index > 0) {
    const Region &r = image.regions[index - 1];
    firstMapped = first - r.address < r.bytes.size();
  }
  if (!firstMapped) {
    std::ostringstream message;
    message << "Bytes at " << addr.getSpace()->getName() << ":0x" << std::hex << first
            << " are not mapped";
    throw DataUnavailError(message.str());
  }
  std::fill_n(ptr, size, 0);
  for (index -= 1; index < image.regions.size() && image.regions[index].address < end; ++index) {
    const Region &r = image.regions[index];
    uint64_t from = std::max(first, r.address);
    uint64_t to = std::min(end, r.address + r.bytes.size());
    if (from < to) std::copy(r.bytes.begin() + (from - r.address), r.bytes.begin() + (to - r.address),
                             ptr + (from - first));
  }
}

bool ImageLoadImage::getNextSymbol(LoadImageFunc &record) const {
  if (space == nullptr || nextSymbol >= image.functions.size()) return false;
  const FunctionName &function = image.functions[nextSymbol++];
  record.address = Address(space, function.address);
  record.name = function.name;
  return true;
}

void ImageLoadImage::getReadonly(RangeList &list) const {
  if (space == nullptr) return;
  for (const Region &region : image.regions) {
    if (!region.readOnly || region.bytes.empty()) continue;
    list.insertRange(space, region.address, region.address + region.bytes.size() - 1);
  }
}

void ImageLoadImage::adjustVma(long) {
  throw LowlevelError("A program image is placed at its own addresses and cannot be moved");
}

namespace {

void writeChunk(std::ostream &out, const std::string &space, uint64_t address, const uint8_t *bytes,
                size_t size, bool readOnly) {
  out << "<bytechunk space=\"" << space << "\" offset=\"0x" << std::hex << address << "\""
      << (readOnly ? " readonly=\"true\"" : "") << ">\n";
  for (size_t i = 0; i < size; ++i) {
    out << std::setw(2) << std::setfill('0') << (int)bytes[i];
    if (i % 32 == 31 || i + 1 == size) out << '\n';
  }
  out << "</bytechunk>\n" << std::dec;
}

}  // namespace

void ImageLoadImage::writeXml(std::ostream &out) const {
  std::string spaceName = space != nullptr ? space->getName() : "ram";
  out << "<binaryimage arch=\"" << archType << "\">\n";
  const std::vector<uint8_t> zeros(16, 0);
  for (size_t i = 0; i < image.regions.size(); ++i) {
    const Region &region = image.regions[i];
    writeChunk(out, spaceName, region.address, region.bytes.data(), region.bytes.size(), region.readOnly);
    uint64_t end = region.address + region.bytes.size();
    uint64_t padding = 16;
    if (i + 1 < image.regions.size()) padding = std::min<uint64_t>(16, image.regions[i + 1].address - end);
    if (padding > 0) writeChunk(out, spaceName, end, zeros.data(), padding, false);
  }
  out << "</binaryimage>\n";
}

}  // namespace host
