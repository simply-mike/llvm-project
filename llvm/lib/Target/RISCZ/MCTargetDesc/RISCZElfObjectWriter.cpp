#include "MCTargetDesc/RISCZFixupKinds.h"
#include "MCTargetDesc/RISCZMCExpr.h"
#include "MCTargetDesc/RISCZMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class RISCZELFObjectWriter : public MCELFObjectTargetWriter {
public:
  RISCZELFObjectWriter(uint8_t OSABI, bool Is64Bit);

  ~RISCZELFObjectWriter() override;

  // Return true if the given relocation must be with a symbol rather than
  // section plus offset.
  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override {
    return true;
  }

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};
}

RISCZELFObjectWriter::RISCZELFObjectWriter(uint8_t OSABI, bool Is64Bit)
    : MCELFObjectTargetWriter(Is64Bit, OSABI, ELF::EM_RISCZ,
                              /*HasRelocationAddend*/ true) {}

RISCZELFObjectWriter::~RISCZELFObjectWriter() {}

unsigned RISCZELFObjectWriter::getRelocType(MCContext &Ctx,
                                             const MCValue &Target,
                                             const MCFixup &Fixup,
                                             bool IsPCRel) const {
  const MCExpr *Expr = Fixup.getValue();
  // Determine the type of the relocation
  unsigned Kind = Fixup.getTargetKind();
  if (Kind >= FirstLiteralRelocationKind)
    return Kind - FirstLiteralRelocationKind;
  if (IsPCRel) {
    switch (Kind) {
    default:
      Ctx.reportError(Fixup.getLoc(), "Unsupported relocation type");
      return ELF::R_RISCZ_NONE;
    case FK_Data_4:
    case FK_PCRel_4:
      return ELF::R_RISCZ_32_PCREL;
    case riscz::fixup_RISCZ_pcrel_hi20:
      return ELF::R_RISCZ_PCREL_HI20;
    case riscz::fixup_RISCZ_pcrel_lo12_i:
      return ELF::R_RISCZ_PCREL_LO12_I;
    case riscz::fixup_RISCZ_pcrel_lo12_s:
      return ELF::R_RISCZ_PCREL_LO12_S;
    case riscz::fixup_RISCZ_got_hi20:
      return ELF::R_RISCZ_GOT_HI20;
    case riscz::fixup_RISCZ_tls_got_hi20:
      return ELF::R_RISCZ_TLS_GOT_HI20;
    case riscz::fixup_RISCZ_tls_gd_hi20:
      return ELF::R_RISCZ_TLS_GD_HI20;
    case riscz::fixup_RISCZ_jal:
      return ELF::R_RISCZ_JAL;
    case riscz::fixup_RISCZ_branch:
      return ELF::R_RISCZ_BRANCH;
    case riscz::fixup_RISCZ_call:
      return ELF::R_RISCZ_CALL;
    case riscz::fixup_RISCZ_call_plt:
      return ELF::R_RISCZ_CALL_PLT;
    case riscz::fixup_RISCZ_add_8:
      return ELF::R_RISCZ_ADD8;
    case riscz::fixup_RISCZ_sub_8:
      return ELF::R_RISCZ_SUB8;
    case riscz::fixup_RISCZ_add_16:
      return ELF::R_RISCZ_ADD16;
    case riscz::fixup_RISCZ_sub_16:
      return ELF::R_RISCZ_SUB16;
    case riscz::fixup_RISCZ_add_32:
      return ELF::R_RISCZ_ADD32;
    case riscz::fixup_RISCZ_sub_32:
      return ELF::R_RISCZ_SUB32;
    case riscz::fixup_RISCZ_add_64:
      return ELF::R_RISCZ_ADD64;
    case riscz::fixup_RISCZ_sub_64:
      return ELF::R_RISCZ_SUB64;
    }
  }

  switch (Kind) {
  default:
    Ctx.reportError(Fixup.getLoc(), "Unsupported relocation type");
    return ELF::R_RISCZ_NONE;
  case FK_Data_1:
    Ctx.reportError(Fixup.getLoc(), "1-byte data relocations not supported");
    return ELF::R_RISCZ_NONE;
  case FK_Data_2:
    Ctx.reportError(Fixup.getLoc(), "2-byte data relocations not supported");
    return ELF::R_RISCZ_NONE;
  case FK_Data_4:
    if (Expr->getKind() == MCExpr::Target &&
        cast<RISCZMCExpr>(Expr)->getKind() == RISCZMCExpr::VK_RISCZ_32_PCREL)
      return ELF::R_RISCZ_32_PCREL;
    return ELF::R_RISCZ_32;
  case FK_Data_8:
    return ELF::R_RISCZ_64;
  case riscz::fixup_RISCZ_hi20:
    return ELF::R_RISCZ_HI20;
  case riscz::fixup_RISCZ_lo12_i:
    return ELF::R_RISCZ_LO12_I;
  case riscz::fixup_RISCZ_lo12_s:
    return ELF::R_RISCZ_LO12_S;
  case riscz::fixup_RISCZ_tprel_hi20:
    return ELF::R_RISCZ_TPREL_HI20;
  case riscz::fixup_RISCZ_tprel_lo12_i:
    return ELF::R_RISCZ_TPREL_LO12_I;
  case riscz::fixup_RISCZ_tprel_lo12_s:
    return ELF::R_RISCZ_TPREL_LO12_S;
  case riscz::fixup_RISCZ_tprel_add:
    return ELF::R_RISCZ_TPREL_ADD;
  case riscz::fixup_RISCZ_relax:
    return ELF::R_RISCZ_RELAX;
  case riscz::fixup_RISCZ_align:
    return ELF::R_RISCZ_ALIGN;
  case riscz::fixup_RISCZ_set_6b:
    return ELF::R_RISCZ_SET6;
  case riscz::fixup_RISCZ_sub_6b:
    return ELF::R_RISCZ_SUB6;
  case riscz::fixup_RISCZ_add_8:
    return ELF::R_RISCZ_ADD8;
  case riscz::fixup_RISCZ_set_8:
    return ELF::R_RISCZ_SET8;
  case riscz::fixup_RISCZ_sub_8:
    return ELF::R_RISCZ_SUB8;
  case riscz::fixup_RISCZ_set_16:
    return ELF::R_RISCZ_SET16;
  case riscz::fixup_RISCZ_add_16:
    return ELF::R_RISCZ_ADD16;
  case riscz::fixup_RISCZ_sub_16:
    return ELF::R_RISCZ_SUB16;
  case riscz::fixup_RISCZ_set_32:
    return ELF::R_RISCZ_SET32;
  case riscz::fixup_RISCZ_add_32:
    return ELF::R_RISCZ_ADD32;
  case riscz::fixup_RISCZ_sub_32:
    return ELF::R_RISCZ_SUB32;
  case riscz::fixup_RISCZ_add_64:
    return ELF::R_RISCZ_ADD64;
  case riscz::fixup_RISCZ_sub_64:
    return ELF::R_RISCZ_SUB64;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createRISCZELFObjectWriter(uint8_t OSABI, bool Is64Bit) {
  return std::make_unique<RISCZELFObjectWriter>(OSABI, Is64Bit);
}