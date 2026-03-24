#include "llvm/Support/RISCZISAInfo.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace {

struct RISCZSupportedExtension {
  const char *Name;
  /// Supported version.
  RISCZISAInfo::ExtensionVersion Version;

  bool operator<(const RISCZSupportedExtension &RHS) const {
    return StringRef(Name) < StringRef(RHS.Name);
  }
};

} // end anonymous namespace

static constexpr StringLiteral AllStdExts = "mad";

// NOTE: This table should be sorted alphabetically by extension name.
static const RISCZSupportedExtension SupportedExtensions[] = {
    {"a", {2, 1}},
    {"d", {2, 2}},
    {"i", {2, 1}},
    {"m", {2, 0}},
};

static void verifyTables() {
#ifndef NDEBUG
  static std::atomic<bool> TableChecked(false);
  if (!TableChecked.load(std::memory_order_relaxed)) {
    assert(llvm::is_sorted(SupportedExtensions) &&
           "Extensions are not sorted by name");
    TableChecked.store(true, std::memory_order_relaxed);
  }
#endif
}

namespace {
struct LessExtName {
  bool operator()(const RISCZSupportedExtension &LHS, StringRef RHS) {
    return StringRef(LHS.Name) < RHS;
  }
  bool operator()(StringRef LHS, const RISCZSupportedExtension &RHS) {
    return LHS < StringRef(RHS.Name);
  }
};
} // namespace

static std::optional<RISCZISAInfo::ExtensionVersion>
findDefaultVersion(StringRef ExtName) {
  // Find default version of an extension.
  // TODO: We might set default version based on profile or ISA spec.
  for (auto &ExtInfo : {ArrayRef(SupportedExtensions)}) {
    auto I = llvm::lower_bound(ExtInfo, ExtName, LessExtName());

    if (I == ExtInfo.end() || I->Name != ExtName)
      continue;

    return I->Version;
  }
  return std::nullopt;
}

void RISCZISAInfo::addExtension(StringRef ExtName,
                                 RISCZISAInfo::ExtensionVersion Version) {
  Exts[ExtName.str()] = Version;
}

static StringRef getExtensionTypeDesc(StringRef Ext) {
  if (Ext.starts_with("s"))
    return "standard supervisor-level extension";
  if (Ext.starts_with("x"))
    return "non-standard user-level extension";
  if (Ext.starts_with("z"))
    return "standard user-level extension";
  return StringRef();
}

bool RISCZISAInfo::isSupportedExtensionFeature(StringRef Ext) {
  ArrayRef<RISCZSupportedExtension> ExtInfo =
      ArrayRef(SupportedExtensions);

  auto I = llvm::lower_bound(ExtInfo, Ext, LessExtName());
  return I != ExtInfo.end() && I->Name == Ext;
}

bool RISCZISAInfo::isSupportedExtension(StringRef Ext) {
  verifyTables();

  for (auto ExtInfo : {ArrayRef(SupportedExtensions)}) {
    auto I = llvm::lower_bound(ExtInfo, Ext, LessExtName());
    if (I != ExtInfo.end() && I->Name == Ext)
      return true;
  }

  return false;
}

bool RISCZISAInfo::isSupportedExtension(StringRef Ext, unsigned MajorVersion,
                                         unsigned MinorVersion) {
  for (auto ExtInfo : {ArrayRef(SupportedExtensions)}) {
    auto Range =
        std::equal_range(ExtInfo.begin(), ExtInfo.end(), Ext, LessExtName());
    for (auto I = Range.first, E = Range.second; I != E; ++I)
      if (I->Version.Major == MajorVersion && I->Version.Minor == MinorVersion)
        return true;
  }

  return false;
}

bool RISCZISAInfo::hasExtension(StringRef Ext) const {
  if (!isSupportedExtension(Ext))
    return false;

  return Exts.count(Ext.str()) != 0;
}

// We rank extensions in the following order:
// -Single letter extensions in canonical order.
// -Unknown single letter extensions in alphabetical order.
// -Multi-letter extensions starting with 'z' sorted by canonical order of
//  the second letter then sorted alphabetically.
// -Multi-letter extensions starting with 's' in alphabetical order.
// -(TODO) Multi-letter extensions starting with 'zxm' in alphabetical order.
// -X extensions in alphabetical order.
// These flags are used to indicate the category. The first 6 bits store the
// single letter extension rank for single letter and multi-letter extensions
// starting with 'z'.
enum RankFlags {
  RF_Z_EXTENSION = 1 << 6,
  RF_S_EXTENSION = 1 << 7,
  RF_X_EXTENSION = 1 << 8,
};

