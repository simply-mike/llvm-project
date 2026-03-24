//===----------------------------------------------------------------------===//
//
// Implements the info about RISC-Z target spec.
//
//===----------------------------------------------------------------------===//

#include "RISCZTargetMachine.h"
#include "RISCZMachineFunctionInfo.h"
#include "TargetInfo/RISCZTargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"

#define DEBUG_TYPE "sim"

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

/// TargetMachine ctor - Create an LP64 Architecture model
RISCZTargetMachine::RISCZTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128",
                        TT, CPU, FS, Options, getEffectiveRelocModel(TT, RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

RISCZTargetMachine::~RISCZTargetMachine() = default;

MachineFunctionInfo *RISCZTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return RISCZFunctionInfo::create<RISCZFunctionInfo>(Allocator, F, STI);
}

namespace {

class RISCZPassConfig : public TargetPassConfig {
public:
  RISCZPassConfig(RISCZTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  RISCZTargetMachine &getRISCZTargetMachine() const {
    return getTM<RISCZTargetMachine>();
  }

  bool addInstSelector() override;
};

} // anonymous namespace

TargetPassConfig *RISCZTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RISCZPassConfig(*this, PM);
}

bool RISCZPassConfig::addInstSelector() {
  addPass(createRISCZISelDag(getRISCZTargetMachine(), getOptLevel()));
  return false;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCZTarget() {
  RegisterTargetMachine<RISCZTargetMachine> X(getTheRISCZTarget());
}