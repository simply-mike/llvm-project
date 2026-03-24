#ifndef LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCTARGETDESC_H
#define LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCTARGETDESC_H

#include "llvm/MC/MCTargetOptions.h"
#include <memory>

namespace llvm {
class Triple;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class Target;

extern Target TheRISCZTarget;

MCCodeEmitter *createRISCZMCCodeEmitter(const MCInstrInfo &MCII,
                                         MCContext &Ctx);

std::unique_ptr<MCObjectTargetWriter> createRISCZELFObjectWriter(uint8_t OSABI,
                                                                  bool Is64Bit);

MCAsmBackend *createRISCZAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);
} // namespace llvm

#endif // LLVM_LIB_TARGET_RISCZ_MCTARGETDESC_RISCZMCTARGETDESC_H