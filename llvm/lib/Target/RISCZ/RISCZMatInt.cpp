#include "RISCZMatInt.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

static int getInstSeqCost(RISCZMatInt::InstSeq &Res) {
    return Res.size();
}

// Recursively generate a sequence for materializing an integer.
static void generateInstSeqImpl(int64_t Val,
                                RISCZMatInt::InstSeq &Res) {
  if (isUInt<32>(Val)) {
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    int64_t Hi20 = ((Val + 0x800) >> 12) & 0xFFFFF;
    int64_t Lo12 = SignExtend64<12>(Val);

    if (Hi20)
      Res.push_back(RISCZMatInt::Inst(riscz::LUI, Hi20));

    if (Lo12 || Hi20 == 0) {
      Res.push_back(RISCZMatInt::Inst(riscz::ADDI, Lo12));
    }
    return;
  }

  llvm_unreachable("64 bit values are banned (due to shift ops not implemented), "
                   "find another way, please");
}

namespace llvm {
namespace RISCZMatInt {
InstSeq generateInstSeq(int64_t Val, const FeatureBitset &ActiveFeatures) {
  RISCZMatInt::InstSeq Res;
  generateInstSeqImpl(Val, Res);

  return Res;
}

int getIntMatCost(const APInt &Val, unsigned Size,
                  const FeatureBitset &ActiveFeatures, bool CompressionCost) {
  int PlatRegSize = 32;

  // Split the constant into platform register sized chunks, and calculate cost
  // of each chunk.
  int Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < Size; ShiftVal += PlatRegSize) {
    APInt Chunk = Val.ashr(ShiftVal).sextOrTrunc(PlatRegSize);
    InstSeq MatSeq = generateInstSeq(Chunk.getSExtValue(), ActiveFeatures);
    Cost += getInstSeqCost(MatSeq);
  }
  return std::max(1, Cost);
}
} // namespace RISCZMatInt
} // namespace llvm