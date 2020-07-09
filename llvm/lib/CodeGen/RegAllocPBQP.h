#ifndef LLVM_CODEGEN_REGALLOCPBQP_CPP_H
#define LLVM_CODEGEN_REGALLOCPBQP_CPP_H

#include "llvm/CodeGen/RegAllocPBQP.h"
#include "RegisterCoalescer.h"
#include "Spiller.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PBQP/Graph.h"
#include "llvm/CodeGen/PBQP/Math.h"
#include "llvm/CodeGen/PBQP/Solution.h"
#include "llvm/CodeGen/PBQPRAConstraint.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

///
/// PBQP based allocators solve the register allocation problem by mapping
/// register allocation problems to Partitioned Boolean Quadratic
/// Programming problems.
class RegAllocPBQP : public MachineFunctionPass {
private:
  using RegSet = std::set<unsigned>;

public:
  static char ID;

  /// Construct a PBQP register allocator.
  RegAllocPBQP(char *cPassID = nullptr)
      : MachineFunctionPass(ID), customPassID(cPassID) {
    initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
    initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
    initializeLiveStacksPass(*PassRegistry::getPassRegistry());
    initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
  }

  /// Return the pass name.
  StringRef getPassName() const override { return "PBQP Register Allocator"; }

  /// PBQP analysis usage.
  void getAnalysisUsage(AnalysisUsage &au) const override;

  /// Perform register allocation
  bool runOnMachineFunction(MachineFunction &MF) override;
  bool runOnMachineFunctionCustom(MachineFunction &MF, VirtRegMap &vrm, LiveIntervals &lis, LiveRegMatrix &matrix, MachineLoopInfo* loops, MachineBlockFrequencyInfo* mbfi, Spiller* spiller, RegSet vRegsToAlloc, RegSet emptyIntervalVRegs);

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

private:
  using LI2NodeMap = std::map<const LiveInterval *, unsigned>;
  using Node2LIMap = std::vector<const LiveInterval *>;
  using AllowedSet = std::vector<unsigned>;
  using AllowedSetMap = std::vector<AllowedSet>;
  using RegPair = std::pair<unsigned, unsigned>;
  using CoalesceMap = std::map<RegPair, PBQP::PBQPNum>;

  char *customPassID;

  RegSet VRegsToAlloc, EmptyIntervalVRegs, VRegsAllocated;

  /// Inst which is a def of an original reg and whose defs are already all
  /// dead after remat is saved in DeadRemats. The deletion of such inst is
  /// postponed till all the allocations are done, so its remat expr is
  /// always available for the remat of all the siblings of the original reg.
  SmallPtrSet<MachineInstr *, 32> DeadRemats;

  /// Finds the initial set of vreg intervals to allocate.
  void findVRegIntervalsToAlloc(const MachineFunction &MF, LiveIntervals &LIS);

  /// Constructs an initial graph.
  void initializeGraph(PBQPRAGraph &G, VirtRegMap &VRM, Spiller &VRegSpiller);

  /// Spill the given VReg.
  void spillVReg(unsigned VReg, SmallVectorImpl<unsigned> &NewIntervals,
                 MachineFunction &MF, LiveIntervals &LIS, VirtRegMap &VRM,
                 Spiller &VRegSpiller);

  /// Given a solved PBQP problem maps this solution back to a register
  /// assignment.
  bool mapPBQPToRegAlloc(const PBQPRAGraph &G,
                         const PBQP::Solution &Solution,
                         VirtRegMap &VRM,
                         Spiller &VRegSpiller);

  /// Postprocessing before final spilling. Sets basic block "live in"
  /// variables.
  void finalizeAlloc(MachineFunction &MF, LiveIntervals &LIS,
                     VirtRegMap &VRM) const;

  void postOptimization(Spiller &VRegSpiller, LiveIntervals &LIS);
};

#endif
