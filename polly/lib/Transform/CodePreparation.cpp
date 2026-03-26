//===---- CodePreparation.cpp - Code preparation for Scop Detection -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Polly code preparation pass is executed before SCoP detection. Its
// currently only splits the entry block of the SCoP to make room for alloc
// instructions as they are generated during code generation.
//
// XXX: In the future, we should remove the need for this pass entirely and
// instead add this spitting to the code generation pass.
//
//===----------------------------------------------------------------------===//

#include "polly/CodePreparation.h"
#include "polly/LinkAllPasses.h"
#include "polly/Options.h"
#include "polly/Support/ScopHelper.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;
using namespace polly;

#define DEBUG_TYPE "polly-prepare"

namespace {

static SmallVector<BranchInst *, 4>
collectNoGrowthBranches(Function &F, LoopInfo &LI, ScalarEvolution &SE) {
  SmallVector<BranchInst *, 4> Branches;
  if (!PollyForceOffsetFusion)
    return Branches;

  for (BasicBlock &BB : F) {
    auto *BI = dyn_cast<BranchInst>(BB.getTerminator());
    if (!BI || !BI->isConditional())
      continue;

    auto *ICmp = dyn_cast<ICmpInst>(BI->getCondition());
    if (!ICmp)
      continue;

    if (isKnownNoGrowBackInserterBranch(*ICmp, LI.getLoopFor(&BB), SE))
      Branches.push_back(BI);
  }

  return Branches;
}

struct NoGrowVersioningCandidate {
  BranchInst *GrowthBranch = nullptr;
  Loop *L = nullptr;
  const SCEV *RemainingBytes = nullptr;
  const SCEV *SourceSpanBytes = nullptr;
};

static SmallVector<NoGrowVersioningCandidate, 2>
collectNoGrowVersioningCandidates(Function &F, LoopInfo &LI,
                                  ScalarEvolution &SE) {
  SmallVector<NoGrowVersioningCandidate, 2> Candidates;
  if (!PollyForceOffsetFusion)
    return Candidates;

  for (BasicBlock &BB : F) {
    auto *BI = dyn_cast<BranchInst>(BB.getTerminator());
    if (!BI || !BI->isConditional())
      continue;

    auto *ICmp = dyn_cast<ICmpInst>(BI->getCondition());
    if (!ICmp)
      continue;

    Loop *L = LI.getLoopFor(&BB);
    const SCEV *RemainingBytes = nullptr;
    const SCEV *SourceSpanBytes = nullptr;
    if (!matchNoGrowBackInserterCheck(*ICmp, L, SE, RemainingBytes,
                                      SourceSpanBytes))
      continue;

    if (isKnownNoGrowBackInserterBranch(*ICmp, L, SE))
      continue;

    LLVM_DEBUG(dbgs() << "Polly found versionable no-growth candidate in "
                      << F.getName() << " at block " << BB.getName() << "\n");
    Candidates.push_back({BI, L, RemainingBytes, SourceSpanBytes});
  }

  return Candidates;
}

static bool rewriteNoGrowthFastPaths(Function &F,
                                     ArrayRef<BranchInst *> Branches) {
  bool Changed = false;

  for (BranchInst *BI : Branches) {
    if (!BI || !BI->getParent() || !BI->isConditional())
      continue;

    auto *ICmp = dyn_cast<ICmpInst>(BI->getCondition());
    if (!ICmp)
      continue;

    BasicBlock *BB = BI->getParent();
    BasicBlock *FastPath = BI->getSuccessor(0);
    BasicBlock *GrowthPath = BI->getSuccessor(1);
    if (FastPath == GrowthPath)
      continue;

    LLVM_DEBUG(dbgs() << "Polly rewrites proven no-growth branch in "
                      << F.getName() << ": " << BB->getName() << " -> "
                      << FastPath->getName() << "\n");

    BranchInst *FastBranch = BranchInst::Create(FastPath, BI->getIterator());
    FastBranch->setDebugLoc(BI->getDebugLoc());
    GrowthPath->removePredecessor(BB, /*KeepOneInputPHIs=*/true);
    BI->eraseFromParent();

    if (ICmp->use_empty())
      RecursivelyDeleteTriviallyDeadInstructions(ICmp);

    Changed = true;
  }

  if (!Changed)
    return false;

  // TODO: offset-aware fusion for STL patterns
  // Delete the dead growth subtree before ScopDetection so RegionInfo sees the
  // no-growth fast path as the only structured loop body.
  EliminateUnreachableBlocks(F, nullptr, /*KeepOneInputPHIs=*/true);
  return true;
}

static bool isCanonicalNoGrowVersioningLoop(Loop &L, BranchInst &GrowthBranch) {
  if (!L.getHeader() || L.getNumBlocks() < 4)
    return false;
  if (!L.getLoopLatch())
    return false;
  if (GrowthBranch.getParent() == L.getHeader())
    return false;

  auto *HeaderBr = dyn_cast<BranchInst>(L.getHeader()->getTerminator());
  if (!HeaderBr || !HeaderBr->isConditional())
    return false;

  BasicBlock *FastSucc = GrowthBranch.getSuccessor(0);
    BasicBlock *GrowthSucc = GrowthBranch.getSuccessor(1);
    BasicBlock *Latch = L.getLoopLatch();
  if (!L.contains(FastSucc) || !L.contains(GrowthSucc) || !L.contains(Latch))
    return false;

  return FastSucc->getSingleSuccessor() == Latch;
}

static void rewriteClonedNoGrowBranch(BranchInst &OrigGrowthBranch,
                                      ValueToValueMapTy &VMap) {
  auto *ClonedBB = cast<BasicBlock>(VMap[OrigGrowthBranch.getParent()]);
  auto *ClonedBI = cast<BranchInst>(ClonedBB->getTerminator());
  auto *ClonedICmp = dyn_cast<ICmpInst>(ClonedBI->getCondition());
  BasicBlock *ClonedFastSucc =
      cast<BasicBlock>(VMap[OrigGrowthBranch.getSuccessor(0)]);
  BasicBlock *ClonedGrowthSucc =
      cast<BasicBlock>(VMap[OrigGrowthBranch.getSuccessor(1)]);

  BranchInst *FastBranch = BranchInst::Create(ClonedFastSucc, ClonedBI->getIterator());
  FastBranch->setDebugLoc(ClonedBI->getDebugLoc());
  ClonedBI->eraseFromParent();
  if (is_contained(predecessors(ClonedGrowthSucc), ClonedBB))
    ClonedGrowthSucc->removePredecessor(ClonedBB, /*KeepOneInputPHIs=*/true);
  if (ClonedICmp && ClonedICmp->use_empty())
    RecursivelyDeleteTriviallyDeadInstructions(ClonedICmp);
}

static SmallVector<BasicBlock *, 8>
collectExclusivelyDeadSubtree(BasicBlock *DeadRoot) {
  SmallVector<BasicBlock *, 8> DeadBlocks;
  if (!DeadRoot || !pred_empty(DeadRoot))
    return DeadBlocks;

  SmallPtrSet<BasicBlock *, 8> DeadSet;
  DeadSet.insert(DeadRoot);
  DeadBlocks.push_back(DeadRoot);

  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (BasicBlock *DeadBB : DeadBlocks) {
      for (BasicBlock *Succ : successors(DeadBB)) {
        if (DeadSet.contains(Succ))
          continue;

        bool AllPredsDead = true;
        for (BasicBlock *Pred : predecessors(Succ)) {
          if (!DeadSet.contains(Pred)) {
            AllPredsDead = false;
            break;
          }
        }

        if (!AllPredsDead)
          continue;

        DeadSet.insert(Succ);
        DeadBlocks.push_back(Succ);
        Changed = true;
      }
    }
  }

