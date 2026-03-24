#ifndef __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCASMINFO_H__
#define __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCASMINFO_H__

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class RISCZMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit RISCZMCAsmInfo(const Triple &TT);
};

} // end namespace llvm

#endif // __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCASMINFO_H__