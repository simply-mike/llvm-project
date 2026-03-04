add_llvm_component_group(RISCZ)

add_llvm_target(RISCZCodeGen
  RISCZTargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
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