  return DeadBlocks;
}

static Value *lookupClonedValue(Value *V, ValueToValueMapTy &VMap) {
  auto It = VMap.find(V);
  if (It == VMap.end())
    return V;
  return It->second;
}

static void addClonedExitPhiInputs(Loop &OrigLoop, ValueToValueMapTy &VMap) {
  for (BasicBlock *OrigBB : OrigLoop.blocks()) {
    auto *ClonedBB = cast<BasicBlock>(VMap[OrigBB]);
    auto *Term = ClonedBB->getTerminator();
    for (BasicBlock *Succ : successors(Term)) {
      if (OrigLoop.contains(Succ))
        continue;

      for (PHINode &Phi : Succ->phis()) {
        int OrigIncomingIdx = Phi.getBasicBlockIndex(OrigBB);
        if (OrigIncomingIdx < 0)
          continue;

        if (Phi.getBasicBlockIndex(ClonedBB) >= 0)
          continue;

        Value *Incoming = Phi.getIncomingValue(OrigIncomingIdx);
        Phi.addIncoming(lookupClonedValue(Incoming, VMap), ClonedBB);
      }
    }
  }
}

static void addClonedExitPhiInputs(ArrayRef<BasicBlock *> OrigBlocks,
                                   ValueToValueMapTy &VMap) {
  SmallPtrSet<BasicBlock *, 16> OrigBlockSet(OrigBlocks.begin(),
                                             OrigBlocks.end());

  for (BasicBlock *OrigBB : OrigBlocks) {
    auto *ClonedBB = cast<BasicBlock>(VMap[OrigBB]);
    auto *Term = ClonedBB->getTerminator();
    for (BasicBlock *Succ : successors(Term)) {
      if (OrigBlockSet.contains(Succ))
        continue;

      for (PHINode &Phi : Succ->phis()) {
        int OrigIncomingIdx = Phi.getBasicBlockIndex(OrigBB);
        if (OrigIncomingIdx < 0)
          continue;

        if (Phi.getBasicBlockIndex(ClonedBB) >= 0)
          continue;

        Value *Incoming = Phi.getIncomingValue(OrigIncomingIdx);
        Phi.addIncoming(lookupClonedValue(Incoming, VMap), ClonedBB);
      }
    }
  }
}

