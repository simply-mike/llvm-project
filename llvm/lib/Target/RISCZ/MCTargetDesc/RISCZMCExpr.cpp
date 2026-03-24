#include "RISCZMCExpr.h"
#include "RISCZFixupKinds.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

#define DEBUG_TYPE "riscz-mcexpr"

const RISCZMCExpr *RISCZMCExpr::create(const MCExpr *Expr, VariantKind Kind,
                                         MCContext &Ctx) {
  return new (Ctx) RISCZMCExpr(Expr, Kind);
}

void RISCZMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  VariantKind Kind = getKind();
  bool HasVariant = ((Kind != VK_RISCZ_None) && (Kind != VK_RISCZ_CALL) &&
                     (Kind != VK_RISCZ_CALL_PLT));

  if (HasVariant)
    OS << '%' << getVariantKindName(Kind) << '(';
  Expr->print(OS, MAI);
  if (Kind == VK_RISCZ_CALL_PLT)
    OS << "@plt";
  if (HasVariant)
    OS << ')';
}

const MCFixup *RISCZMCExpr::getPCRelHiFixup(const MCFragment **DFOut) const {
  MCValue AUIPCLoc;
  if (!getSubExpr()->evaluateAsRelocatable(AUIPCLoc, nullptr, nullptr))
    return nullptr;

  const MCSymbolRefExpr *AUIPCSRE = AUIPCLoc.getSymA();
  if (!AUIPCSRE)
    return nullptr;

  const MCSymbol *AUIPCSymbol = &AUIPCSRE->getSymbol();
  const auto *DF = dyn_cast_or_null<MCDataFragment>(AUIPCSymbol->getFragment());

  if (!DF)
    return nullptr;

  uint64_t Offset = AUIPCSymbol->getOffset();
  if (DF->getContents().size() == Offset) {
    DF = dyn_cast_or_null<MCDataFragment>(DF->getNext());
    if (!DF)
      return nullptr;
    Offset = 0;
  }

  for (const MCFixup &F : DF->getFixups()) {
    if (F.getOffset() != Offset)
      continue;

    switch ((unsigned)F.getKind()) {
    default:
      continue;
    case riscz::fixup_RISCZ_got_hi20:
    case riscz::fixup_RISCZ_tls_got_hi20:
    case riscz::fixup_RISCZ_tls_gd_hi20:
    case riscz::fixup_RISCZ_pcrel_hi20:
      if (DFOut)
        *DFOut = DF;
      return &F;
    }
  }

  return nullptr;
}

bool RISCZMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                             const MCAssembler *Asm,
                                             const MCFixup *Fixup) const {
  // Explicitly drop the layout and assembler to prevent any symbolic folding in
  // the expression handling.  This is required to preserve symbolic difference
  // expressions to emit the paired relocations.
  if (!getSubExpr()->evaluateAsRelocatable(Res, nullptr, nullptr))
    return false;

  Res =
      MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(), getKind());
  // Custom fixup types are not valid with symbol difference expressions.
  return Res.getSymB() ? getKind() == VK_RISCZ_None : true;
}

void RISCZMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

RISCZMCExpr::VariantKind RISCZMCExpr::getVariantKindForName(StringRef name) {
  return StringSwitch<RISCZMCExpr::VariantKind>(name)
      .Case("lo", VK_RISCZ_LO)
      .Case("hi", VK_RISCZ_HI)
      .Case("pcrel_lo", VK_RISCZ_PCREL_LO)
      .Case("pcrel_hi", VK_RISCZ_PCREL_HI)
      .Case("got_pcrel_hi", VK_RISCZ_GOT_HI)
      .Case("tprel_lo", VK_RISCZ_TPREL_LO)
      .Case("tprel_hi", VK_RISCZ_TPREL_HI)
      .Case("tprel_add", VK_RISCZ_TPREL_ADD)
      .Case("tls_ie_pcrel_hi", VK_RISCZ_TLS_GOT_HI)
      .Case("tls_gd_pcrel_hi", VK_RISCZ_TLS_GD_HI)
      .Default(VK_RISCZ_Invalid);
}

