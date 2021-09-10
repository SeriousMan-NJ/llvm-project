//===- RegAllocBase.h - basic regalloc interface and driver -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegAllocBase class, which is the skeleton of a basic
// register allocation algorithm and interface for extending it. It provides the
// building blocks on which to construct other experimental allocators and test
// the validity of two principles:
//
// - If virtual and physical register liveness is modeled using intervals, then
// on-the-fly interference checking is cheap. Furthermore, interferences can be
// lazily cached and reused.
//
// - Register allocation complexity, and generated code performance is
// determined by the effectiveness of live range splitting rather than optimal
// coloring.
//
// Following the first principle, interfering checking revolves around the
// LiveIntervalUnion data structure.
//
// To fulfill the second principle, the basic allocator provides a driver for
// incremental splitting. It essentially punts on the problem of register
// coloring, instead driving the assignment of virtual to physical registers by
// the cost of splitting. The basic allocator allows for heuristic reassignment
// of registers, if a more sophisticated allocator chooses to do that.
//
// This framework provides a way to engineer the compile time vs. code
// quality trade-off without relying on a particular theoretical solver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_REGALLOCBASE_H
#define LLVM_LIB_CODEGEN_REGALLOCBASE_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/ADT/IndexedMap.h"

namespace llvm {

class LiveInterval;
class LiveIntervals;
class LiveRegMatrix;
class MachineInstr;
class MachineRegisterInfo;
template<typename T> class SmallVectorImpl;
class Spiller;
class TargetRegisterInfo;
class VirtRegMap;

/// RegAllocBase provides the register allocation driver and interface that can
/// be extended to add interesting heuristics.
///
/// Register allocators must override the selectOrSplit() method to implement
/// live range splitting. They must also override enqueue/dequeue to provide an
/// assignment order.
class RegAllocBase {
  virtual void anchor();

protected:
  const TargetRegisterInfo *TRI = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  VirtRegMap *VRM = nullptr;
  LiveIntervals *LIS = nullptr;
  LiveRegMatrix *Matrix = nullptr;
  RegisterClassInfo RegClassInfo;
  using RegSet = std::set<unsigned>;
  RegSet VRegsToAlloc, EmptyIntervalVRegs;
  bool isPBQP = false;

  /// Inst which is a def of an original reg and whose defs are already all
  /// dead after remat is saved in DeadRemats. The deletion of such inst is
  /// postponed till all the allocations are done, so its remat expr is
  /// always available for the remat of all the siblings of the original reg.
  SmallPtrSet<MachineInstr *, 32> DeadRemats;

  RegAllocBase() = default;
  virtual ~RegAllocBase() = default;

  // A RegAlloc pass should call this before allocatePhysRegs.
  void init(VirtRegMap &vrm, LiveIntervals &lis, LiveRegMatrix &mat);

  // The top-level driver. The output is a VirtRegMap that us updated with
  // physical register assignments.
  void allocatePhysRegs();

  // Include spiller post optimization and removing dead defs left because of
  // rematerialization.
  virtual void postOptimization();

  // Get a temporary reference to a Spiller instance.
  virtual Spiller &spiller() = 0;

  /// enqueue - Add VirtReg to the priority queue of unassigned registers.
  virtual void enqueue(LiveInterval *LI) = 0;

  /// dequeue - Return the next unassigned register, or NULL.
  virtual LiveInterval *dequeue() = 0;

  // A RegAlloc pass should override this to provide the allocation heuristics.
  // Each call must guarantee forward progess by returning an available PhysReg
  // or new set of split live virtual registers. It is up to the splitter to
  // converge quickly toward fully spilled live ranges.
  virtual MCRegister selectOrSplit(LiveInterval &VirtReg,
                                   SmallVectorImpl<Register> &splitLVRs) = 0;

  // Use this group name for NamedRegionTimer.
  static const char TimerGroupName[];
  static const char TimerGroupDescription[];

  /// Method called when the allocator is about to remove a LiveInterval.
  virtual void aboutToRemoveInterval(LiveInterval &LI) {}
  virtual float calcPotentialSpillCosts() { return -1; }

  int getRound(std::string filename);
  bool isSuboptimal(std::string filename);

  float MinSpillCost;
  int Round;
  int MinRound;
  int Limit;
  bool Fallback;
  bool MaybeSuboptimal;
  bool _EnableFallback = false;

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
    RS_Done,

    RS_Instruction_Split,
    RS_Local_Split,
    RS_Block_Split,
    RS_Region_Split,
    RS_Not_Split
  };

  // RegInfo - Keep additional information about each live range.
  struct RegInfo {
    LiveRangeStage Stage = RS_New;

    // Cascade - Eviction loop prevention. See canEvictInterference().
    unsigned Cascade = 0;

    RegInfo() = default;
  };

  IndexedMap<RegInfo, VirtReg2IndexFunctor> ExtraRegInfo;
  IndexedMap<RegInfo, VirtReg2IndexFunctor> DetailedRegStageInfo;

  void printCost(float cost);
  void printCost(std::string msg);
  void printStage(LiveRangeStage stage, int detailed_stage, std::string f="");

public:
  /// VerifyEnabled - True when -verify-regalloc is given.
  static bool VerifyEnabled;

private:
  void seedLiveRegs();
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_REGALLOCBASE_H
