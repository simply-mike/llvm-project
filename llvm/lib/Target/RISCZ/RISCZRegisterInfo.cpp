#include "MCTargetDesc/RISCZInfo.h"
#include "RISCZRegisterInfo.h"
#include "RISCZInstrsInfo.h"
#include "RISCZSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "RISCZGenRegisterInfo.inc"

RISCZRegisterInfo::RISCZRegisterInfo() : RISCZGenRegisterInfo(riscz::X1) {}

const MCPhysReg *RISCZRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  if (MF->getFunction().getCallingConv() == CallingConv::GHC)
    return CSR_NoRegs_SaveList;

  return CSR_ILP32_LP64_SaveList;
}

BitVector RISCZRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  markSuperRegs(Reserved, riscz::X0); // zero
  markSuperRegs(Reserved, riscz::X2); // sp
  markSuperRegs(Reserved, riscz::X3); // gp
  markSuperRegs(Reserved, riscz::X4); // tp
  return Reserved;
}

bool RISCZRegisterInfo::requiresRegisterScavenging(const MachineFunction &MF) const {
  return false;
}

bool RISCZRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                             int SPAdj, unsigned FIOperandNum,
                                             RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  int Offset = getFrameLowering(MF)
                   ->getFrameIndexReference(MF, FrameIndex, FrameReg)
                   .getFixed();
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  if (!isInt<16>(Offset)) {
    llvm_unreachable("");
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false, false, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);

  return false;
}

Register RISCZRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? riscz::X8 : riscz::X2;
}

const uint32_t *RISCZRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                                         CallingConv::ID CC) const {
  auto &Subtarget = MF.getSubtarget<RISCZSubtarget>();

  if (CC == CallingConv::GHC)
    return CSR_NoRegs_RegMask;

  switch (Subtarget.getTargetABI()) {
  default:
    llvm_unreachable("Unrecognized ABI");
  case risczABI::ABI_LP64:
    return CSR_ILP32_LP64_RegMask;
  }
}