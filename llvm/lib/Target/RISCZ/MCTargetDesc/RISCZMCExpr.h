#ifndef __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCEXPR_H__
#define __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCEXPR_H__

#include "llvm/MC/MCExpr.h"

namespace llvm {

class StringRef;

class RISCZMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_RISCZ_None,
    VK_RISCZ_LO,
    VK_RISCZ_HI,
    VK_RISCZ_PCREL_LO,
    VK_RISCZ_PCREL_HI,
    VK_RISCZ_GOT_HI,
    VK_RISCZ_TPREL_LO,
    VK_RISCZ_TPREL_HI,
    VK_RISCZ_TPREL_ADD,
    VK_RISCZ_TLS_GOT_HI,
    VK_RISCZ_TLS_GD_HI,
    VK_RISCZ_CALL,
    VK_RISCZ_CALL_PLT,
    VK_RISCZ_32_PCREL,
    VK_RISCZ_Invalid // Must be the last item
  };

private:
  const MCExpr *Expr;
  const VariantKind Kind;

  int64_t evaluateAsInt64(int64_t Value) const;

  explicit RISCZMCExpr(const MCExpr *Expr, VariantKind Kind)
      : Expr(Expr), Kind(Kind) {}

public:
  static const RISCZMCExpr *create(const MCExpr *Expr, VariantKind Kind,
                                    MCContext &Ctx);

  VariantKind getKind() const { return Kind; }

  const MCExpr *getSubExpr() const { return Expr; }

  /// Get the corresponding PC-relative HI fixup that a VK_RISCZ_PCREL_LO
  /// points to, and optionally the fragment containing it.
  ///
  /// \returns nullptr if this isn't a VK_RISCZ_PCREL_LO pointing to a
  /// known PC-relative HI fixup.
  const MCFixup *getPCRelHiFixup(const MCFragment **DFOut) const;

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override;

  bool evaluateAsConstant(int64_t &Res) const;

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }

  static bool classof(const RISCZMCExpr *) { return true; }

  static VariantKind getVariantKindForName(StringRef name);
  static StringRef getVariantKindName(VariantKind Kind);
};

} // end namespace llvm.

#endif // __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCEXPR_H__