//===- RegAllocBase.cpp - Register Allocator Base Class -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegAllocBase class which provides common functionality
// for LiveIntervalUnion-based register allocators.
//
//===----------------------------------------------------------------------===//

#include "RegAllocBase.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Spiller.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <limits>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumNewQueued    , "Number of new live ranges queued");

extern int LookaheadThreshold;

// Hysteresis to use when comparing floats.
// This helps stabilize decisions based on float comparisons.
static const float Hysteresis = (2007 / 2048.0f); // 0.97998046875

static const float EPSILON = std::numeric_limits<float>::epsilon();

// Temporary verification option until we can put verification inside
// MachineVerifier.
static cl::opt<bool, true>
    VerifyRegAlloc("verify-regalloc", cl::location(RegAllocBase::VerifyEnabled),
                   cl::Hidden, cl::desc("Verify during register allocation"));

const char RegAllocBase::TimerGroupName[] = "regalloc";
const char RegAllocBase::TimerGroupDescription[] = "Register Allocation";
bool RegAllocBase::VerifyEnabled = false;

static cl::opt<bool> PrintCost(
    "print-cost", cl::Hidden,
    cl::desc("Print cost"),
    cl::init(false));

//===----------------------------------------------------------------------===//
//                         RegAllocBase Implementation
//===----------------------------------------------------------------------===//

// Pin the vtable to this file.
void RegAllocBase::anchor() {}

void RegAllocBase::init(VirtRegMap &vrm,
                        LiveIntervals &lis,
                        LiveRegMatrix &mat) {
  TRI = &vrm.getTargetRegInfo();
  MRI = &vrm.getRegInfo();
  VRM = &vrm;
  LIS = &lis;
  Matrix = &mat;
  MRI->freezeReservedRegs(vrm.getMachineFunction());
  RegClassInfo.runOnMachineFunction(vrm.getMachineFunction());
}

// Visit all the live registers. If they are already assigned to a physical
// register, unify them with the corresponding LiveIntervalUnion, otherwise push
// them on the priority queue for later assignment.
void RegAllocBase::seedLiveRegs() {
  NamedRegionTimer T("seed", "Seed Live Regs", TimerGroupName,
                     TimerGroupDescription, TimePassesIsEnabled);
  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    Register Reg = Register::index2VirtReg(i);
    if (MRI->reg_nodbg_empty(Reg))
      continue;
    enqueue(&LIS->getInterval(Reg));
  }
}

static int getRound(std::string filename) {
  // return __INT_MAX__;
  std::ifstream f(filename);
  if (f.good()) {
    // errs() << "GOOD\n";
    std::string r;
    getline(f, r);
    return std::stoi(r);
  } else {
    errs() << "BAD\n";
    return __INT_MAX__;
  }
}

void RegAllocBase::printCost(std::string msg) {
  if (!PrintCost) return;

  const char* filename = "/home/ywshin/cost.txt";
  std::error_code EC;
  raw_fd_ostream OS(filename, EC, sys::fs::OF_Append);

  MachineFunction &mf = VRM->getMachineFunction();
  std::string moduleId = mf.getFunction().getParent()->getModuleIdentifier();
  std::string functionName = mf.getName().str();
  std::vector<std::string> moduleIds{"df-scan.c", "lcm.c", "ldecod_src/quant.c", "ldecod_src/erc_do_i.c", "x264_src/encoder/analyse.c", "x264_src/encoder/analyse.c"};
  std::vector<std::string> functionNames{"df_uses_record", "pre_edge_lcm", "CalculateQuant4x4Param", "ercPixConcealIMB", "x264_weight_plane_analyse", "x264_slicetype_frame_cost"};

  for (int i = 0; i < moduleIds.size(); i++) {
    if (!moduleIds[i].compare(moduleId) && !functionNames[i].compare(functionName)) {
      OS << msg << "\n";
    }
  }
  // OS << msg << "\n";
}

void RegAllocBase::printCost(float cost) {
  if (!PrintCost) return;

  const char* filename = "/home/ywshin/cost.txt";
  std::error_code EC;
  raw_fd_ostream OS(filename, EC, sys::fs::OF_Append);

  MachineFunction &mf = VRM->getMachineFunction();
  std::string moduleId = mf.getFunction().getParent()->getModuleIdentifier();
  std::string functionName = mf.getName().str();
  std::vector<std::string> moduleIds{"df-scan.c", "lcm.c", "ldecod_src/quant.c", "ldecod_src/erc_do_i.c", "x264_src/encoder/analyse.c", "x264_src/encoder/analyse.c"};
  std::vector<std::string> functionNames{"df_uses_record", "pre_edge_lcm", "CalculateQuant4x4Param", "ercPixConcealIMB", "x264_weight_plane_analyse", "x264_slicetype_frame_cost"};

  for (int i = 0; i < moduleIds.size(); i++) {
    if (!moduleIds[i].compare(moduleId) && !functionNames[i].compare(functionName)) {
      if (cost < 0) {
        OS << "<END OF FUNCTION:" << functionName << "> \n";
        return;
      }
      OS << cost << "\n";
    }
  }
  // if (cost < 0) {
  //   OS << "<END OF FUNCTION:" << functionName << "> \n";
  //   return;
  // }
  // OS << cost << "\n";
}