// Get the rank for single-letter extension, lower value meaning higher
// priority.
static unsigned singleLetterExtensionRank(char Ext) {
  assert(Ext >= 'a' && Ext <= 'z');
  switch (Ext) {
  case 'i':
    return 0;
  case 'e':
    return 1;
  }

  size_t Pos = AllStdExts.find(Ext);
  if (Pos != StringRef::npos)
    return Pos + 2; // Skip 'e' and 'i' from above.

  // If we got an unknown extension letter, then give it an alphabetical
  // order, but after all known standard extensions.
  return 2 + AllStdExts.size() + (Ext - 'a');
}

// Get the rank for multi-letter extension, lower value meaning higher
// priority/order in canonical order.
static unsigned getExtensionRank(const std::string &ExtName) {
  assert(ExtName.size() >= 1);
  switch (ExtName[0]) {
  case 's':
    return RF_S_EXTENSION;
  case 'z':
    assert(ExtName.size() >= 2);
    // `z` extension must be sorted by canonical order of second letter.
    // e.g. zmx has higher rank than zax.
    return RF_Z_EXTENSION | singleLetterExtensionRank(ExtName[1]);
  case 'x':
    return RF_X_EXTENSION;
  default:
    assert(ExtName.size() == 1);
    return singleLetterExtensionRank(ExtName[0]);
  }
}

// Compare function for extension.
// Only compare the extension name, ignore version comparison.
bool RISCZISAInfo::compareExtension(const std::string &LHS,
                                     const std::string &RHS) {
  unsigned LHSRank = getExtensionRank(LHS);
  unsigned RHSRank = getExtensionRank(RHS);

  // If the ranks differ, pick the lower rank.
  if (LHSRank != RHSRank)
    return LHSRank < RHSRank;

  // If the rank is same, it must be sorted by lexicographic order.
  return LHS < RHS;
}

std::vector<std::string> RISCZISAInfo::toFeatures(bool AddAllExtensions,
                                                   bool IgnoreUnknown) const {
  std::vector<std::string> Features;
  for (const auto &[ExtName, _] : Exts) {
    // i is a base instruction set, not an extension (see
    // https://github.com/riscv/riscv-isa-manual/blob/main/src/naming.adoc#base-integer-isa)
    // and is not recognized in clang -cc1
    if (ExtName == "i")
      continue;
    if (IgnoreUnknown && !isSupportedExtension(ExtName))
      continue;

    Features.push_back((llvm::Twine("+") + ExtName).str());
  }
  if (AddAllExtensions) {
    for (const RISCZSupportedExtension &Ext : SupportedExtensions) {
      if (Exts.count(Ext.Name))
        continue;
      Features.push_back((llvm::Twine("-") + Ext.Name).str());
    }
  }
  return Features;
}

// Extensions may have a version number, and may be separated by
// an underscore '_' e.g.: rv32i2_m2.
// Version number is divided into major and minor version numbers,
// separated by a 'p'. If the minor version is 0 then 'p0' can be
// omitted from the version string. E.g., rv32i2p0, rv32i2, rv32i2p1.
static Error getExtensionVersion(StringRef Ext, StringRef In, unsigned &Major,
                                 unsigned &Minor, unsigned &ConsumeLength) {
  StringRef MajorStr, MinorStr;
  Major = 0;
  Minor = 0;
  ConsumeLength = 0;
  MajorStr = In.take_while(isDigit);
  In = In.substr(MajorStr.size());

  if (!MajorStr.empty() && In.consume_front("p")) {
    MinorStr = In.take_while(isDigit);
    In = In.substr(MajorStr.size() + MinorStr.size() - 1);

    // Expected 'p' to be followed by minor version number.
    if (MinorStr.empty()) {
      return createStringError(
          errc::invalid_argument,
          "minor version number missing after 'p' for extension '" + Ext + "'");
    }
  }

  if (!MajorStr.empty() && MajorStr.getAsInteger(10, Major))
    return createStringError(
        errc::invalid_argument,
        "Failed to parse major version number for extension '" + Ext + "'");

  if (!MinorStr.empty() && MinorStr.getAsInteger(10, Minor))
    return createStringError(
        errc::invalid_argument,
        "Failed to parse minor version number for extension '" + Ext + "'");

  ConsumeLength = MajorStr.size();

  if (!MinorStr.empty())
    ConsumeLength += MinorStr.size() + 1 /*'p'*/;

  // Expected multi-character extension with version number to have no
  // subsequent characters (i.e. must either end string or be followed by
  // an underscore).
  if (Ext.size() > 1 && In.size())
    return createStringError(
        errc::invalid_argument,
        "multi-character extensions must be separated by underscores");

  if (MajorStr.empty() && MinorStr.empty()) {
    if (auto DefaultVersion = findDefaultVersion(Ext)) {
      Major = DefaultVersion->Major;
      Minor = DefaultVersion->Minor;
    }
    // No matter found or not, return success, assume other place will
    // verify.
    return Error::success();
  }

  if (RISCZISAInfo::isSupportedExtension(Ext, Major, Minor))
    return Error::success();

  std::string Error = "unsupported version number " + std::string(MajorStr);
  if (!MinorStr.empty())
    Error += "." + MinorStr.str();
  Error += " for extension '" + Ext.str() + "'";
  return createStringError(errc::invalid_argument, Error);
}

