//===-- DWARFContext.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
using namespace llvm;
using namespace dwarf;

typedef DWARFDebugLine::LineTable DWARFLineTable;

void DWARFContext::dump(raw_ostream &OS) {
  OS << ".debug_abbrev contents:\n";
  getDebugAbbrev()->dump(OS);

  OS << "\n.debug_info contents:\n";
  for (unsigned i = 0, e = getNumCompileUnits(); i != e; ++i)
    getCompileUnitAtIndex(i)->dump(OS);

  OS << "\n.debug_aranges contents:\n";
  DataExtractor arangesData(getARangeSection(), isLittleEndian(), 0);
  uint32_t offset = 0;
  DWARFDebugArangeSet set;
  while (set.extract(arangesData, &offset))
    set.dump(OS);

  uint8_t savedAddressByteSize = 0;
  OS << "\n.debug_line contents:\n";
  for (unsigned i = 0, e = getNumCompileUnits(); i != e; ++i) {
    DWARFCompileUnit *cu = getCompileUnitAtIndex(i);
    savedAddressByteSize = cu->getAddressByteSize();
    unsigned stmtOffset =
      cu->getCompileUnitDIE()->getAttributeValueAsUnsigned(cu, DW_AT_stmt_list,
                                                           -1U);
    if (stmtOffset != -1U) {
      DataExtractor lineData(getLineSection(), isLittleEndian(),
                             savedAddressByteSize);
      DWARFDebugLine::DumpingState state(OS);
      DWARFDebugLine::parseStatementTable(lineData, &stmtOffset, state);
    }
  }

  OS << "\n.debug_str contents:\n";
  DataExtractor strData(getStringSection(), isLittleEndian(), 0);
  offset = 0;
  uint32_t strOffset = 0;
  while (const char *s = strData.getCStr(&offset)) {
    OS << format("0x%8.8x: \"%s\"\n", strOffset, s);
    strOffset = offset;
  }

  OS << "\n.debug_ranges contents:\n";
  // In fact, different compile units may have different address byte
  // sizes, but for simplicity we just use the address byte size of the last
  // compile unit (there is no easy and fast way to associate address range
  // list and the compile unit it describes).
  DataExtractor rangesData(getRangeSection(), isLittleEndian(),
                           savedAddressByteSize);
  offset = 0;
  DWARFDebugRangeList rangeList;
  while (rangeList.extract(rangesData, &offset))
    rangeList.dump(OS);

  OS << "\n.debug_abbrev.dwo contents:\n";
  getDebugAbbrevDWO()->dump(OS);

  OS << "\n.debug_info.dwo contents:\n";
  for (unsigned i = 0, e = getNumDWOCompileUnits(); i != e; ++i)
    getDWOCompileUnitAtIndex(i)->dump(OS);

  OS << "\n.debug_str.dwo contents:\n";
  DataExtractor strDWOData(getStringDWOSection(), isLittleEndian(), 0);
  offset = 0;
  uint32_t strDWOOffset = 0;
  while (const char *s = strDWOData.getCStr(&offset)) {
    OS << format("0x%8.8x: \"%s\"\n", strDWOOffset, s);
    strDWOOffset = offset;
  }
}

const DWARFDebugAbbrev *DWARFContext::getDebugAbbrev() {
  if (Abbrev)
    return Abbrev.get();

  DataExtractor abbrData(getAbbrevSection(), isLittleEndian(), 0);

  Abbrev.reset(new DWARFDebugAbbrev());
  Abbrev->parse(abbrData);
  return Abbrev.get();
}

const DWARFDebugAbbrev *DWARFContext::getDebugAbbrevDWO() {
  if (AbbrevDWO)
    return AbbrevDWO.get();

  DataExtractor abbrData(getAbbrevDWOSection(), isLittleEndian(), 0);
  AbbrevDWO.reset(new DWARFDebugAbbrev());
  AbbrevDWO->parse(abbrData);
  return AbbrevDWO.get();
}

