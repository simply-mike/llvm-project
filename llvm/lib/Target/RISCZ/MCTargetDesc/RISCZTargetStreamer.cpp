#include "RISCZInfo.h"
#include "RISCZTargetStreamer.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/RISCZAttributes.h"
#include "llvm/Support/RISCZISAInfo.h"

using namespace llvm;

RISCZTargetStreamer::RISCZTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

void RISCZTargetStreamer::finish() { finishAttributeSection(); }

void RISCZTargetStreamer::emitDirectiveOptionPush() {}
void RISCZTargetStreamer::emitDirectiveOptionPop() {}
void RISCZTargetStreamer::emitDirectiveOptionPIC() {}
void RISCZTargetStreamer::emitDirectiveOptionNoPIC() {}
void RISCZTargetStreamer::emitDirectiveOptionRelax() {}
void RISCZTargetStreamer::emitDirectiveOptionNoRelax() {}
void RISCZTargetStreamer::emitAttribute(unsigned Attribute, unsigned Value) {}
void RISCZTargetStreamer::finishAttributeSection() {}
void RISCZTargetStreamer::emitTextAttribute(unsigned Attribute,
                                             StringRef String) {}
void RISCZTargetStreamer::emitIntTextAttribute(unsigned Attribute,
                                                unsigned IntValue,
                                                StringRef StringValue) {}

void RISCZTargetStreamer::emitTargetAttributes(const MCSubtargetInfo &STI) {
  emitAttribute(RISCZAttrs::STACK_ALIGN, RISCZAttrs::ALIGN_16);

  unsigned XLen = 64;
  std::vector<std::string> FeatureVector;
  risczFeatures::toFeatureVector(FeatureVector, STI.getFeatureBits());

  auto ParseResult = llvm::RISCZISAInfo::parseFeatures(XLen, FeatureVector);
  if (!ParseResult) {
    /* Assume any error about features should handled earlier.  */
    consumeError(ParseResult.takeError());
    llvm_unreachable("Parsing feature error when emitTargetAttributes?");
  } else {
    auto &ISAInfo = *ParseResult;
    emitTextAttribute(RISCZAttrs::ARCH, ISAInfo->toString());
  }
}

// This part is for ascii assembly output
RISCZTargetAsmStreamer::RISCZTargetAsmStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS)
    : RISCZTargetStreamer(S), OS(OS) {}

void RISCZTargetAsmStreamer::emitDirectiveOptionPush() {
  OS << "\t.option\tpush\n";
}

void RISCZTargetAsmStreamer::emitDirectiveOptionPop() {
  OS << "\t.option\tpop\n";
}

void RISCZTargetAsmStreamer::emitDirectiveOptionPIC() {
  OS << "\t.option\tpic\n";
}

void RISCZTargetAsmStreamer::emitDirectiveOptionNoPIC() {
  OS << "\t.option\tnopic\n";
}

void RISCZTargetAsmStreamer::emitDirectiveOptionRelax() {
  OS << "\t.option\trelax\n";
}

void RISCZTargetAsmStreamer::emitDirectiveOptionNoRelax() {
  OS << "\t.option\tnorelax\n";
}

void RISCZTargetAsmStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  OS << "\t.attribute\t" << Attribute << ", " << Twine(Value) << "\n";
}

void RISCZTargetAsmStreamer::emitTextAttribute(unsigned Attribute,
                                                StringRef String) {
  OS << "\t.attribute\t" << Attribute << ", \"" << String << "\"\n";
}

void RISCZTargetAsmStreamer::emitIntTextAttribute(unsigned Attribute,
                                                   unsigned IntValue,
                                                   StringRef StringValue) {}

void RISCZTargetAsmStreamer::finishAttributeSection() {}