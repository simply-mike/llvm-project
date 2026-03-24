#pragma once

#include "llvm/Support/ELFAttributes.h"

namespace llvm {
namespace RISCZAttrs {

const TagNameMap &getRISCZAttributeTags();

enum AttrType : unsigned {
  // Attribute types in ELF/.RISCZ.attributes.
  STACK_ALIGN = 4,
  ARCH = 5,
  UNALIGNED_ACCESS = 6,
  PRIV_SPEC = 8,
  PRIV_SPEC_MINOR = 10,
  PRIV_SPEC_REVISION = 12,
};

enum StackAlign { ALIGN_4 = 4, ALIGN_16 = 16 };

enum { NOT_ALLOWED = 0, ALLOWED = 1 };

} // namespace RISCZAttrs
} // namespace llvm