static void pruneBlockPhiInputsToPredecessors(BasicBlock *BB) {
  if (!BB)
    return;

  SmallPtrSet<BasicBlock *, 8> Preds(pred_begin(BB), pred_end(BB));
  for (PHINode &Phi : BB->phis()) {
    SmallVector<unsigned, 4> ToRemove;
    for (unsigned I = 0, E = Phi.getNumIncomingValues(); I != E; ++I)
      if (!Preds.contains(Phi.getIncomingBlock(I)))
        ToRemove.push_back(I);
    for (unsigned I : reverse(ToRemove))
      Phi.removeIncomingValue(I, /*DeletePHIIfEmpty=*/false);
  }
}

static BasicBlock *createDedicatedExitEdgeBlock(BasicBlock *ExitingPred,
                                                BasicBlock *OrigExit) {
  if (!ExitingPred || !OrigExit)
    return nullptr;

  auto *Term = ExitingPred->getTerminator();
  if (!Term)
    return nullptr;

  unsigned SuccIdx = 0;
  bool FoundSucc = false;
  for (unsigned I = 0, E = Term->getNumSuccessors(); I != E; ++I) {
    if (Term->getSuccessor(I) != OrigExit)
      continue;
    SuccIdx = I;
    FoundSucc = true;
    break;
  }
  if (!FoundSucc)
    return nullptr;

  BasicBlock *EdgeBB = BasicBlock::Create(
      OrigExit->getContext(), OrigExit->getName() + ".polly.nogrow.edge",
      OrigExit->getParent(), OrigExit);
  BranchInst::Create(OrigExit, EdgeBB);
  Term->setSuccessor(SuccIdx, EdgeBB);

  for (PHINode &Phi : OrigExit->phis()) {
    int IncomingIdx = Phi.getBasicBlockIndex(ExitingPred);
    if (IncomingIdx < 0)
      continue;
    Phi.setIncomingBlock(IncomingIdx, EdgeBB);
  }

  return EdgeBB;
}

