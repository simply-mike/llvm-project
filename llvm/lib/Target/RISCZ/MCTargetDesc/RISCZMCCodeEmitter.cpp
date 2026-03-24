#include "RISCZInfo.h"
#include "RISCZFixupKinds.h"
#include "RISCZMCExpr.h"
#include "RISCZMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

#define GET_REGINFO_ENUM
#include "RISCZGenRegisterInfo.inc"

#define GET_COMPUTE_FEATURES
#define GET_INSTRINFO_MC_HELPER_DECLS
#define GET_INSTRINFO_ENUM
#include "RISCZGenInstrsInfo.inc"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");
STATISTIC(MCNumFixups, "Number of MC fixups created");

namespace {
class RISCZMCCodeEmitter : public MCCodeEmitter {
  RISCZMCCodeEmitter(const RISCZMCCodeEmitter &) = delete;
  void operator=(const RISCZMCCodeEmitter &) = delete;
  MCContext &Ctx;
  MCInstrInfo const &MCII;

public:
  RISCZMCCodeEmitter(MCContext &ctx, MCInstrInfo const &MCII)
      : Ctx(ctx), MCII(MCII) {}

  ~RISCZMCCodeEmitter() override {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  void expandFunctionCall(const MCInst &MI, SmallVectorImpl<char> &CB,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  // void expandAddTPRel(const MCInst &MI, raw_ostream &OS,
  //                     SmallVectorImpl<MCFixup> &Fixups,
  //                     const MCSubtargetInfo &STI) const;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Return binary encoding of operand. If the machine operand requires
  /// relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImmOpValue(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;
};
} // end anonymous namespace

MCCodeEmitter *llvm::createRISCZMCCodeEmitter(const MCInstrInfo &MCII,
                                               MCContext &Ctx) {
  return new RISCZMCCodeEmitter(Ctx, MCII);
}

void RISCZMCCodeEmitter::expandFunctionCall(const MCInst &MI, SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  MCInst TmpInst;
  MCOperand Func;
  MCRegister Ra;
  uint32_t Binary;

  if (MI.getOpcode() == riscz::PseudoCALL) {
    Func = MI.getOperand(0);
    Ra = riscz::X1;
  }
  assert(Func.isExpr() && "Expected expression");

  const MCExpr *CallExpr = Func.getExpr();

  // Emit AUIPC Ra, Func with R_RISCZ_CALL relocation type.
  TmpInst = MCInstBuilder(riscz::AUIPC)
                .addReg(Ra)
                .addOperand(MCOperand::createExpr(CallExpr));
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, endianness::little);

  // Emit JALR Ra, Ra, 0
  TmpInst = MCInstBuilder(riscz::JALR).addReg(Ra).addReg(Ra).addImm(0);
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, endianness::little);
}

// Expand PseudoAddTPRel to a simple ADD with the correct relocation.
// void RISCZMCCodeEmitter::expandAddTPRel(const MCInst &MI, raw_ostream &OS,
//                                          SmallVectorImpl<MCFixup> &Fixups,
//                                          const MCSubtargetInfo &STI) const {
//   MCOperand DestReg = MI.getOperand(0);
//   MCOperand SrcReg = MI.getOperand(1);
//   MCOperand TPReg = MI.getOperand(2);
//   assert(TPReg.isReg() && TPReg.getReg() == riscz::X4 &&
//          "Expected thread pointer as second input to TP-relative add");

//   MCOperand SrcSymbol = MI.getOperand(3);
//   assert(SrcSymbol.isExpr() &&
//          "Expected expression as third input to TP-relative add");

//   const RISCZMCExpr *Expr = dyn_cast<RISCZMCExpr>(SrcSymbol.getExpr());
//   assert(Expr && Expr->getKind() == RISCZMCExpr::VK_RISCZ_TPREL_ADD &&
//          "Expected tprel_add relocation on TP-relative symbol");

//   // Emit the correct tprel_add relocation for the symbol.
//   Fixups.push_back(MCFixup::create(
//       0, Expr, MCFixupKind(riscz::fixup_RISCZ_tprel_add), MI.getLoc()));

//   // Emit a normal ADD instruction with the given operands.
//   MCInst TmpInst = MCInstBuilder(riscz::ADD)
//                        .addOperand(DestReg)
//                        .addOperand(SrcReg)
//                        .addOperand(TPReg);
//   uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
//   support::endian::write(OS, Binary, endianness::little);
// }

void RISCZMCCodeEmitter::encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  RISCZ_MC::verifyInstructionPredicates(MI.getOpcode(),
      RISCZ_MC::computeAvailableFeatures(STI.getFeatureBits()));

  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  // Get byte count of instruction.
  unsigned Size = Desc.getSize();

  std::string msg;
  raw_string_ostream Msg(msg);
  Msg << MI;
  printf("[DUMP] %s\n\n", Msg.str().c_str());
  // RISCZInstrInfo::getInstSizeInBytes expects that the total size of the
  // expanded instructions for each pseudo is correct in the Size field of the
  // tablegen definition for the pseudo.
  if (MI.getOpcode() == riscz::PseudoCALL) {
    expandFunctionCall(MI, CB, Fixups, STI);
    MCNumEmitted += 2;
    return;
  }

  switch (Size) {
  default:
    llvm_unreachable("Unhandled encodeInstruction length!");
  // case 2: {
  //   uint16_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
  //   support::endian::write<uint16_t>(CB, Bits, endianness::little);
  //   break;
  // }
  case 4: {
    uint32_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write(CB, Bits, endianness::little);
    break;
  }
  }

  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

