// Writing JSON, for the results the wasm exports hand to JavaScript.
#ifndef DECOMPILER_HOST_JSON_HH
#define DECOMPILER_HOST_JSON_HH

#include <ostream>
#include <string>
#include <vector>

namespace host {

/// Writes text as a JSON string. Text is UTF-8 and passes through as it is;
/// quotes, backslashes and control characters are escaped.
void writeJsonString(std::ostream &out, const std::string &text);

/// A JSON array of strings.
std::string jsonStrings(const std::vector<std::string> &strings);

}  // namespace host

#endif
