//===----------------------------------------------------------------------===//
// This describes the calling conventions for riscz architecture.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RISCZ Return Value Calling Convention
//===----------------------------------------------------------------------===//

def CSR_ILP32_LP64
    : CalleeSavedRegs<(add X1, X3, X4, X8, X9, (sequence "X%u", 18, 27))>;

// Needed for implementation of RISCVRegisterInfo::getNoPreservedMask()
def CSR_NoRegs : CalleeSavedRegs<(add)>;