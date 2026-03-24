add_llvm_component_group(RISCZ)

set(LLVM_TARGET_DEFINITIONS RISCZ.td)

tablegen(LLVM RISCZGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM RISCZGenInstrsInfo.inc -gen-instr-info)
tablegen(LLVM RISCZGenCallingConv.inc -gen-callingconv)
tablegen(LLVM RISCZGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM RISCZGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM RISCZGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM RISCZGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM RISCZGenMCCodeEmitter.inc -gen-emitter)

add_public_tablegen_target(RISCZCommonTableGen)

add_llvm_target(RISCZCodeGen
  RISCZTargetMachine.cpp
  RISCZSubtarget.cpp

  RISCZRegisterInfo.cpp
  RISCZInstrsInfo.cpp

  RISCZISelLowering.cpp
  RISCZFrameLowering.cpp

  RISCZMatInt.cpp
  RISCZISelDagToDag.cpp
  RISCZAsmPrinter.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  GlobalISel
  RISCZInfo
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  RISCZ
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)