llvm::Expected<std::unique_ptr<RISCZISAInfo>>
RISCZISAInfo::parseFeatures(unsigned XLen,
                             const std::vector<std::string> &Features) {
  assert(XLen == 64);
  std::unique_ptr<RISCZISAInfo> ISAInfo(new RISCZISAInfo(XLen));

  for (auto &Feature : Features) {
    StringRef ExtName = Feature;
    assert(ExtName.size() > 1 && (ExtName[0] == '+' || ExtName[0] == '-'));
    bool Add = ExtName[0] == '+';
    ExtName = ExtName.drop_front(1); // Drop '+' or '-'
    auto ExtensionInfos = ArrayRef(SupportedExtensions);
    auto ExtensionInfoIterator =
        llvm::lower_bound(ExtensionInfos, ExtName, LessExtName());

    // Not all features is related to ISA extension, like `relax` or
    // `save-restore`, skip those feature.
    if (ExtensionInfoIterator == ExtensionInfos.end() ||
        ExtensionInfoIterator->Name != ExtName)
      continue;

    if (Add)
      ISAInfo->addExtension(ExtName, ExtensionInfoIterator->Version);
    else
      ISAInfo->Exts.erase(ExtName.str());
  }

  return RISCZISAInfo::postProcessAndChecking(std::move(ISAInfo));
}

llvm::Expected<std::unique_ptr<RISCZISAInfo>>
RISCZISAInfo::parseNormalizedArchString(StringRef Arch) {
  if (llvm::any_of(Arch, isupper)) {
    return createStringError(errc::invalid_argument,
                             "string must be lowercase");
  }
  // Must start with a valid base ISA name.
  unsigned XLen;
  if (Arch.starts_with("rv64i") )
    XLen = 64;
  else
    return createStringError(errc::invalid_argument,
                             "arch string must begin with valid base ISA");
  std::unique_ptr<RISCZISAInfo> ISAInfo(new RISCZISAInfo(XLen));
  // Discard rv64 prefix.
  Arch = Arch.substr(4);

  // Each extension is of the form ${name}${major_version}p${minor_version}
  // and separated by _. Split by _ and then extract the name and version
  // information for each extension.
  SmallVector<StringRef, 8> Split;
  Arch.split(Split, '_');
  for (StringRef Ext : Split) {
    StringRef Prefix, MinorVersionStr;
    std::tie(Prefix, MinorVersionStr) = Ext.rsplit('p');
    if (MinorVersionStr.empty())
      return createStringError(errc::invalid_argument,
                               "extension lacks version in expected format");
    unsigned MajorVersion, MinorVersion;
    if (MinorVersionStr.getAsInteger(10, MinorVersion))
      return createStringError(errc::invalid_argument,
                               "failed to parse minor version number");

    // Split Prefix into the extension name and the major version number
    // (the trailing digits of Prefix).
    int TrailingDigits = 0;
    StringRef ExtName = Prefix;
    while (!ExtName.empty()) {
      if (!isDigit(ExtName.back()))
        break;
      ExtName = ExtName.drop_back(1);
      TrailingDigits++;
    }
    if (!TrailingDigits)
      return createStringError(errc::invalid_argument,
                               "extension lacks version in expected format");

    StringRef MajorVersionStr = Prefix.take_back(TrailingDigits);
    if (MajorVersionStr.getAsInteger(10, MajorVersion))
      return createStringError(errc::invalid_argument,
                               "failed to parse major version number");
    ISAInfo->addExtension(ExtName, {MajorVersion, MinorVersion});
  }
  ISAInfo->updateMinVLen();
  return std::move(ISAInfo);
}