StringRef RISCZMCExpr::getVariantKindName(VariantKind Kind) {
  switch (Kind) {
  case VK_RISCZ_Invalid:
  case VK_RISCZ_None:
    llvm_unreachable("Invalid ELF symbol kind");
  case VK_RISCZ_LO:
    return "lo";
  case VK_RISCZ_HI:
    return "hi";
  case VK_RISCZ_PCREL_LO:
    return "pcrel_lo";
  case VK_RISCZ_PCREL_HI:
    return "pcrel_hi";
  case VK_RISCZ_GOT_HI:
    return "got_pcrel_hi";
  case VK_RISCZ_TPREL_LO:
    return "tprel_lo";
  case VK_RISCZ_TPREL_HI:
    return "tprel_hi";
  case VK_RISCZ_TPREL_ADD:
    return "tprel_add";
  case VK_RISCZ_TLS_GOT_HI:
    return "tls_ie_pcrel_hi";
  case VK_RISCZ_TLS_GD_HI:
    return "tls_gd_pcrel_hi";
  case VK_RISCZ_CALL:
    return "call";
  case VK_RISCZ_CALL_PLT:
    return "call_plt";
  case VK_RISCZ_32_PCREL:
    return "32_pcrel";
  }
  llvm_unreachable("Invalid ELF symbol kind");
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    llvm_unreachable("Can't handle nested target expression");
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    fixELFSymbolsInTLSFixupsImpl(BE->getLHS(), Asm);
    fixELFSymbolsInTLSFixupsImpl(BE->getRHS(), Asm);
    break;
  }

  case MCExpr::SymbolRef: {
    // We're known to be under a TLS fixup, so any symbol should be
    // modified. There should be only one.
    const MCSymbolRefExpr &SymRef = *cast<MCSymbolRefExpr>(Expr);
    cast<MCSymbolELF>(SymRef.getSymbol()).setType(ELF::STT_TLS);
    break;
  }

  case MCExpr::Unary:
    fixELFSymbolsInTLSFixupsImpl(cast<MCUnaryExpr>(Expr)->getSubExpr(), Asm);
    break;
  }
}

void RISCZMCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  switch (getKind()) {
  default:
    return;
  case VK_RISCZ_TPREL_HI:
  case VK_RISCZ_TLS_GOT_HI:
  case VK_RISCZ_TLS_GD_HI:
    break;
  }

  fixELFSymbolsInTLSFixupsImpl(getSubExpr(), Asm);
}

bool RISCZMCExpr::evaluateAsConstant(int64_t &Res) const {
  MCValue Value;

  if (Kind == VK_RISCZ_PCREL_HI   || Kind == VK_RISCZ_PCREL_LO ||
      Kind == VK_RISCZ_GOT_HI     || Kind == VK_RISCZ_TPREL_HI ||
      Kind == VK_RISCZ_TPREL_LO   || Kind == VK_RISCZ_TPREL_ADD ||
      Kind == VK_RISCZ_TLS_GOT_HI || Kind == VK_RISCZ_TLS_GD_HI ||
      Kind == VK_RISCZ_CALL       || Kind == VK_RISCZ_CALL_PLT)
    return false;

  if (!getSubExpr()->evaluateAsRelocatable(Value, nullptr, nullptr))
    return false;

  if (!Value.isAbsolute())
    return false;

  Res = evaluateAsInt64(Value.getConstant());
  return true;
}

int64_t RISCZMCExpr::evaluateAsInt64(int64_t Value) const {
  switch (Kind) {
  default:
    llvm_unreachable("Invalid kind");
  case VK_RISCZ_LO:
    return SignExtend64<12>(Value);
  case VK_RISCZ_HI:
    // Add 1 if bit 11 is 1, to compensate for low 12 bits being negative.
    return ((Value + 0x800) >> 12) & 0xfffff;
  }
}