static BasicBlock *cloneImmediateExitBlock(BasicBlock *OrigExit,
                                           ArrayRef<BasicBlock *> ClonedBlocks,
                                           ValueToValueMapTy &VMap) {
  if (!OrigExit)
    return nullptr;

  BasicBlock *ClonedExit =
      CloneBasicBlock(OrigExit, VMap, ".polly.nogrow", OrigExit->getParent());
  VMap[OrigExit] = ClonedExit;

  for (Instruction &I : *ClonedExit)
    RemapInstruction(&I, VMap,
                     RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);

  for (BasicBlock *ClonedBB : ClonedBlocks) {
    auto *Term = ClonedBB->getTerminator();
    for (unsigned I = 0, E = Term->getNumSuccessors(); I != E; ++I)
      if (Term->getSuccessor(I) == OrigExit)
        Term->setSuccessor(I, ClonedExit);
  }

  pruneBlockPhiInputsToPredecessors(ClonedExit);
  pruneBlockPhiInputsToPredecessors(OrigExit);

  addClonedExitPhiInputs(ArrayRef<BasicBlock *>{OrigExit}, VMap);
  return ClonedExit;
}

static SmallVector<BasicBlock *, 8>
collectNoGrowVersioningEntries(BasicBlock *Start, DominatorTree &DT) {
  SmallVector<BasicBlock *, 8> Entries;
  BasicBlock *Current = Start;
  while (Current) {
    Entries.push_back(Current);

    auto PredIt = pred_begin(Current);
    auto PredEnd = pred_end(Current);
    if (PredIt == PredEnd)
      break;

    BasicBlock *FirstPred = *PredIt++;
    if (PredIt == PredEnd)
      break;

    BasicBlock *NCD = FirstPred;
    for (; PredIt != PredEnd; ++PredIt) {
      NCD = DT.findNearestCommonDominator(NCD, *PredIt);
      if (!NCD)
        return Entries;
    }

    if (NCD == Current)
      break;
    Current = NCD;
  }

  return Entries;
}

static bool canMaterializeSCEVBefore(const SCEV *Expr, Instruction *InsertPt,
                                     DominatorTree &DT) {
  SmallVector<const SCEV *, 8> Worklist;
  SmallPtrSet<const SCEV *, 8> Seen;
  Worklist.push_back(Expr);

  while (!Worklist.empty()) {
    const SCEV *Current = Worklist.pop_back_val();
    if (!Seen.insert(Current).second)
      continue;

    if (auto *Unknown = dyn_cast<SCEVUnknown>(Current)) {
      if (auto *I = dyn_cast<Instruction>(Unknown->getValue()))
        if (!DT.dominates(I, InsertPt))
          return false;
      continue;
    }

    for (const SCEV *Op : Current->operands())
      Worklist.push_back(Op);
  }

  return true;
}

static SmallVector<BasicBlock *, 16>
collectFastPathSliceBlocks(BasicBlock *Entry, BasicBlock *Exit) {
  SmallVector<BasicBlock *, 16> Worklist;
  SmallVector<BasicBlock *, 16> Blocks;
  SmallPtrSet<BasicBlock *, 16> Seen;

  if (!Entry || !Exit)
    return Blocks;

  Worklist.push_back(Entry);
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    if (!Seen.insert(BB).second)
      continue;
    if (BB == Exit)
      continue;

    Blocks.push_back(BB);
    for (BasicBlock *Succ : successors(BB))
      Worklist.push_back(Succ);
  }

  return Blocks;
}

