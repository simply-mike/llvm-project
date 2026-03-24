#include "RISCZObjectFileInfo.h"

using namespace llvm;

unsigned RISCZMCObjectFileInfo::getTextSectionAlignment() const {
  return 4;
}