#ifndef __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZASMBACKEND_H__
#define __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZASMBACKEND_H__

#include "MCTargetDesc/RISCZFixupKinds.h"
#include "MCTargetDesc/RISCZInfo.h"
#include "MCTargetDesc/RISCZMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {
class MCAssembler;
class MCObjectTargetWriter;
class raw_ostream;
class MCTargetOptions;

class RISCZAsmBackend : public MCAsmBackend {
  const MCSubtargetInfo &STI;
  uint8_t OSABI{};
  bool Is64Bit = true;
  bool ForceRelocs = false;
  const MCTargetOptions &TargetOptions;
  risczABI::ABI TargetABI = risczABI::ABI_LP64;

public:
  RISCZAsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI, bool Is64Bit,
                const MCTargetOptions &Options)
      : MCAsmBackend(endianness::little), STI(STI), OSABI(OSABI), Is64Bit(Is64Bit),
        TargetOptions(Options) {}
  ~RISCZAsmBackend() override {}

  void setForceRelocs() { ForceRelocs = true; }

  // Return Size with extra Nop Bytes for alignment directive in code section.
  bool shouldInsertExtraNopBytesForCodeAlign(const MCAlignFragment &AF,
                                             unsigned &Size) override;

  // Insert target specific fixup type for alignment directive in code section.
  bool shouldInsertFixupForCodeAlign(MCAssembler &Asm,
                                     MCAlignFragment &AF) override;

  bool evaluateTargetFixup(const MCAssembler &Asm,
                           const MCFixup &Fixup, const MCFragment *DF,
                           const MCValue &Target, const MCSubtargetInfo *STI,
                           uint64_t &Value, bool &WasForced) override;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target, const uint64_t Value,
                             const MCSubtargetInfo *STI) override;

  bool fixupNeedsRelaxationAdvanced(const MCAssembler &Asm,
                                    const MCFixup &Fixup, bool Resolved,
                                    uint64_t Value,
                                    const MCRelaxableFragment *DF,
                                    const bool WasForced) const override;

  unsigned getNumFixupKinds() const override {
    return riscz::NumTargetFixupKinds;
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override;
  unsigned getRelaxedOpcode(unsigned Op) const;

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;

  bool relaxDwarfLineAddr(const MCAssembler &Asm, MCDwarfLineAddrFragment &DF,
                          bool &WasRelaxed) const override;
  bool relaxDwarfCFA(const MCAssembler &Asm, MCDwarfCallFrameFragment &DF,
                     bool &WasRelaxed) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  const MCTargetOptions &getTargetOptions() const { return TargetOptions; }
  risczABI::ABI getTargetABI() const { return TargetABI; }
};
} // namespace llvm

#endif // __LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZASMBACKEND_H__