static Error splitExtsByUnderscore(StringRef Exts,
                                   std::vector<std::string> &SplitExts) {
  SmallVector<StringRef, 8> Split;
  if (Exts.empty())
    return Error::success();

  Exts.split(Split, "_");

  for (auto Ext : Split) {
    if (Ext.empty())
      return createStringError(errc::invalid_argument,
                               "extension name missing after separator '_'");

    SplitExts.push_back(Ext.str());
  }
  return Error::success();
}

static Error processSingleLetterExtension(
    StringRef &RawExt,
    MapVector<std::string, RISCZISAInfo::ExtensionVersion,
              std::map<std::string, unsigned>> &SeenExtMap,
    bool IgnoreUnknown) {
  unsigned Major, Minor, ConsumeLength;
  StringRef Name = RawExt.take_front(1);
  RawExt.consume_front(Name);
  if (auto E = getExtensionVersion(Name, RawExt, Major, Minor, ConsumeLength)) {
    if (IgnoreUnknown) {
      consumeError(std::move(E));
      RawExt = RawExt.substr(ConsumeLength);
      return Error::success();
    }
    return E;
  }

  RawExt = RawExt.substr(ConsumeLength);

  // Check if duplicated extension.
  if (!IgnoreUnknown && SeenExtMap.contains(Name.str()))
    return createStringError(errc::invalid_argument,
                             "duplicated standard user-level extension '" +
                                 Name + "'");

  if (IgnoreUnknown && !RISCZISAInfo::isSupportedExtension(Name))
    return Error::success();

  SeenExtMap[Name.str()] = {Major, Minor};
  return Error::success();
}

llvm::Expected<std::unique_ptr<RISCZISAInfo>>
RISCZISAInfo::parseArchString(StringRef Arch, bool IgnoreUnknown) {
  // RISC-V ISA strings must be lowercase.
  if (llvm::any_of(Arch, isupper)) {
    return createStringError(errc::invalid_argument,
                             "string must be lowercase");
  }

  bool HasRV64 = Arch.starts_with("rv64");
  // ISA string must begin with rv32 or rv64.
  if (!(Arch.starts_with("rv32") || HasRV64) || (Arch.size() < 5)) {
    return createStringError(
        errc::invalid_argument,
        "string must begin with rv64{i,e,g}");
  }

  unsigned XLen = 64;
  std::unique_ptr<RISCZISAInfo> ISAInfo(new RISCZISAInfo(XLen));
  MapVector<std::string, RISCZISAInfo::ExtensionVersion,
            std::map<std::string, unsigned>>
      SeenExtMap;

  char Baseline = Arch[4];

  // First letter should be 'i'
  switch (Baseline) {
  default:
    return createStringError(errc::invalid_argument,
                             "first letter should be 'i'");
  case 'i':
    break;
  }

  if (Arch.back() == '_')
    return createStringError(errc::invalid_argument,
                             "extension name missing after separator '_'");

  // Skip rvxxx
  StringRef Exts = Arch.substr(5);

  unsigned Major, Minor, ConsumeLength;

  // Baseline is `i`
  if (auto E = getExtensionVersion(
          StringRef(&Baseline, 1), Exts, Major, Minor, ConsumeLength)) {
    if (!IgnoreUnknown)
      return std::move(E);
    // If IgnoreUnknown, then ignore an unrecognised version of the baseline
    // ISA and just use the default supported version.
    consumeError(std::move(E));
    auto Version = findDefaultVersion(StringRef(&Baseline, 1));
    Major = Version->Major;
    Minor = Version->Minor;
  }

  // Postpone AddExtension until end of this function
  SeenExtMap[StringRef(&Baseline, 1).str()] = {Major, Minor};

  // Consume the base ISA version number and any '_' between rvxxx and the
  // first extension
  Exts = Exts.drop_front(ConsumeLength);
  Exts.consume_front("_");

  std::vector<std::string> SplittedExts;
  if (auto E = splitExtsByUnderscore(Exts, SplittedExts))
    return std::move(E);

  for (auto &Ext : SplittedExts) {
    StringRef CurrExt = Ext;
    while (!CurrExt.empty()) {
      if (AllStdExts.contains(CurrExt.front())) {
        if (auto E = processSingleLetterExtension(
                CurrExt, SeenExtMap, IgnoreUnknown))
          return std::move(E);
      } else {
        // FIXME: Could it be ignored by IgnoreUnknown?
        return createStringError(errc::invalid_argument,
                                 "invalid standard user-level extension '" +
                                     Twine(CurrExt.front()) + "'");
      }
    }
  }

  // Check all Extensions are supported.
  for (auto &SeenExtAndVers : SeenExtMap) {
    const std::string &ExtName = SeenExtAndVers.first;
    RISCZISAInfo::ExtensionVersion ExtVers = SeenExtAndVers.second;

    if (!RISCZISAInfo::isSupportedExtension(ExtName)) {
      if (ExtName.size() == 1) {
        return createStringError(errc::invalid_argument,
                                 "unsupported standard user-level extension '" +
                                     ExtName + "'");
      }
      return createStringError(errc::invalid_argument,
                               "unsupported " + getExtensionTypeDesc(ExtName) +
                                   " '" + ExtName + "'");
    }
    ISAInfo->addExtension(ExtName, ExtVers);
  }

  return RISCZISAInfo::postProcessAndChecking(std::move(ISAInfo));
}