unsigned
RISCZMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {

  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  llvm_unreachable("Unhandled expression!");
  return 0;
}

unsigned
RISCZMCCodeEmitter::getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    unsigned Res = MO.getImm();
    assert((Res & 1) == 0 && "LSB is non-zero");
    return Res >> 1;
  }

  return getImmOpValue(MI, OpNo, Fixups, STI);
}

unsigned RISCZMCCodeEmitter::getImmOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  MCInstrDesc const &Desc = MCII.get(MI.getOpcode());
  unsigned MIFrm = risczII::getFormat(Desc.TSFlags);

  // If the destination is an immediate, there is nothing to do.
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() &&
         "getImmOpValue expects only expressions or immediates");
  const MCExpr *Expr = MO.getExpr();
  MCExpr::ExprKind Kind = Expr->getKind();
  riscz::Fixups FixupKind = riscz::fixup_RISCZ_invalid;
  if (Kind == MCExpr::Target) {
    const RISCZMCExpr *RVExpr = cast<RISCZMCExpr>(Expr);

    switch (RVExpr->getKind()) {
    case RISCZMCExpr::VK_RISCZ_None:
    case RISCZMCExpr::VK_RISCZ_Invalid:
    case RISCZMCExpr::VK_RISCZ_32_PCREL:
      llvm_unreachable("Unhandled fixup kind!");
    case RISCZMCExpr::VK_RISCZ_TPREL_ADD:
      // tprel_add is only used to indicate that a relocation should be emitted
      // for an add instruction used in TP-relative addressing. It should not be
      // expanded as if representing an actual instruction operand and so to
      // encounter it here is an error.
      llvm_unreachable(
          "VK_RISCZ_TPREL_ADD should not represent an instruction operand");
    case RISCZMCExpr::VK_RISCZ_LO:
      if (MIFrm == risczII::InstFormatI)
        FixupKind = riscz::fixup_RISCZ_lo12_i;
      else if (MIFrm == risczII::InstFormatS)
        FixupKind = riscz::fixup_RISCZ_lo12_s;
      else
        llvm_unreachable("VK_RISCZ_LO used with unexpected instruction format");
      break;
    case RISCZMCExpr::VK_RISCZ_HI:
      FixupKind = riscz::fixup_RISCZ_hi20;
      break;
    case RISCZMCExpr::VK_RISCZ_PCREL_LO:
      if (MIFrm == risczII::InstFormatI)
        FixupKind = riscz::fixup_RISCZ_pcrel_lo12_i;
      else if (MIFrm == risczII::InstFormatS)
        FixupKind = riscz::fixup_RISCZ_pcrel_lo12_s;
      else
        llvm_unreachable(
            "VK_RISCZ_PCREL_LO used with unexpected instruction format");
      break;
    case RISCZMCExpr::VK_RISCZ_PCREL_HI:
      FixupKind = riscz::fixup_RISCZ_pcrel_hi20;
      break;
    case RISCZMCExpr::VK_RISCZ_GOT_HI:
      FixupKind = riscz::fixup_RISCZ_got_hi20;
      break;
    case RISCZMCExpr::VK_RISCZ_TPREL_LO:
      if (MIFrm == risczII::InstFormatI)
        FixupKind = riscz::fixup_RISCZ_tprel_lo12_i;
      else if (MIFrm == risczII::InstFormatS)
        FixupKind = riscz::fixup_RISCZ_tprel_lo12_s;
      else
        llvm_unreachable(
            "VK_RISCZ_TPREL_LO used with unexpected instruction format");
      break;
    case RISCZMCExpr::VK_RISCZ_TPREL_HI:
      FixupKind = riscz::fixup_RISCZ_tprel_hi20;
      break;
    case RISCZMCExpr::VK_RISCZ_TLS_GOT_HI:
      FixupKind = riscz::fixup_RISCZ_tls_got_hi20;
      break;
    case RISCZMCExpr::VK_RISCZ_TLS_GD_HI:
      FixupKind = riscz::fixup_RISCZ_tls_gd_hi20;
      break;
    case RISCZMCExpr::VK_RISCZ_CALL:
      FixupKind = riscz::fixup_RISCZ_call;
      break;
    case RISCZMCExpr::VK_RISCZ_CALL_PLT:
      FixupKind = riscz::fixup_RISCZ_call_plt;
      break;
    }
  } else if (Kind == MCExpr::SymbolRef &&
             cast<MCSymbolRefExpr>(Expr)->getKind() == MCSymbolRefExpr::VK_None) {
    if (MIFrm == risczII::InstFormatJ) {
      FixupKind = riscz::fixup_RISCZ_jal;
    } else if (MIFrm == risczII::InstFormatB) {
      FixupKind = riscz::fixup_RISCZ_branch;
    } else {
      llvm_unreachable("Unhandled fixup");
    }
  }

  assert(FixupKind != riscz::fixup_RISCZ_invalid && "Unhandled expression!");

  Fixups.push_back(
      MCFixup::create(0, Expr, MCFixupKind(FixupKind), MI.getLoc()));
  ++MCNumFixups;

  return 0;
}

#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "RISCZGenMCCodeEmitter.inc"