static BasicBlock *cloneFastPathSlice(
    BasicBlock *Entry, BasicBlock *Exit, ArrayRef<BasicBlock *> OrigBlocks,
    ValueToValueMapTy &VMap, StringRef Suffix) {
  SmallPtrSet<BasicBlock *, 16> OrigBlockSet(OrigBlocks.begin(),
                                             OrigBlocks.end());

  for (BasicBlock *OrigBB : OrigBlocks) {
    BasicBlock *Clone = CloneBasicBlock(OrigBB, VMap, Suffix, OrigBB->getParent());
    VMap[OrigBB] = Clone;
  }

  for (BasicBlock *OrigBB : OrigBlocks) {
    auto *ClonedBB = cast<BasicBlock>(VMap[OrigBB]);
    for (Instruction &I : *ClonedBB)
      RemapInstruction(&I, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);

    auto *Term = ClonedBB->getTerminator();
    for (unsigned I = 0, E = Term->getNumSuccessors(); I != E; ++I) {
      BasicBlock *Succ = Term->getSuccessor(I);
      if (!OrigBlockSet.contains(Succ))
        continue;
      Term->setSuccessor(I, cast<BasicBlock>(VMap[Succ]));
    }
  }

  addClonedExitPhiInputs(OrigBlocks, VMap);
  return cast<BasicBlock>(VMap[Entry]);
}