Error RISCZISAInfo::checkDependency() {
  return Error::success();
}

struct ImpliedExtsEntry {
  StringLiteral Name;
  ArrayRef<const char *> Exts;

  bool operator<(const ImpliedExtsEntry &Other) const {
    return Name < Other.Name;
  }

  bool operator<(StringRef Other) const { return Name < Other; }
};

// Note: The table needs to be sorted by name.
static ImpliedExtsEntry ImpliedExts[] = {
    {{"d"}},
};

void RISCZISAInfo::updateImplication() {
  bool HasI = Exts.count("i") != 0;

  if (!HasI) {
    auto Version = findDefaultVersion("i");
    addExtension("i", Version.value());
  }

  assert(llvm::is_sorted(ImpliedExts) && "Table not sorted by Name");

  // This loop may execute over 1 iteration since implication can be layered
  // Exits loop if no more implication is applied
  SmallSetVector<StringRef, 16> WorkList;
  for (auto const &Ext : Exts)
    WorkList.insert(Ext.first);

  while (!WorkList.empty()) {
    StringRef ExtName = WorkList.pop_back_val();
    auto I = llvm::lower_bound(ImpliedExts, ExtName);
    if (I != std::end(ImpliedExts) && I->Name == ExtName) {
      for (const char *ImpliedExt : I->Exts) {
        if (WorkList.count(ImpliedExt))
          continue;
        if (Exts.count(ImpliedExt))
          continue;
        auto Version = findDefaultVersion(ImpliedExt);
        addExtension(ImpliedExt, Version.value());
        WorkList.insert(ImpliedExt);
      }
    }
  }
}

struct CombinedExtsEntry {
  StringLiteral CombineExt;
  ArrayRef<const char *> RequiredExts;
};

void RISCZISAInfo::updateMinVLen() {
  for (auto const &Ext : Exts) {
    StringRef ExtName = Ext.first;
    bool IsZvlExt = ExtName.consume_front("zvl") && ExtName.consume_back("b");
    if (IsZvlExt) {
      unsigned ZvlLen;
      if (!ExtName.getAsInteger(10, ZvlLen))
        MinVLen = std::max(MinVLen, ZvlLen);
    }
  }
}

std::string RISCZISAInfo::toString() const {
  std::string Buffer;
  raw_string_ostream Arch(Buffer);

  Arch << "rv" << XLen;

  ListSeparator LS("_");
  for (auto const &Ext : Exts) {
    StringRef ExtName = Ext.first;
    auto ExtInfo = Ext.second;
    Arch << LS << ExtName;
    Arch << ExtInfo.Major << "p" << ExtInfo.Minor;
  }

  return Arch.str();
}

llvm::Expected<std::unique_ptr<RISCZISAInfo>>
RISCZISAInfo::postProcessAndChecking(std::unique_ptr<RISCZISAInfo> &&ISAInfo) {
  ISAInfo->updateImplication();
  ISAInfo->updateMinVLen();

  if (Error Result = ISAInfo->checkDependency())
    return std::move(Result);
  return std::move(ISAInfo);
}

StringRef RISCZISAInfo::computeDefaultABI() const {
  if (XLen == 64) {
    if (hasExtension("d"))
      return "lp64d";
    return "lp64"; // default case (other are not implemented)
  }
  llvm_unreachable("Invalid XLEN");
}