// A Ghidra LoadImage answering from a ProgramImage held in memory.
#ifndef DECOMPILER_HOST_IMAGE_LOAD_IMAGE_HH
#define DECOMPILER_HOST_IMAGE_LOAD_IMAGE_HH

#include "loadimage.hh"
#include "program_image.hh"

namespace host {

/// Serves the image's regions as the default code space and its function
/// names as the loader's symbols. Reads follow Ghidra's file loaders
/// (LoadImageBfd, RawLoadImage): a read starting on an unmapped byte throws
/// DataUnavailError, and one running past the end of the mapped bytes is
/// filled with zeros, since SLEIGH fetches 16 bytes for every instruction,
/// including the last one before a gap.
class ImageLoadImage : public ghidra::LoadImage {
  ProgramImage image;             // regions sorted by address
  std::string archType;
  ghidra::AddrSpace *space = nullptr;
  mutable size_t nextSymbol = 0;

 public:
  ImageLoadImage(ProgramImage image, const std::string &archType);
  void attachToSpace(ghidra::AddrSpace *codeSpace) { space = codeSpace; }

  void loadFill(ghidra::uint1 *ptr, ghidra::int4 size, const ghidra::Address &addr) override;
  void openSymbols(void) const override { nextSymbol = 0; }
  bool getNextSymbol(ghidra::LoadImageFunc &record) const override;
  void getReadonly(ghidra::RangeList &list) const override;
  std::string getArchType(void) const override { return archType; }
  void adjustVma(long adjust) override;
};

}  // namespace host

#endif