static bool versionNoGrowFastPaths(Function &F,
                                   ArrayRef<NoGrowVersioningCandidate> Candidates,
                                   LoopInfo &LI, DominatorTree &DT,
                                   ScalarEvolution &SE) {
  bool Changed = false;

  for (const NoGrowVersioningCandidate &Candidate : Candidates) {
    BranchInst *GrowthBranch = Candidate.GrowthBranch;
    if (!GrowthBranch || !GrowthBranch->getParent() || !GrowthBranch->isConditional())
      continue;

    Loop *L = Candidate.L;
    if (!L) {
      LLVM_DEBUG(dbgs() << "Polly skipped no-growth versioning candidate without loop\n");
      continue;
    }
    if (!isCanonicalNoGrowVersioningLoop(*L, *GrowthBranch)) {
      LLVM_DEBUG(dbgs() << "Polly skipped non-canonical no-growth loop at header "
                        << L->getHeader()->getName() << "\n");
      continue;
    }

    BasicBlock *Header = L->getHeader();
    BasicBlock *LoopPred = L->getLoopPredecessor();
    if (!Header || !LoopPred) {
      LLVM_DEBUG(dbgs() << "Polly skipped no-growth versioning due to missing loop predecessor for "
                        << (Header ? Header->getName() : "<null>") << "\n");
      continue;
    }

    if (!L->getLoopPreheader()) {
      SmallVector<BasicBlock *, 1> Preds = {LoopPred};
      BasicBlock *NewPreheader =
          SplitBlockPredecessors(Header, Preds, ".polly.nogrow.preheader", &DT, &LI);
      if (!NewPreheader)
        continue;
      LoopPred = NewPreheader;
    }

    BasicBlock *OrigPreheader = L->getLoopPreheader();
    if (!OrigPreheader) {
      LLVM_DEBUG(dbgs() << "Polly failed to materialize loop preheader for "
                        << Header->getName() << "\n");
      continue;
    }

    BasicBlock *SliceExit = nullptr;
    BasicBlock *Latch = L->getLoopLatch();
    if (Latch) {
      for (BasicBlock *Succ : successors(Latch)) {
        if (!L->contains(Succ)) {
          SliceExit = Succ;
          break;
        }
      }
    }
    if (!SliceExit) {
      LLVM_DEBUG(dbgs() << "Polly skipped no-growth versioning due to missing fast-path exit for "
                        << Header->getName() << "\n");
      continue;
    }

    BasicBlock *InitialEntry = OrigPreheader->getSinglePredecessor();
    if (!InitialEntry) {
      LLVM_DEBUG(dbgs() << "Polly skipped no-growth versioning due to ambiguous preheader predecessor for "
                        << Header->getName() << "\n");
      continue;
    }

    BasicBlock *SliceEntry = InitialEntry;
    for (BasicBlock *CandidateEntry : collectNoGrowVersioningEntries(InitialEntry,
                                                                     DT)) {
      BasicBlock *CandidatePred = CandidateEntry->getSinglePredecessor();
      if (!CandidatePred)
        break;

      Instruction *InsertPt = CandidatePred->getTerminator();
      if (!canMaterializeSCEVBefore(Candidate.RemainingBytes, InsertPt, DT) ||
          !canMaterializeSCEVBefore(Candidate.SourceSpanBytes, InsertPt, DT))
        break;

      SliceEntry = CandidateEntry;
    }

    if (SliceEntry != InitialEntry) {
      BasicBlock *VersionPred = SliceEntry->getSinglePredecessor();
      if (!VersionPred) {
        LLVM_DEBUG(dbgs() << "Polly skipped widened no-growth versioning due to missing single predecessor for slice entry "
                          << SliceEntry->getName() << "\n");
        continue;
      }

      SmallVector<BasicBlock *, 1> Preds = {VersionPred};
      BasicBlock *VersionCheckBB = SplitBlockPredecessors(
          SliceEntry, Preds, ".polly.nogrow.check", &DT, &LI);
      if (!VersionCheckBB) {
        LLVM_DEBUG(dbgs() << "Polly failed to split widened version check block for "
                          << SliceEntry->getName() << "\n");
        continue;
      }

      ValueToValueMapTy VMap;
      SmallVector<BasicBlock *, 16> SliceBlocks =
          collectFastPathSliceBlocks(SliceEntry, SliceExit);
      if (SliceBlocks.empty()) {
        LLVM_DEBUG(dbgs() << "Polly skipped widened no-growth versioning due to empty slice for "
                          << Header->getName() << "\n");
        continue;
      }

      BasicBlock *ClonedEntry =
          cloneFastPathSlice(SliceEntry, SliceExit, SliceBlocks, VMap,
                             ".polly.nogrow");
      rewriteClonedNoGrowBranch(*GrowthBranch, VMap);
      auto *ClonedGrowthSucc =
          cast<BasicBlock>(VMap[GrowthBranch->getSuccessor(1)]);
      SmallVector<BasicBlock *, 8> DeadClonedBlocks =
          collectExclusivelyDeadSubtree(ClonedGrowthSucc);
      if (!DeadClonedBlocks.empty())
        DeleteDeadBlocks(DeadClonedBlocks, nullptr, /*KeepOneInputPHIs=*/true);

      IRBuilder<> Builder(VersionCheckBB->getTerminator());
      SCEVExpander Expander(SE, F.getDataLayout(), "polly.nogrow");
      Value *Remaining = Expander.expandCodeFor(
          Candidate.RemainingBytes, Candidate.RemainingBytes->getType(),
          Builder.GetInsertPoint());
      Value *Source = Expander.expandCodeFor(Candidate.SourceSpanBytes,
                                             Candidate.SourceSpanBytes->getType(),
                                             Builder.GetInsertPoint());
      Value *EnoughCapacity =
          Builder.CreateICmpUGE(Remaining, Source, "polly.nogrow");

      auto *OldBranch = cast<BranchInst>(VersionCheckBB->getTerminator());
      BranchInst *NewBranch =
          BranchInst::Create(ClonedEntry, SliceEntry, EnoughCapacity,
                             OldBranch->getIterator());
      NewBranch->setDebugLoc(OldBranch->getDebugLoc());
      OldBranch->eraseFromParent();

      LLVM_DEBUG(dbgs() << "Polly versions widened no-growth fast path in "
                        << F.getName() << " for loop header "
                        << Header->getName() << " from slice entry "
                        << SliceEntry->getName() << "\n");
    } else {
      BasicBlock *VersionPred = OrigPreheader->getSinglePredecessor();
      if (!VersionPred) {
        LLVM_DEBUG(dbgs() << "Polly skipped no-growth versioning due to ambiguous preheader predecessor for "
                          << Header->getName() << "\n");
        continue;
      }

      SmallVector<BasicBlock *, 1> Preds = {VersionPred};
      BasicBlock *VersionCheckBB = SplitBlockPredecessors(
          OrigPreheader, Preds, ".polly.nogrow.check", &DT, &LI);
      if (!VersionCheckBB) {
        LLVM_DEBUG(dbgs() << "Polly failed to split version check block for "
                          << Header->getName() << "\n");
        continue;
      }

      ValueToValueMapTy VMap;
      SmallVector<BasicBlock *, 8> ClonedBlocks;
      SmallVector<BasicBlock *, 4> ExitBlocks;
      L->getExitBlocks(ExitBlocks);
      if (ExitBlocks.empty()) {
        LLVM_DEBUG(dbgs() << "Polly skipped no-growth versioning due to missing exit blocks for "
                          << Header->getName() << "\n");
        continue;
      }
      BasicBlock *NormalExit = nullptr;
      if (BasicBlock *Latch = L->getLoopLatch()) {
        for (BasicBlock *Succ : successors(Latch)) {
          if (!L->contains(Succ)) {
            NormalExit = Succ;
            break;
          }
        }
      }
      if (!NormalExit)
        NormalExit = ExitBlocks.front();

      BasicBlock *InsertBefore = NormalExit;
      Loop *ClonedLoop = cloneLoopWithPreheader(InsertBefore, VersionCheckBB, L,
                                                VMap, ".polly.nogrow", &LI,
                                                &DT, ClonedBlocks);
      if (!ClonedLoop)
        continue;
      remapInstructionsInBlocks(ClonedBlocks, VMap);
      BasicBlock *OrigExit = NormalExit;
      addClonedExitPhiInputs(*L, VMap);

      rewriteClonedNoGrowBranch(*GrowthBranch, VMap);
      SmallVector<BasicBlock *, 4> ExitingBlocks;
      L->getExitingBlocks(ExitingBlocks);
      for (BasicBlock *OrigExiting : ExitingBlocks) {
        auto *ClonedExiting = dyn_cast_or_null<BasicBlock>(VMap[OrigExiting]);
        LLVM_DEBUG({
          dbgs() << "Polly checks cloned exiting block "
                 << OrigExiting->getName() << " -> "
                 << (ClonedExiting ? ClonedExiting->getName() : "<null>")
                 << " against exit " << OrigExit->getName() << "\n";
        });
        if (!ClonedExiting || !is_contained(successors(ClonedExiting), OrigExit))
          continue;

        if (BasicBlock *DedicatedExit =
                createDedicatedExitEdgeBlock(ClonedExiting, OrigExit)) {
          LLVM_DEBUG(dbgs() << "Polly created dedicated no-growth exit edge "
                            << DedicatedExit->getName() << " from "
                            << ClonedExiting->getName() << "\n");
          (void)DedicatedExit;
          pruneBlockPhiInputsToPredecessors(OrigExit);
          break;
        }
      }
      auto *ClonedGrowthSucc =
          cast<BasicBlock>(VMap[GrowthBranch->getSuccessor(1)]);
      SmallVector<BasicBlock *, 8> DeadClonedBlocks =
          collectExclusivelyDeadSubtree(ClonedGrowthSucc);
      if (!DeadClonedBlocks.empty())
        DeleteDeadBlocks(DeadClonedBlocks, nullptr, /*KeepOneInputPHIs=*/true);

      IRBuilder<> Builder(VersionCheckBB->getTerminator());
      SCEVExpander Expander(SE, F.getDataLayout(), "polly.nogrow");
      Value *Remaining = Expander.expandCodeFor(
          Candidate.RemainingBytes, Candidate.RemainingBytes->getType(),
          Builder.GetInsertPoint());
      Value *Source = Expander.expandCodeFor(Candidate.SourceSpanBytes,
                                             Candidate.SourceSpanBytes->getType(),
                                             Builder.GetInsertPoint());
      Value *EnoughCapacity =
          Builder.CreateICmpUGE(Remaining, Source, "polly.nogrow");

      auto *OldBranch = cast<BranchInst>(VersionCheckBB->getTerminator());
      BranchInst *NewBranch =
          BranchInst::Create(cast<BasicBlock>(VMap[OrigPreheader]), OrigPreheader,
                             EnoughCapacity, OldBranch->getIterator());
      NewBranch->setDebugLoc(OldBranch->getDebugLoc());
      OldBranch->eraseFromParent();

      LLVM_DEBUG(dbgs() << "Polly versions no-growth fast path in "
                        << F.getName() << " for loop header "
                        << Header->getName() << "\n");
    }

    Changed = true;
    break;
  }

  if (!Changed)
    return false;

  return true;
}

