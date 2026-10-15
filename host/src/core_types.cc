#include "core_types.hh"

#include <map>
#include <string>

namespace host {

using namespace ghidra;

namespace {

/// The parts of Ghidra's DataOrganization the core types depend on, with
/// DataOrganizationImpl's defaults.
struct DataOrganization {
  int charSize = 1;
  bool signedChar = true;
  int wideCharSize = 2;
  int shortSize = 2;
  int intSize = 4;
  int longSize = 4;
  int longLongSize = 8;
  int floatSize = 4;
  int doubleSize = 8;
  int longDoubleSize = 8;
};

DataOrganization readDataOrganization(const Element *compilerSpec) {
  DataOrganization org;
  if (compilerSpec == nullptr) return org;
  const Element *organization = nullptr;
  for (const Element *child : compilerSpec->getChildren())
    if (child->getName() == "data_organization") organization = child;
  if (organization == nullptr) return org;
  std::map<std::string, int *> sizes = {
      {"char_size", &org.charSize},        {"wchar_size", &org.wideCharSize},
      {"short_size", &org.shortSize},      {"integer_size", &org.intSize},
      {"long_size", &org.longSize},        {"long_long_size", &org.longLongSize},
      {"float_size", &org.floatSize},      {"double_size", &org.doubleSize},
      {"long_double_size", &org.longDoubleSize},
  };
  for (const Element *child : organization->getChildren()) {
    if (child->getName() == "char_type") {
      for (int4 i = 0; i < child->getNumAttributes(); ++i)
        if (child->getAttributeName(i) == "signed")
          org.signedChar = xml_readbool(child->getAttributeValue(i));
      continue;
    }
    auto size = sizes.find(child->getName());
    if (size == sizes.end()) continue;
    for (int4 i = 0; i < child->getNumAttributes(); ++i)
      if (child->getAttributeName(i) == "value") *size->second = std::stoi(child->getAttributeValue(i));
  }
  return org;
}

}  // namespace

void buildGhidraCoreTypes(TypeFactory &types, const Element *compilerSpec) {
  DataOrganization org = readDataOrganization(compilerSpec);

  types.setCoreType("void", 1, TYPE_VOID, false);
  for (int size = 1; size <= 8; ++size)
    types.setCoreType("undefined" + std::to_string(size), size, TYPE_UNKNOWN, false);

  // AbstractIntegerDataType.getSignedDataTypes and getUnsignedDataTypes: one
  // type per size from 1 to 8, then the 16-byte one, the C names replacing
  // the fixed-size ones in the order long long, long, short, int.
  std::string signedNames[9] = {"sbyte", "sword", "int3", "sdword", "int5", "int6", "int7", "sqword", "int16"};
  std::string unsignedNames[9] = {"byte", "word", "uint3", "dword", "uint5", "uint6", "uint7", "qword", "uint16"};
  auto rename = [&](int size, const char *signedName, const char *unsignedName) {
    if (size < 1 || size > 8) return;
    signedNames[size - 1] = signedName;
    unsignedNames[size - 1] = unsignedName;
  };
  rename(org.longLongSize, "longlong", "ulonglong");
  rename(org.longSize, "long", "ulong");
  rename(org.shortSize, "short", "ushort");
  rename(org.intSize, "int", "uint");
  for (int i = 0; i < 9; ++i) {
    int size = i < 8 ? i + 1 : 16;
    types.setCoreType(signedNames[i], size, TYPE_INT, false);
    types.setCoreType(unsignedNames[i], size, TYPE_UINT, false);
  }

  // AbstractFloatDataType.getFloatDataTypes: float, double and long double at
  // their sizes, then the fixed-size types for any size still free.
  std::map<int, std::string> floats;
  floats.emplace(org.floatSize, "float");
  floats.emplace(org.doubleSize, "double");
  floats.emplace(org.longDoubleSize, "longdouble");
  for (int size : {2, 4, 8, 10, 16}) floats.emplace(size, "float" + std::to_string(size));
  for (const auto &entry : floats) types.setCoreType(entry.second, entry.first, TYPE_FLOAT, false);

  types.setCoreType("code", 1, TYPE_CODE, false);
  types.setCoreType("char", org.charSize, org.signedChar ? TYPE_INT : TYPE_UINT, true);
  types.setCoreType("wchar_t", org.wideCharSize, TYPE_INT, true);
  if (org.wideCharSize != 2) types.setCoreType("wchar16", 2, TYPE_INT, true);
  if (org.wideCharSize != 4) types.setCoreType("wchar32", 4, TYPE_INT, true);
  types.setCoreType("bool", 1, TYPE_BOOL, false);
  types.cacheCoreTypes();
}

}  // namespace host
