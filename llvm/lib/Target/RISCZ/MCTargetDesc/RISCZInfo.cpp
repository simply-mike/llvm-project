#include "RISCZInfo.h"

#define GET_REGINFO_ENUM
#include "RISCZGenRegisterInfo.inc"

namespace llvm {
  namespace risczABI {
    MCRegister getBPReg() { return riscz::X9; }
    MCRegister getSCSPReg() { return riscz::X18; }
  } // namespace risczABI
} // namespace llvm