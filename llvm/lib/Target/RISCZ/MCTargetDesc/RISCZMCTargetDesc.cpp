#include "RISCZInfo.h"
#include "RISCZMCTargetDesc.h"
#include "TargetInfo/RISCZTargetInfo.h"
#include "RISCZInstPrinter.h"
#include "RISCZElfStreamer.h"
#include "RISCZObjectFileInfo.h"
#include "RISCZMCAsmInfo.h"
#include "RISCZTargetStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_ENUM
#define GET_REGINFO_MC_DESC
#include "RISCZGenRegisterInfo.inc"

#define ENABLE_INSTR_PREDICATE_VERIFIER
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_DESC
#include "RISCZGenInstrsInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "RISCZGenSubtargetInfo.inc"

static MCInstrInfo *createRISCZMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitRISCZMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createRISCZMCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  InitRISCZMCRegisterInfo(X, riscz::X1);
  return X;
}

static MCSubtargetInfo *createRISCZMCSubtargetInfo(const Triple &TT,
                                                    StringRef CPU, StringRef FS) {
  return createRISCZMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createRISCZMCAsmInfo(const MCRegisterInfo &MRI,
                                        const Triple &TT,
                                        const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new RISCZMCAsmInfo(TT);
  MCRegister SP = MRI.getDwarfRegNum(riscz::X2, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstPrinter *createRISCZMCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  return new RISCZInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *createRISCZTargetAsmStreamer(MCStreamer &S,
                                                       formatted_raw_ostream &OS,
                                                       MCInstPrinter *InstPrint) {
  return new RISCZTargetStreamer(S);
}

static MCObjectFileInfo *
createRISCZMCObjectFileInfo(MCContext &Ctx, bool PIC,
                             bool LargeCodeModel = false) {
  MCObjectFileInfo *MOFI = new RISCZMCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

static MCTargetStreamer *
createRISCZObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new RISCZTargetELFStreamer(S, STI);
  return nullptr;
}

class RISCZMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit RISCZMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    if (isConditionalBranch(Inst)) {
      int64_t Imm;
      if (Size == 2)
        Imm = Inst.getOperand(1).getImm();
      else
        Imm = Inst.getOperand(2).getImm();
      Target = Addr + Imm;
      return true;
    }

    if (Inst.getOpcode() == riscz::JAL) {
      Target = Addr + Inst.getOperand(1).getImm();
      return true;
    }

    return false;
  }
};

static MCInstrAnalysis *createRISCZInstrAnalysis(const MCInstrInfo *Info) {
  return new RISCZMCInstrAnalysis(Info);
}

static MCTargetStreamer *createRISCZNullTargetStreamer(MCStreamer &S) {
  return new RISCZTargetStreamer(S);
}

namespace {
MCStreamer *createRISCZELFStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&MOW,
                                    std::unique_ptr<MCCodeEmitter> &&MCE) {
  return createRISCZELFStreamer(Context, std::move(MAB), std::move(MOW),
                                 std::move(MCE));
}
} // end anonymous namespace

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCZTargetMC() {
  // Register the MC asm info.
  Target &TheRISCZTarget = getTheRISCZTarget();
  RegisterMCAsmInfoFn X(TheRISCZTarget, createRISCZMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCObjectFileInfo(TheRISCZTarget, createRISCZMCObjectFileInfo);
  TargetRegistry::RegisterMCInstrInfo(TheRISCZTarget, createRISCZMCInstrInfo);
  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(TheRISCZTarget, createRISCZMCRegisterInfo);

  TargetRegistry::RegisterMCAsmBackend(TheRISCZTarget, createRISCZAsmBackend);
  TargetRegistry::RegisterMCCodeEmitter(TheRISCZTarget, createRISCZMCCodeEmitter);
  TargetRegistry::RegisterMCInstPrinter(TheRISCZTarget, createRISCZMCInstPrinter);
  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(TheRISCZTarget,
                                          createRISCZMCSubtargetInfo);
  TargetRegistry::RegisterELFStreamer(TheRISCZTarget, createRISCZELFStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(TheRISCZTarget,
                                               createRISCZObjectTargetStreamer);
  TargetRegistry::RegisterMCInstrAnalysis(TheRISCZTarget, createRISCZInstrAnalysis);
  // Register the MCInstPrinter
  TargetRegistry::RegisterMCInstPrinter(TheRISCZTarget, createRISCZMCInstPrinter);

  TargetRegistry::RegisterAsmTargetStreamer(TheRISCZTarget,
                                            createRISCZTargetAsmStreamer);

  TargetRegistry::RegisterNullTargetStreamer(TheRISCZTarget,
                                               createRISCZNullTargetStreamer);
}