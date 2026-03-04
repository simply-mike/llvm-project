#include "RISCZTargetMachine.h"
#include "TargetInfo/RISCZTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

#include <optional>

using namespace llvm;
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCZTarget() {
  RegisterTargetMachine<RISCZTargetMachine> A(getTheRISCZTarget());
}

static std::string computeDataLayout(const Triple &TT, StringRef CPU,
                                     const TargetOptions &Options) {
  std::string Ret = "e-m:e-p:32:32-i8:8:32-i16:16:32-i64:64-n32";

  if(!TT.isLittleEndian())
    report_fatal_error("RISCZ target supports only little-endian triples");
  return Ret;
}

static Reloc::Model getEffectiveRelocModel(bool JIT,
                                           std::optional<Reloc::Model> RM) {
  return (JIT || !RM) ? Reloc::Static : *RM;
}

RISCZTargetMachine::RISCZTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(
          T, computeDataLayout(TT, CPU, Options), TT, CPU, FS, Options,
          getEffectiveRelocModel(JIT, RM),
          getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
}

TargetPassConfig *RISCZTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new TargetPassConfig(*this, PM);
}