const DWARFDebugAranges *DWARFContext::getDebugAranges() {
  if (Aranges)
    return Aranges.get();

  DataExtractor arangesData(getARangeSection(), isLittleEndian(), 0);

  Aranges.reset(new DWARFDebugAranges());
  Aranges->extract(arangesData);
  // Generate aranges from DIEs: even if .debug_aranges section is present,
  // it may describe only a small subset of compilation units, so we need to
  // manually build aranges for the rest of them.
  Aranges->generate(this);
  return Aranges.get();
}

const DWARFLineTable *
DWARFContext::getLineTableForCompileUnit(DWARFCompileUnit *cu) {
  if (!Line)
    Line.reset(new DWARFDebugLine());

  unsigned stmtOffset =
    cu->getCompileUnitDIE()->getAttributeValueAsUnsigned(cu, DW_AT_stmt_list,
                                                         -1U);
  if (stmtOffset == -1U)
    return 0; // No line table for this compile unit.

  // See if the line table is cached.
  if (const DWARFLineTable *lt = Line->getLineTable(stmtOffset))
    return lt;

  // We have to parse it first.
  DataExtractor lineData(getLineSection(), isLittleEndian(),
                         cu->getAddressByteSize());
  return Line->getOrParseLineTable(lineData, stmtOffset);
}

void DWARFContext::parseCompileUnits() {
  uint32_t offset = 0;
  const DataExtractor &DIData = DataExtractor(getInfoSection(),
                                              isLittleEndian(), 0);
  while (DIData.isValidOffset(offset)) {
    CUs.push_back(DWARFCompileUnit(getDebugAbbrev(), getInfoSection(),
                                   getAbbrevSection(), getRangeSection(),
                                   getStringSection(), &infoRelocMap(),
                                   isLittleEndian()));
    if (!CUs.back().extract(DIData, &offset)) {
      CUs.pop_back();
      break;
    }

    offset = CUs.back().getNextCompileUnitOffset();
  }
}

void DWARFContext::parseDWOCompileUnits() {
  uint32_t offset = 0;
  const DataExtractor &DIData = DataExtractor(getInfoDWOSection(),
                                              isLittleEndian(), 0);
  while (DIData.isValidOffset(offset)) {
    DWOCUs.push_back(DWARFCompileUnit(getDebugAbbrevDWO(), getInfoDWOSection(),
                                      getAbbrevDWOSection(),
                                      getRangeDWOSection(),
                                      getStringDWOSection(),
                                      &infoDWORelocMap(),
                                      isLittleEndian()));
    if (!DWOCUs.back().extract(DIData, &offset)) {
      DWOCUs.pop_back();
      break;
    }

    offset = DWOCUs.back().getNextCompileUnitOffset();
  }
}

namespace {
  struct OffsetComparator {
    bool operator()(const DWARFCompileUnit &LHS,
                    const DWARFCompileUnit &RHS) const {
      return LHS.getOffset() < RHS.getOffset();
    }
    bool operator()(const DWARFCompileUnit &LHS, uint32_t RHS) const {
      return LHS.getOffset() < RHS;
    }
    bool operator()(uint32_t LHS, const DWARFCompileUnit &RHS) const {
      return LHS < RHS.getOffset();
    }
  };
}

DWARFCompileUnit *DWARFContext::getCompileUnitForOffset(uint32_t Offset) {
  if (CUs.empty())
    parseCompileUnits();

  DWARFCompileUnit *CU = std::lower_bound(CUs.begin(), CUs.end(), Offset,
                                          OffsetComparator());
  if (CU != CUs.end())
    return &*CU;
  return 0;
}

DWARFCompileUnit *DWARFContext::getCompileUnitForAddress(uint64_t Address) {
  // First, get the offset of the compile unit.
  uint32_t CUOffset = getDebugAranges()->findAddress(Address);
  // Retrieve the compile unit.
  return getCompileUnitForOffset(CUOffset);
}

static bool getFileNameForCompileUnit(DWARFCompileUnit *CU,
                                      const DWARFLineTable *LineTable,
                                      uint64_t FileIndex,
                                      bool NeedsAbsoluteFilePath,
                                      std::string &FileName) {
  if (CU == 0 ||
      LineTable == 0 ||
      !LineTable->getFileNameByIndex(FileIndex, NeedsAbsoluteFilePath,
                                     FileName))
    return false;
  if (NeedsAbsoluteFilePath && sys::path::is_relative(FileName)) {
    // We may still need to append compilation directory of compile unit.
    SmallString<16> AbsolutePath;
    if (const char *CompilationDir = CU->getCompilationDir()) {
      sys::path::append(AbsolutePath, CompilationDir);
    }
    sys::path::append(AbsolutePath, FileName);
    FileName = AbsolutePath.str();
  }
  return true;
}

