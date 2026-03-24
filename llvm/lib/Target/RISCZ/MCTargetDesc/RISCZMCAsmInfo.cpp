#include "RISCZMCAsmInfo.h"

using namespace llvm;

void RISCZMCAsmInfo::anchor() {}

RISCZMCAsmInfo::RISCZMCAsmInfo(const Triple &TT) {
  CodePointerSize = CalleeSaveStackSlotSize = 8;
  SupportsDebugInformation = false;
  Data16bitsDirective = "\t.short\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = nullptr; // not supported yet
  ZeroDirective = "\t.space\t";
  CommentString = ";";

  UsesELFSectionDirectiveForBSS = false;
  AllowAtInName = true;
  HiddenVisibilityAttr = MCSA_Invalid;
  HiddenDeclarationVisibilityAttr = MCSA_Invalid;
  ProtectedVisibilityAttr = MCSA_Invalid;

  ExceptionsType = ExceptionHandling::None;
}