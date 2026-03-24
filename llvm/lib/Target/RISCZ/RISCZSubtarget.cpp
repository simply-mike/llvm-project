#include "RISCZSubtarget.h"

using namespace llvm;

#define DEBUG_TYPE "riscz-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "RISCZGenSubtargetInfo.inc"

void RISCZSubtarget::anchor() {}

RISCZSubtarget::RISCZSubtarget(const Triple &TT, const std::string &CPU,
                             const std::string &FS, const TargetMachine &TM)
    : RISCZGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS), InstrInfo(*this),
      FrameLowering(*this), TLInfo(TM, *this) {}