/// Prepare the IR for the scop detection.
///
class CodePreparation final : public FunctionPass {
  CodePreparation(const CodePreparation &) = delete;
  const CodePreparation &operator=(const CodePreparation &) = delete;

  LoopInfo *LI;
  ScalarEvolution *SE;

  void clear();

public:
  static char ID;

  explicit CodePreparation() : FunctionPass(ID) {}
  ~CodePreparation();

  /// @name FunctionPass interface.
  //@{
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;
  bool runOnFunction(Function &F) override;
  void print(raw_ostream &OS, const Module *) const override;
  //@}
};
} // namespace

PreservedAnalyses CodePreparationPass::run(Function &F,
                                           FunctionAnalysisManager &FAM) {
  auto &LI = FAM.getResult<LoopAnalysis>(F);
  auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  SmallVector<BranchInst *, 4> NoGrowthBranches =
      collectNoGrowthBranches(F, LI, SE);
  SmallVector<NoGrowVersioningCandidate, 2> NoGrowVersioningCandidates =
      collectNoGrowVersioningCandidates(F, LI, SE);

  // Find first non-alloca instruction. Every basic block has a non-alloca
  // instruction, as every well formed basic block has a terminator.
  auto &EntryBlock = F.getEntryBlock();
  BasicBlock::iterator I = EntryBlock.begin();
  while (isa<AllocaInst>(I))
    ++I;

  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);

  // splitBlock updates DT, LI and RI.
  splitEntryBlockForAlloca(&EntryBlock, &DT, &LI, nullptr);

  if (rewriteNoGrowthFastPaths(F, NoGrowthBranches))
    return PreservedAnalyses::none();
  if (versionNoGrowFastPaths(F, NoGrowVersioningCandidates, LI, DT, SE))
    return PreservedAnalyses::none();

  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}

