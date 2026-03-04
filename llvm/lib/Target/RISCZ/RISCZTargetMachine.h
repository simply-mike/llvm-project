#pragma once

#include "MCTargetDesc/RISCZMCTargetDesc.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>
#include <optional>

namespace llvm {

extern Target TheRISCZTarget;

class RISCZTargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;

public:
  RISCZTargetMachine(const Target &T, const Triple &TT,
                     StringRef CPU, StringRef FS,
                     const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM,
                     CodeGenOptLevel OL,
                     bool JIT);

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

} // namespace llvm