// Top-level driver to manage the queue of unassigned VirtRegs and call the
// selectOrSplit implementation.
void RegAllocBase::allocatePhysRegs() {
  seedLiveRegs();
  // errs() << "0:" << calcPotentialSpillCosts() << "\n";
  printCost(calcPotentialSpillCosts());
  // errs() << calcPotentialSpillCosts() << "\n";
  if (MinSpillCost >= calcPotentialSpillCosts() * Hysteresis) {
    MinSpillCost = calcPotentialSpillCosts();
  }
  if (MinThresholdCost >= calcPotentialSpillCosts() * Hysteresis) {
    MinThresholdCost = calcPotentialSpillCosts();
  }

  MachineFunction &mf = VRM->getMachineFunction();
  std::string filename = mf.getFunction().getParent()->getModuleIdentifier() + "." + std::to_string(mf.getFunctionNumber()) + ".txt";
  errs() << "FILENAME:" << filename << "\n";

  Limit = getRound(filename);
  // Continue assigning vregs one at a time to available physical registers.
  while (LiveInterval *VirtReg = dequeue()) {
    printCost("dequeue");
    printCost(calcPotentialSpillCosts());
    assert(!VRM->hasPhys(VirtReg->reg()) && "Register already assigned");
    Round++;
    if (!Fallback && MinRound > Limit) {
      report_fatal_error("MinRound has passed Limit");
    }

    // Unused registers can appear when the spiller coalesces snippets.
    if (MRI->reg_nodbg_empty(VirtReg->reg())) {
      LLVM_DEBUG(dbgs() << "Dropping unused " << *VirtReg << '\n');
      aboutToRemoveInterval(*VirtReg);
      LIS->removeInterval(VirtReg->reg());
      // errs() << "1:" << calcPotentialSpillCosts() << "\n";
      if (!Fallback && MinSpillCost >= calcPotentialSpillCosts() * Hysteresis) {
        MinSpillCost = calcPotentialSpillCosts();
        MinRound = Round;
      }
      if (!Fallback && MinThresholdCost >= calcPotentialSpillCosts() * Hysteresis) {
        if (Threshold > 0) {
          MinThresholdCost = calcPotentialSpillCosts();
          MinThresholdRound = Round;
          Threshold = LookaheadThreshold;
        }
      } else if (!Fallback && MinThresholdCost < calcPotentialSpillCosts() * Hysteresis) {
        Threshold--;
      }
      printCost(calcPotentialSpillCosts());
      // errs() << calcPotentialSpillCosts() << "\n";
      continue;
    }

    // Invalidate all interference queries, live ranges could have changed.
    Matrix->invalidateVirtRegs();

    // selectOrSplit requests the allocator to return an available physical
    // register if possible and populate a list of new live intervals that
    // result from splitting.
    LLVM_DEBUG(dbgs() << "\nselectOrSplit "
                      << TRI->getRegClassName(MRI->getRegClass(VirtReg->reg()))
                      << ':' << *VirtReg << " w=" << VirtReg->weight() << '\n');

    using VirtRegVec = SmallVector<Register, 4>;

    VirtRegVec SplitVRegs;
    MCRegister AvailablePhysReg = selectOrSplit(*VirtReg, SplitVRegs);

    if (AvailablePhysReg == ~0u) {
      // selectOrSplit failed to find a register!
      // Probably caused by an inline asm.
      MachineInstr *MI = nullptr;
      for (MachineRegisterInfo::reg_instr_iterator
               I = MRI->reg_instr_begin(VirtReg->reg()),
               E = MRI->reg_instr_end();
           I != E;) {
        MI = &*(I++);
        if (MI->isInlineAsm())
          break;
      }

      const TargetRegisterClass *RC = MRI->getRegClass(VirtReg->reg());
      ArrayRef<MCPhysReg> AllocOrder = RegClassInfo.getOrder(RC);
      if (AllocOrder.empty())
        report_fatal_error("no registers from class available to allocate");
      else if (MI && MI->isInlineAsm()) {
        MI->emitError("inline assembly requires more registers than available");
      } else if (MI) {
        LLVMContext &Context =
            MI->getParent()->getParent()->getMMI().getModule()->getContext();
        Context.emitError("ran out of registers during register allocation");
      } else {
        report_fatal_error("ran out of registers during register allocation");
      }

      // Keep going after reporting the error.
      VRM->assignVirt2Phys(VirtReg->reg(), AllocOrder.front());
      // errs() << "2:" << calcPotentialSpillCosts() << "\n";
      if (!Fallback && MinSpillCost >= calcPotentialSpillCosts() * Hysteresis) {
        MinSpillCost = calcPotentialSpillCosts();
        MinRound = Round;
      }
      if (!Fallback && MinThresholdCost >= calcPotentialSpillCosts() * Hysteresis) {
        if (Threshold > 0) {
          MinThresholdCost = calcPotentialSpillCosts();
          MinThresholdRound = Round;
          Threshold = LookaheadThreshold;
        }
      } else if (!Fallback && MinThresholdCost < calcPotentialSpillCosts() * Hysteresis) {
        Threshold--;
      }
      printCost(calcPotentialSpillCosts());
      // errs() << calcPotentialSpillCosts() << "\n";
      continue;
    }

    if (AvailablePhysReg) {
      printCost("assign");
      printCost(calcPotentialSpillCosts());
      Matrix->assign(*VirtReg, AvailablePhysReg);
    }

    if (SplitVRegs.size() > 0) printCost(calcPotentialSpillCosts());

    for (Register Reg : SplitVRegs) {
      assert(LIS->hasInterval(Reg));

      LiveInterval *SplitVirtReg = &LIS->getInterval(Reg);
      assert(!VRM->hasPhys(SplitVirtReg->reg()) && "Register already assigned");
      if (MRI->reg_nodbg_empty(SplitVirtReg->reg())) {
        assert(SplitVirtReg->empty() && "Non-empty but used interval");
        LLVM_DEBUG(dbgs() << "not queueing unused  " << *SplitVirtReg << '\n');
        aboutToRemoveInterval(*SplitVirtReg);
        LIS->removeInterval(SplitVirtReg->reg());
        // errs() << "3:" << calcPotentialSpillCosts() << "\n";
        continue;
      }
      LLVM_DEBUG(dbgs() << "queuing new interval: " << *SplitVirtReg << "\n");
      assert(Register::isVirtualRegister(SplitVirtReg->reg()) &&
             "expect split value in virtual register");
      enqueue(SplitVirtReg);
      // errs() << "s:" << calcPotentialSpillCosts() << "\n";
      ++NumNewQueued;
    }
    // errs() << "4:" << calcPotentialSpillCosts() << "\n";
    if (!Fallback && MinSpillCost >= calcPotentialSpillCosts() * Hysteresis) {
      MinSpillCost = calcPotentialSpillCosts();
      MinRound = Round;
    }
    if (!Fallback && MinThresholdCost >= calcPotentialSpillCosts() * Hysteresis) {
      if (Threshold > 0) {
        MinThresholdCost = calcPotentialSpillCosts();
        MinThresholdRound = Round;
        Threshold = LookaheadThreshold;
      }
    } else if (!Fallback && MinThresholdCost < calcPotentialSpillCosts() * Hysteresis) {
      Threshold--;
    }
    printCost("enqueue");
    printCost(calcPotentialSpillCosts());
    // errs() << calcPotentialSpillCosts() << "\n";
  }
  // errs() << "5:" << calcPotentialSpillCosts() << "\n";
  if (!Fallback && MinSpillCost >= calcPotentialSpillCosts() * Hysteresis) {
    MinSpillCost = calcPotentialSpillCosts();
    MinRound = Round;
  }
  if (!Fallback && MinThresholdCost >= calcPotentialSpillCosts() * Hysteresis) {
    if (Threshold > 0) {
      MinThresholdCost = calcPotentialSpillCosts();
      MinThresholdRound = Round;
      Threshold = LookaheadThreshold;
    }
  } else if (!Fallback && MinThresholdCost < calcPotentialSpillCosts() * Hysteresis) {
    Threshold--;
  }

  if (MinSpillCost * Hysteresis > calcPotentialSpillCosts()) {
    errs() << "XXXXX:" << MinSpillCost << "\n";
    errs() << "XXXXX:" << calcPotentialSpillCosts() << "\n";
    // report_fatal_error("MinSpillCost * Hysteresis > calcPotentialSpillCosts() at the end of register allocation");
  }
  // printCost(calcPotentialSpillCosts());
  // errs() << calcPotentialSpillCosts() << "\n";
}

void RegAllocBase::postOptimization() {
  spiller().postOptimization();
  for (auto DeadInst : DeadRemats) {
    LIS->RemoveMachineInstrFromMaps(*DeadInst);
    DeadInst->eraseFromParent();
  }
  DeadRemats.clear();
}