void CodePreparation::clear() {}

CodePreparation::~CodePreparation() { clear(); }

void CodePreparation::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
}

bool CodePreparation::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  DominatorTree *DT = DTWP ? &DTWP->getDomTree() : nullptr;
  SmallVector<BranchInst *, 4> NoGrowthBranches =
      collectNoGrowthBranches(F, *LI, *SE);
  SmallVector<NoGrowVersioningCandidate, 2> NoGrowVersioningCandidates =
      collectNoGrowVersioningCandidates(F, *LI, *SE);

  splitEntryBlockForAlloca(&F.getEntryBlock(), this);

  rewriteNoGrowthFastPaths(F, NoGrowthBranches);
  if (DT)
    versionNoGrowFastPaths(F, NoGrowVersioningCandidates, *LI, *DT, *SE);
  return true;
}

void CodePreparation::releaseMemory() { clear(); }

void CodePreparation::print(raw_ostream &OS, const Module *) const {}

char CodePreparation::ID = 0;
char &polly::CodePreparationID = CodePreparation::ID;

Pass *polly::createCodePreparationPass() { return new CodePreparation(); }

INITIALIZE_PASS_BEGIN(CodePreparation, "polly-prepare",
                      "Polly - Prepare code for polly", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(CodePreparation, "polly-prepare",
                    "Polly - Prepare code for polly", false, false)
