#include "program_image.hh"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace host {

void normalize(ProgramImage &image) {
  std::stable_sort(image.regions.begin(), image.regions.end(),
                   [](const Region &a, const Region &b) { return a.address < b.address; });
  for (size_t i = 1; i < image.regions.size(); ++i) {
    const Region &previous = image.regions[i - 1];
    if (previous.address + previous.bytes.size() > image.regions[i].address)
      throw std::invalid_argument("program image regions overlap");
  }

  std::vector<FunctionName> unique;
  std::set<uint64_t> seen;
  for (FunctionName &function : image.functions) {
    if (function.name.empty() || !seen.insert(function.address).second) continue;
    unique.push_back(std::move(function));
  }
  std::stable_sort(unique.begin(), unique.end(),
                   [](const FunctionName &a, const FunctionName &b) { return a.address < b.address; });
  image.functions = std::move(unique);
}

}  // namespace host
