#ifndef LLVM_CODEGEN_REGALLOCGREEDY_H
#define LLVM_CODEGEN_REGALLOCGREEDY_H

#include "AllocationOrder.h"
#include "InterferenceCache.h"
#include "LiveDebugVariables.h"
#include "RegAllocBase.h"
#include "SpillPlacement.h"
#include "Spiller.h"
#include "SplitKit.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/EdgeBundles.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalUnion.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <queue>
#include <tuple>
#include <utility>
#include <set>

using namespace llvm;

class RAGreedy : public MachineFunctionPass,
                 public RegAllocBase,
                 private LiveRangeEdit::Delegate {
  // Convenient shortcuts.
  using PQueue = std::priority_queue<std::pair<unsigned, unsigned>>;
  using SmallLISet = SmallPtrSet<LiveInterval *, 4>;
  using SmallVirtRegSet = SmallSet<unsigned, 16>;
  using RegSet = std::set<unsigned>;

  // context
  MachineFunction *MF;

  // Shortcuts to some useful interface.
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  RegisterClassInfo RCI;

  // analyses
  SlotIndexes *Indexes;
  MachineBlockFrequencyInfo *MBFI;
  MachineDominatorTree *DomTree;
  MachineLoopInfo *Loops;
  MachineOptimizationRemarkEmitter *ORE;
  EdgeBundles *Bundles;
  SpillPlacement *SpillPlacer;
  LiveDebugVariables *DebugVars;
  AliasAnalysis *AA;

  // state
  std::unique_ptr<Spiller> SpillerInstance;
  PQueue Queue;
  unsigned NextCascade;

  // Live ranges pass through a number of stages as we try to allocate them.
  // Some of the stages may also create new live ranges:
  //
  // - Region splitting.
  // - Per-block splitting.
  // - Local splitting.
  // - Spilling.
  //
  // Ranges produced by one of the stages skip the previous stages when they are
  // dequeued. This improves performance because we can skip interference checks
  // that are unlikely to give any results. It also guarantees that the live
  // range splitting algorithm terminates, something that is otherwise hard to
  // ensure.
  enum LiveRangeStage {
    /// Newly created live range that has never been queued.
    RS_New,

    /// Only attempt assignment and eviction. Then requeue as RS_Split.
    RS_Assign,

    /// Attempt live range splitting if assignment is impossible.
    RS_Split,

    /// Attempt more aggressive live range splitting that is guaranteed to make
    /// progress.  This is used for split products that may not be making
    /// progress.
    RS_Split2,

    /// Live range will be spilled.  No more splitting will be attempted.
    RS_Spill,


    /// Live range is in memory. Because of other evictions, it might get moved
    /// in a register in the end.
    RS_Memory,

    /// There is nothing more we can do to this live range.  Abort compilation
    /// if it can't be assigned.
    RS_Done
  };

  // Enum CutOffStage to keep a track whether the register allocation failed
  // because of the cutoffs encountered in last chance recoloring.
  // Note: This is used as bitmask. New value should be next power of 2.
  enum CutOffStage {
    // No cutoffs encountered
    CO_None = 0,

    // lcr-max-depth cutoff encountered
    CO_Depth = 1,

    // lcr-max-interf cutoff encountered
    CO_Interf = 2
  };

  uint8_t CutOffInfo;

#ifndef NDEBUG
  static const char *const StageName[];
#endif

  // RegInfo - Keep additional information about each live range.
  struct RegInfo {
    LiveRangeStage Stage = RS_New;

    // Cascade - Eviction loop prevention. See canEvictInterference().
    unsigned Cascade = 0;

    RegInfo() = default;
  };

  IndexedMap<RegInfo, VirtReg2IndexFunctor> ExtraRegInfo;

  LiveRangeStage getStage(const LiveInterval &VirtReg) const {
    return ExtraRegInfo[VirtReg.reg].Stage;
  }

  void setStage(const LiveInterval &VirtReg, LiveRangeStage Stage) {
    ExtraRegInfo.resize(MRI->getNumVirtRegs());
    ExtraRegInfo[VirtReg.reg].Stage = Stage;
  }

  template<typename Iterator>
  void setStage(Iterator Begin, Iterator End, LiveRangeStage NewStage) {
    ExtraRegInfo.resize(MRI->getNumVirtRegs());
    for (;Begin != End; ++Begin) {
      unsigned Reg = *Begin;
      if (ExtraRegInfo[Reg].Stage == RS_New)
        ExtraRegInfo[Reg].Stage = NewStage;
    }
  }

  /// Cost of evicting interference.
  struct EvictionCost {
    unsigned BrokenHints = 0; ///< Total number of broken hints.
    float MaxWeight = 0;      ///< Maximum spill weight evicted.

    EvictionCost() = default;

    bool isMax() const { return BrokenHints == ~0u; }

    void setMax() { BrokenHints = ~0u; }

    void setBrokenHints(unsigned NHints) { BrokenHints = NHints; }

    bool operator<(const EvictionCost &O) const {
      return std::tie(BrokenHints, MaxWeight) <
             std::tie(O.BrokenHints, O.MaxWeight);
    }
  };

  /// EvictionTrack - Keeps track of past evictions in order to optimize region
  /// split decision.
  class EvictionTrack {

  public:
    using EvictorInfo =
        std::pair<unsigned /* evictor */, unsigned /* physreg */>;
    using EvicteeInfo = llvm::DenseMap<unsigned /* evictee */, EvictorInfo>;

  private:
    /// Each Vreg that has been evicted in the last stage of selectOrSplit will
    /// be mapped to the evictor Vreg and the PhysReg it was evicted from.
    EvicteeInfo Evictees;

  public:
    /// Clear all eviction information.
    void clear() { Evictees.clear(); }

    ///  Clear eviction information for the given evictee Vreg.
    /// E.g. when Vreg get's a new allocation, the old eviction info is no
    /// longer relevant.
    /// \param Evictee The evictee Vreg for whom we want to clear collected
    /// eviction info.
    void clearEvicteeInfo(unsigned Evictee) { Evictees.erase(Evictee); }

    /// Track new eviction.
    /// The Evictor vreg has evicted the Evictee vreg from Physreg.
    /// \param PhysReg The physical register Evictee was evicted from.
    /// \param Evictor The evictor Vreg that evicted Evictee.
    /// \param Evictee The evictee Vreg.
    void addEviction(unsigned PhysReg, unsigned Evictor, unsigned Evictee) {
      Evictees[Evictee].first = Evictor;
      Evictees[Evictee].second = PhysReg;
    }

    /// Return the Evictor Vreg which evicted Evictee Vreg from PhysReg.
    /// \param Evictee The evictee vreg.
    /// \return The Evictor vreg which evicted Evictee vreg from PhysReg. 0 if
    /// nobody has evicted Evictee from PhysReg.
    EvictorInfo getEvictor(unsigned Evictee) {
      if (Evictees.count(Evictee)) {
        return Evictees[Evictee];
      }

      return EvictorInfo(0, 0);
    }
  };

  // Keeps track of past evictions in order to optimize region split decision.
  EvictionTrack LastEvicted;

  // splitting state.
  std::unique_ptr<SplitAnalysis> SA;
  std::unique_ptr<SplitEditor> SE;

  /// Cached per-block interference maps
  InterferenceCache IntfCache;

  /// All basic blocks where the current register has uses.
  SmallVector<SpillPlacement::BlockConstraint, 8> SplitConstraints;

  /// Global live range splitting candidate info.
  struct GlobalSplitCandidate {
    // Register intended for assignment, or 0.
    unsigned PhysReg;

    // SplitKit interval index for this candidate.
    unsigned IntvIdx;

    // Interference for PhysReg.
    InterferenceCache::Cursor Intf;

    // Bundles where this candidate should be live.
    BitVector LiveBundles;
    SmallVector<unsigned, 8> ActiveBlocks;

    void reset(InterferenceCache &Cache, unsigned Reg) {
      PhysReg = Reg;
      IntvIdx = 0;
      Intf.setPhysReg(Cache, Reg);
      LiveBundles.clear();
      ActiveBlocks.clear();
    }

    // Set B[i] = C for every live bundle where B[i] was NoCand.
    unsigned getBundles(SmallVectorImpl<unsigned> &B, unsigned C) {
      unsigned Count = 0;
      for (unsigned i : LiveBundles.set_bits())
        if (B[i] == NoCand) {
          B[i] = C;
          Count++;
        }
      return Count;
    }
  };

  /// Candidate info for each PhysReg in AllocationOrder.
  /// This vector never shrinks, but grows to the size of the largest register
  /// class.
  SmallVector<GlobalSplitCandidate, 32> GlobalCand;

  enum : unsigned { NoCand = ~0u };

  /// Candidate map. Each edge bundle is assigned to a GlobalCand entry, or to
  /// NoCand which indicates the stack interval.
  SmallVector<unsigned, 32> BundleCand;

  /// Callee-save register cost, calculated once per machine function.
  BlockFrequency CSRCost;

  /// Run or not the local reassignment heuristic. This information is
  /// obtained from the TargetSubtargetInfo.
  bool EnableLocalReassign;

  /// Enable or not the consideration of the cost of local intervals created
  /// by a split candidate when choosing the best split candidate.
  bool EnableAdvancedRASplitCost;

  /// Set of broken hints that may be reconciled later because of eviction.
  SmallSetVector<LiveInterval *, 8> SetOfBrokenHints;

  RegSet VRegsAllocated;

public:
  RAGreedy();

  /// Return the pass name.
  StringRef getPassName() const override { return "Greedy Register Allocator"; }

  /// RAGreedy analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;
  Spiller &spiller() override { return *SpillerInstance; }
  void enqueue(LiveInterval *LI) override;
  LiveInterval *dequeue() override;
  unsigned selectOrSplit(LiveInterval&, SmallVectorImpl<unsigned>&) override;
  void aboutToRemoveInterval(LiveInterval &) override;

  /// Perform register allocation.
  bool runOnMachineFunction(MachineFunction &mf) override;
  bool runOnMachineFunctionCustom(MachineFunction &mf, VirtRegMap &vrm, LiveIntervals &lis, LiveRegMatrix &matrix, SlotIndexes* indexes, MachineBlockFrequencyInfo* mbfi, MachineDominatorTree* domtree, MachineOptimizationRemarkEmitter* ore, MachineLoopInfo* loops, EdgeBundles* bundles, SpillPlacement* spillplacer, LiveDebugVariables* debugvars, AAResults* aa, Spiller* spiller, RegSet vRegsAllocated);

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

  static char ID;

private:
  unsigned selectOrSplitImpl(LiveInterval &, SmallVectorImpl<unsigned> &,
                             SmallVirtRegSet &, unsigned = 0);

  bool LRE_CanEraseVirtReg(unsigned) override;
  void LRE_WillShrinkVirtReg(unsigned) override;
  void LRE_DidCloneVirtReg(unsigned, unsigned) override;
  void enqueue(PQueue &CurQueue, LiveInterval *LI);
  LiveInterval *dequeue(PQueue &CurQueue);

  BlockFrequency calcSpillCost();
  bool addSplitConstraints(InterferenceCache::Cursor, BlockFrequency&);
  bool addThroughConstraints(InterferenceCache::Cursor, ArrayRef<unsigned>);
  bool growRegion(GlobalSplitCandidate &Cand);
  bool splitCanCauseEvictionChain(unsigned Evictee, GlobalSplitCandidate &Cand,
                                  unsigned BBNumber,
                                  const AllocationOrder &Order);
  bool splitCanCauseLocalSpill(unsigned VirtRegToSplit,
                               GlobalSplitCandidate &Cand, unsigned BBNumber,
                               const AllocationOrder &Order);
  BlockFrequency calcGlobalSplitCost(GlobalSplitCandidate &,
                                     const AllocationOrder &Order,
                                     bool *CanCauseEvictionChain);
  bool calcCompactRegion(GlobalSplitCandidate&);
  void splitAroundRegion(LiveRangeEdit&, ArrayRef<unsigned>);
  void calcGapWeights(unsigned, SmallVectorImpl<float>&);
  unsigned canReassign(LiveInterval &VirtReg, unsigned PrevReg);
  bool shouldEvict(LiveInterval &A, bool, LiveInterval &B, bool);
  bool canEvictInterference(LiveInterval&, unsigned, bool, EvictionCost&,
                            const SmallVirtRegSet&);
  bool canEvictInterferenceInRange(LiveInterval &VirtReg, unsigned PhysReg,
                                   SlotIndex Start, SlotIndex End,
                                   EvictionCost &MaxCost);
  unsigned getCheapestEvicteeWeight(const AllocationOrder &Order,
                                    LiveInterval &VirtReg, SlotIndex Start,
                                    SlotIndex End, float *BestEvictWeight);
  void evictInterference(LiveInterval&, unsigned,
                         SmallVectorImpl<unsigned>&);
  bool mayRecolorAllInterferences(unsigned PhysReg, LiveInterval &VirtReg,
                                  SmallLISet &RecoloringCandidates,
                                  const SmallVirtRegSet &FixedRegisters);

  unsigned tryAssign(LiveInterval&, AllocationOrder&,
                     SmallVectorImpl<unsigned>&,
                     const SmallVirtRegSet&);
  unsigned tryEvict(LiveInterval&, AllocationOrder&,
                    SmallVectorImpl<unsigned>&, unsigned,
                    const SmallVirtRegSet&);
  unsigned tryRegionSplit(LiveInterval&, AllocationOrder&,
                          SmallVectorImpl<unsigned>&);
  unsigned isSplitBenefitWorthCost(LiveInterval &VirtReg);
  /// Calculate cost of region splitting.
  unsigned calculateRegionSplitCost(LiveInterval &VirtReg,
                                    AllocationOrder &Order,
                                    BlockFrequency &BestCost,
                                    unsigned &NumCands, bool IgnoreCSR,
                                    bool *CanCauseEvictionChain = nullptr);
  /// Perform region splitting.
  unsigned doRegionSplit(LiveInterval &VirtReg, unsigned BestCand,
                         bool HasCompact,
                         SmallVectorImpl<unsigned> &NewVRegs);
  /// Check other options before using a callee-saved register for the first
  /// time.
  unsigned tryAssignCSRFirstTime(LiveInterval &VirtReg, AllocationOrder &Order,
                                 unsigned PhysReg, unsigned &CostPerUseLimit,
                                 SmallVectorImpl<unsigned> &NewVRegs);
  void initializeCSRCost();
  unsigned tryBlockSplit(LiveInterval&, AllocationOrder&,
                         SmallVectorImpl<unsigned>&);
  unsigned tryInstructionSplit(LiveInterval&, AllocationOrder&,
                               SmallVectorImpl<unsigned>&);
  unsigned tryLocalSplit(LiveInterval&, AllocationOrder&,
    SmallVectorImpl<unsigned>&);
  unsigned trySplit(LiveInterval&, AllocationOrder&,
                    SmallVectorImpl<unsigned>&,
                    const SmallVirtRegSet&);
  unsigned tryLastChanceRecoloring(LiveInterval &, AllocationOrder &,
                                   SmallVectorImpl<unsigned> &,
                                   SmallVirtRegSet &, unsigned);
  bool tryRecoloringCandidates(PQueue &, SmallVectorImpl<unsigned> &,
                               SmallVirtRegSet &, unsigned);
  void tryHintRecoloring(LiveInterval &);
  void tryHintsRecoloring();

  /// Model the information carried by one end of a copy.
  struct HintInfo {
    /// The frequency of the copy.
    BlockFrequency Freq;
    /// The virtual register or physical register.
    unsigned Reg;
    /// Its currently assigned register.
    /// In case of a physical register Reg == PhysReg.
    unsigned PhysReg;

    HintInfo(BlockFrequency Freq, unsigned Reg, unsigned PhysReg)
        : Freq(Freq), Reg(Reg), PhysReg(PhysReg) {}
  };
  using HintsInfo = SmallVector<HintInfo, 4>;

  BlockFrequency getBrokenHintFreq(const HintsInfo &, unsigned);
  void collectHintInfo(unsigned, HintsInfo &);

  bool isUnusedCalleeSavedReg(unsigned PhysReg) const;

  /// Compute and report the number of spills and reloads for a loop.
  void reportNumberOfSplillsReloads(MachineLoop *L, unsigned &Reloads,
                                    unsigned &FoldedReloads, unsigned &Spills,
                                    unsigned &FoldedSpills);

  /// Report the number of spills and reloads for each loop.
  void reportNumberOfSplillsReloads() {
    for (MachineLoop *L : *Loops) {
      unsigned Reloads, FoldedReloads, Spills, FoldedSpills;
      reportNumberOfSplillsReloads(L, Reloads, FoldedReloads, Spills,
                                   FoldedSpills);
    }
  }
};

#endif
