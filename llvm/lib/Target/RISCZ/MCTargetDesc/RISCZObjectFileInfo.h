#ifndef LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCOBJECTFILEINFO_H
#define LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCOBJECTFILEINFO_H

#include "llvm/MC/MCObjectFileInfo.h"

namespace llvm {

class RISCZMCObjectFileInfo : public MCObjectFileInfo {
public:
  unsigned getTextSectionAlignment() const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCOBJECTFILEINFO_H