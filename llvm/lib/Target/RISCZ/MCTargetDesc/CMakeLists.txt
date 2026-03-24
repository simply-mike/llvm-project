add_llvm_component_library(LLVMRISCZDesc
  RISCZMCTargetDesc.cpp
  RISCZInfo.cpp

  RISCZMCAsmInfo.cpp
  RISCZMCExpr.cpp
  RISCZObjectFileInfo.cpp
  RISCZInstPrinter.cpp
  RISCZTargetStreamer.cpp
  RISCZAsmBackend.cpp
  RISCZMCCodeEmitter.cpp
  RISCZElfObjectWriter.cpp
  RISCZElfStreamer.cpp
  
  LINK_COMPONENTS
  RISCZInfo
  MC
  Support

  ADD_TO_COMPONENT
  RISCZ
  )