static bool getFileLineInfoForCompileUnit(DWARFCompileUnit *CU,
                                          const DWARFLineTable *LineTable,
                                          uint64_t Address,
                                          bool NeedsAbsoluteFilePath,
                                          std::string &FileName,
                                          uint32_t &Line, uint32_t &Column) {
  if (CU == 0 || LineTable == 0)
    return false;
  // Get the index of row we're looking for in the line table.
  uint32_t RowIndex = LineTable->lookupAddress(Address);
  if (RowIndex == -1U)
    return false;
  // Take file number and line/column from the row.
  const DWARFDebugLine::Row &Row = LineTable->Rows[RowIndex];
  if (!getFileNameForCompileUnit(CU, LineTable, Row.File,
                                 NeedsAbsoluteFilePath, FileName))
    return false;
  Line = Row.Line;
  Column = Row.Column;
  return true;
}

DILineInfo DWARFContext::getLineInfoForAddress(uint64_t Address,
    DILineInfoSpecifier Specifier) {
  DWARFCompileUnit *CU = getCompileUnitForAddress(Address);
  if (!CU)
    return DILineInfo();
  std::string FileName = "<invalid>";
  std::string FunctionName = "<invalid>";
  uint32_t Line = 0;
  uint32_t Column = 0;
  if (Specifier.needs(DILineInfoSpecifier::FunctionName)) {
    // The address may correspond to instruction in some inlined function,
    // so we have to build the chain of inlined functions and take the
    // name of the topmost function in it.
    const DWARFDebugInfoEntryMinimal::InlinedChain &InlinedChain =
        CU->getInlinedChainForAddress(Address);
    if (InlinedChain.size() > 0) {
      const DWARFDebugInfoEntryMinimal &TopFunctionDIE = InlinedChain[0];
      if (const char *Name = TopFunctionDIE.getSubroutineName(CU))
        FunctionName = Name;
    }
  }
  if (Specifier.needs(DILineInfoSpecifier::FileLineInfo)) {
    const DWARFLineTable *LineTable = getLineTableForCompileUnit(CU);
    const bool NeedsAbsoluteFilePath =
        Specifier.needs(DILineInfoSpecifier::AbsoluteFilePath);
    getFileLineInfoForCompileUnit(CU, LineTable, Address,
                                  NeedsAbsoluteFilePath,
                                  FileName, Line, Column);
  }
  return DILineInfo(StringRef(FileName), StringRef(FunctionName),
                    Line, Column);
}

DIInliningInfo DWARFContext::getInliningInfoForAddress(uint64_t Address,
    DILineInfoSpecifier Specifier) {
  DWARFCompileUnit *CU = getCompileUnitForAddress(Address);
  if (!CU)
    return DIInliningInfo();

  const DWARFDebugInfoEntryMinimal::InlinedChain &InlinedChain =
      CU->getInlinedChainForAddress(Address);
  if (InlinedChain.size() == 0)
    return DIInliningInfo();

  DIInliningInfo InliningInfo;
  uint32_t CallFile = 0, CallLine = 0, CallColumn = 0;
  const DWARFLineTable *LineTable = 0;
  for (uint32_t i = 0, n = InlinedChain.size(); i != n; i++) {
    const DWARFDebugInfoEntryMinimal &FunctionDIE = InlinedChain[i];
    std::string FileName = "<invalid>";
    std::string FunctionName = "<invalid>";
    uint32_t Line = 0;
    uint32_t Column = 0;
    // Get function name if necessary.
    if (Specifier.needs(DILineInfoSpecifier::FunctionName)) {
      if (const char *Name = FunctionDIE.getSubroutineName(CU))
        FunctionName = Name;
    }
    if (Specifier.needs(DILineInfoSpecifier::FileLineInfo)) {
      const bool NeedsAbsoluteFilePath =
          Specifier.needs(DILineInfoSpecifier::AbsoluteFilePath);
      if (i == 0) {
        // For the topmost frame, initialize the line table of this
        // compile unit and fetch file/line info from it.
        LineTable = getLineTableForCompileUnit(CU);
        // For the topmost routine, get file/line info from line table.
        getFileLineInfoForCompileUnit(CU, LineTable, Address,
                                      NeedsAbsoluteFilePath,
                                      FileName, Line, Column);
      } else {
        // Otherwise, use call file, call line and call column from
        // previous DIE in inlined chain.
        getFileNameForCompileUnit(CU, LineTable, CallFile,
                                  NeedsAbsoluteFilePath, FileName);
        Line = CallLine;
        Column = CallColumn;
      }
      // Get call file/line/column of a current DIE.
      if (i + 1 < n) {
        FunctionDIE.getCallerFrame(CU, CallFile, CallLine, CallColumn);
      }
    }
    DILineInfo Frame(StringRef(FileName), StringRef(FunctionName),
                     Line, Column);
    InliningInfo.addFrame(Frame);
  }
  return InliningInfo;
}

