#include "json.hh"

#include <sstream>

namespace host {

void writeJsonString(std::ostream &out, const std::string &text) {
  static const char *hex = "0123456789abcdef";
  out << '"';
  for (char c : text) {
    unsigned char byte = (unsigned char)c;
    if (c == '"') out << "\\\"";
    else if (c == '\\') out << "\\\\";
    else if (c == '\n') out << "\\n";
    else if (c == '\t') out << "\\t";
    else if (c == '\r') out << "\\r";
    else if (byte < 0x20) out << "\\u00" << hex[byte >> 4] << hex[byte & 0xf];
    else out << c;
  }
  out << '"';
}

std::string jsonStrings(const std::vector<std::string> &strings) {
  std::ostringstream out;
  out << '[';
  for (size_t i = 0; i < strings.size(); ++i) {
    if (i > 0) out << ',';
    writeJsonString(out, strings[i]);
  }
  out << ']';
  return out.str();
}

}  // namespace host
