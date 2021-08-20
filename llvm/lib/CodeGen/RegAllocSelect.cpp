//===-- RegAllocBasic.cpp - Basic Register Allocator ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RABasic function pass, which provides a minimal
// implementation of the basic register allocator.
//
//===----------------------------------------------------------------------===//

#include "AllocationOrder.h"
#include "LiveDebugVariables.h"
#include "RegAllocBase.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/Spiller.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallSet.h"
#include <cstdlib>
#include <queue>
#include <fstream>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

static cl::opt<bool> EnableRASelect(
    "enable-ra-select", cl::Hidden,
    cl::desc("Enable RA Select"),
    cl::init(false));

namespace {

class RASelect : public MachineFunctionPass {
private:
  // context
  MachineFunction *MF;

public:
  RASelect();

  /// RASelect analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Perform selecting register allocator.
  bool runOnMachineFunction(MachineFunction &mf) override;

  static char ID;
};

char RASelect::ID = 0;

} // end anonymous namespace

char &llvm::RASelectID = RASelect::ID;

// INITIALIZE_PASS_BEGIN(RASelect, "regallocselect", "Register Allocator Selector",
//                       false, false)
// INITIALIZE_PASS_END(RASelect, "regallocselect", "Register Allocator Selector", false,
//                     false)

RASelect::RASelect(): MachineFunctionPass(ID) {
}

void RASelect::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool RASelect::runOnMachineFunction(MachineFunction &mf) {
  LLVM_DEBUG(dbgs() << "********** REGISTER ALLOCATIOR SELECTOR **********\n"
                    << "********** Function: " << mf.getName() << '\n');
  MF = &mf;
  MF->RAOption = -1;

  std::string prefix = MF->getFunction().getParent()->getModuleIdentifier() + "." + std::to_string(MF->getFunctionNumber());

  std::ifstream f(prefix + ".greedy.txt");
  if (!f.good()) {
    // errs() << "PASS\n";
    return true;
  }

  // std::vector<std::string> allocators = {
  //   ".fast.txt",
  //   ".basic.txt",
  //   ".greedy.txt",
  //   ".pbqp.txt",
  //   ".greedy-skip-global.txt",
  //   ".pbqp-global.txt",
  //   ".pbqp-local.txt",
  //   ".pbqp-skip-global-local.txt"
  // };
  std::vector<std::string> allocators = {
    ".fast.txt",
    ".basic.txt",
    ".greedy.txt",
    ".pbqp.txt"
  };
  float min_cost = std::numeric_limits<float>::infinity();
  int min_index = -1;

  for (int i = 0; i < allocators.size(); i++) {
    std::ifstream f(prefix + allocators[i]);
    if (!f.good()) continue;

    std::string c;
    getline(f, c);
    if (std::stof(c) < min_cost) {
      min_cost = std::stof(c);
      min_index = i;
    }
  }

  // TODO: currently use Greedy instead of Fast
  if (min_index < 0 || min_index == 0) min_index = 2;

  if (EnableRASelect)
    MF->RAOption = min_index;
  else
    MF->RAOption = -1;

  // errs() << min_index << "\n";

  return true;
}

FunctionPass* llvm::createRegisterAllocatorSelector()
{
  return new RASelect();
}
