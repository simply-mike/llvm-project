add_llvm_component_library(LLVMRISCZDesc
  RISCZMCTargetDesc.cpp

  LINK_COMPONENTS
  RISCZInfo
  MC
  Support

  ADD_TO_COMPONENT
  RISCZ
  )