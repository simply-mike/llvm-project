#include "TargetInfo/RISCZTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheRISCZTarget() {
  static Target TheRISCZTarget;
  return TheRISCZTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCZTargetInfo() {
  RegisterTarget<Triple::riscz, /*HasJIT=*/false> X(
        getTheRISCZTarget(), "riscz", "64-bit RISC-Z", "RISCZ");
}