DWARFContextInMemory::DWARFContextInMemory(object::ObjectFile *Obj) :
  IsLittleEndian(true /* FIXME */) {
  error_code ec;
  for (object::section_iterator i = Obj->begin_sections(),
         e = Obj->end_sections();
       i != e; i.increment(ec)) {
    StringRef name;
    i->getName(name);
    StringRef data;
    i->getContents(data);

    name = name.substr(name.find_first_not_of("._")); // Skip . and _ prefixes.
    if (name == "debug_info")
      InfoSection = data;
    else if (name == "debug_abbrev")
      AbbrevSection = data;
    else if (name == "debug_line")
      LineSection = data;
    else if (name == "debug_aranges")
      ARangeSection = data;
    else if (name == "debug_str")
      StringSection = data;
    else if (name == "debug_ranges") {
      // FIXME: Use the other dwo range section when we emit it.
      RangeDWOSection = data;
      RangeSection = data;
    }
    else if (name == "debug_info.dwo")
      InfoDWOSection = data;
    else if (name == "debug_abbrev.dwo")
      AbbrevDWOSection = data;
    else if (name == "debug_str.dwo")
      StringDWOSection = data;
    // Any more debug info sections go here.
    else
      continue;

    // TODO: For now only handle relocations for the debug_info section.
    RelocAddrMap *Map;
    if (name == "debug_info")
      Map = &InfoRelocMap;
    else if (name == "debug_info.dwo")
      Map = &InfoDWORelocMap;
    else
      continue;

    if (i->begin_relocations() != i->end_relocations()) {
      uint64_t SectionSize;
      i->getSize(SectionSize);
      for (object::relocation_iterator reloc_i = i->begin_relocations(),
             reloc_e = i->end_relocations();
           reloc_i != reloc_e; reloc_i.increment(ec)) {
        uint64_t Address;
        reloc_i->getAddress(Address);
        uint64_t Type;
        reloc_i->getType(Type);

        object::RelocVisitor V(Obj->getFileFormatName());
        // The section address is always 0 for debug sections.
        object::RelocToApply R(V.visit(Type, *reloc_i));
        if (V.error()) {
          SmallString<32> Name;
          error_code ec(reloc_i->getTypeName(Name));
          if (ec) {
            errs() << "Aaaaaa! Nameless relocation! Aaaaaa!\n";
          }
          errs() << "error: failed to compute relocation: "
                 << Name << "\n";
          continue;
        }

        if (Address + R.Width > SectionSize) {
          errs() << "error: " << R.Width << "-byte relocation starting "
                 << Address << " bytes into section " << name << " which is "
                 << SectionSize << " bytes long.\n";
          continue;
        }
        if (R.Width > 8) {
          errs() << "error: can't handle a relocation of more than 8 bytes at "
                    "a time.\n";
          continue;
        }
        DEBUG(dbgs() << "Writing " << format("%p", R.Value)
                     << " at " << format("%p", Address)
                     << " with width " << format("%d", R.Width)
                     << "\n");
        Map->insert(std::make_pair(Address, std::make_pair(R.Width, R.Value)));
      }
    }
  }
}

void DWARFContextInMemory::anchor() { }
