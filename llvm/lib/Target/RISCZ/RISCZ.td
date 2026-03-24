//===----------------------------------------------------------------------===//
// This is the top level entry point for the RISCZ target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// RISCZ Subtarget features
//===----------------------------------------------------------------------===//

defvar GRV64 = DefaultMode;

//===----------------------------------------------------------------------===//
// Target-dependent interfaces
//===----------------------------------------------------------------------===//

include "RISCZRegisterInfo.td"

//===----------------------------------------------------------------------===//
// Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "RISCZSchedule.td"
include "RISCZCallingConv.td"

include "RISCZInstrsInfo.td"
def RISCZInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// RISCZ processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", []>;

def RISCZAsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
  let AllowDuplicateRegisterNames = 1;
}

def RISCZ : Target {
  let InstructionSet = RISCZInstrInfo;
}