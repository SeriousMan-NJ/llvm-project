//===-- PP2Dummy.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass collects the count of all instructions and reports them
//
//===----------------------------------------------------------------------===//

#include "AllocationOrder.h"
#include "PP2/Graph.h"
#include "Spiller.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/InitializePasses.h"
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
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
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
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "RegAllocGreedy.h"
#include "RegAllocBasic.h"
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
#include <fstream>
using namespace llvm;

#define DEBUG_TYPE "regalloc"

static RegisterRegAlloc PP2RegAlloc("pp2", "PP2 register allocator",
                                      createPP2DummyPass);

namespace pp2 {
// trim from start (in place)
inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}
}

#ifndef NDEBUG
static cl::opt<bool>
PP2DummyDumpGraphs("pp2-dummy-dump-graph",
               cl::desc("Dump interference graph"),
               cl::init(false), cl::NotHidden);
#endif

#ifndef NDEBUG
static cl::opt<bool>
PP2DummyExportGraphs("pp2-dummy-export-graph",
               cl::desc("Export interference graph"),
               cl::init(false), cl::NotHidden);
#endif

#ifndef NDEBUG
static cl::opt<bool>
PP2DummyViewCFG("pp2-dummy-view-cfg",
               cl::desc("View CFG"),
               cl::init(false), cl::NotHidden);
#endif

#ifndef NDEBUG
static cl::opt<bool>
PP2DummySkip("pp2-skip",
               cl::desc("Skip MIS coloring"),
               cl::init(false), cl::NotHidden);
#endif

#ifndef NDEBUG
static cl::opt<std::string>
PP2DummyRegAlloc("pp2-regalloc",
               cl::desc("Select register allocator for residual graph"),
               cl::init("greedy"), cl::NotHidden);
#endif

namespace {
  class PP2Dummy : public MachineFunctionPass {
  public:
    static char ID; // Pass identification, replacement for typeid
    PP2Dummy() : MachineFunctionPass(ID) { }

    /// Return the pass name.
    StringRef getPassName() const override { return "PP2 Register Allocator"; }

    void getAnalysisUsage(AnalysisUsage &au) const override;
    bool runOnMachineFunction(MachineFunction &F) override;

  protected:
    RegisterClassInfo RegClassInfo;

  private:
    using RegSet = std::set<unsigned>;

    RegSet VRegsToAlloc, EmptyIntervalVRegs;
    SmallPtrSet<MachineInstr *, 32> DeadRemats;

    /// Finds the initial set of vreg intervals to allocate.
    void findVRegIntervalsToAlloc(const MachineFunction &F, LiveIntervals &LIS);

    /// Constructs an initial graph.
    void initializeGraph(PP2::Graph &G, VirtRegMap &VRM, Spiller &VRegSpiller);

    /// Spill the given VReg.
    void spillVReg(unsigned VReg, SmallVectorImpl<unsigned> &NewIntervals,
                   MachineFunction &MF, LiveIntervals &LIS, VirtRegMap &VRM,
                   Spiller &VRegSpiller);

    /// Coloring
    void coloringMIS(PP2::Graph &G, std::string ExportGraphFileName);
    void coloring(PP2::Graph &G, std::string ExportGraphFileName);

    LiveIntervals* LIS;
    VirtRegMap* VRM;
    LiveRegMatrix* Matrix;
    SlotIndexes* Indexes;
    MachineBlockFrequencyInfo* MBFI;
    MachineDominatorTree* DomTree;
    MachineOptimizationRemarkEmitter* ORE;
    MachineLoopInfo* Loops;
    EdgeBundles* Bundles;
    SpillPlacement* SpillPlacer;
    LiveDebugVariables* DebugVars;
    AAResults* AA;
    Spiller* spiller;
    std::unique_ptr<Spiller> VRegSpiller;
  };
}

void PP2Dummy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<MachineBlockFrequencyInfo>();
  AU.addPreserved<MachineBlockFrequencyInfo>();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addRequired<LiveIntervals>();
  AU.addPreserved<LiveIntervals>();
  AU.addRequired<SlotIndexes>();
  AU.addPreserved<SlotIndexes>();
  AU.addRequired<LiveDebugVariables>();
  AU.addPreserved<LiveDebugVariables>();
  AU.addRequired<LiveStacks>();
  AU.addPreserved<LiveStacks>();
  AU.addRequired<MachineDominatorTree>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addRequired<MachineLoopInfo>();
  AU.addPreserved<MachineLoopInfo>();
  AU.addRequired<VirtRegMap>();
  AU.addPreserved<VirtRegMap>();
  AU.addRequired<LiveRegMatrix>();
  AU.addPreserved<LiveRegMatrix>();
  AU.addRequired<EdgeBundles>();
  AU.addRequired<SpillPlacement>();
  AU.addRequired<MachineOptimizationRemarkEmitterPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

char PP2Dummy::ID = 0;

FunctionPass *llvm::createPP2DummyPass() { return new PP2Dummy(); }

void PP2Dummy::findVRegIntervalsToAlloc(const MachineFunction &MF,
                                          LiveIntervals &LIS) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Iterate over all live ranges.
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    unsigned Reg = Register::index2VirtReg(I);
    if (MRI.reg_nodbg_empty(Reg))
      continue;
    VRegsToAlloc.insert(Reg);
  }
}

void PP2Dummy::initializeGraph(PP2::Graph &G, VirtRegMap &VRM,
                                        Spiller &VRegSpiller) {
  for (auto VReg : VRegsToAlloc) {
    // Move empty intervals to the EmptyIntervalVReg set.
    if (LIS->getInterval(VReg).empty()) {
      EmptyIntervalVRegs.insert(VReg);
      VRegsToAlloc.erase(VReg);
      continue;
    }

    PP2::Graph::NodeId NId = G.addNodeForVReg(VReg);
    G.setNodeIdForVReg(VReg, NId);
  }

  for (auto VReg1 : VRegsToAlloc) {
    for (auto VReg2 : VRegsToAlloc) {
      if (VReg1 != VReg2 && LIS->getInterval(VReg1).overlaps(LIS->getInterval(VReg2))) {
        G.addEdgeForVReg(VReg1, VReg2);
      }
    }
  }
}

void PP2::Graph::dump(raw_ostream &OS) const {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
  for (auto N : Nodes) {
    OS << N.NId << " (" << printReg(N.VReg, TRI) << ")" << ": ";
    for (auto AdjN : N.adjNodes) {
      OS << AdjN << " ";
    }
    OS << "\n";
  }
}

void PP2::Graph::exportToNetworkx(raw_ostream &OS) const {
  for (auto N : Nodes) {
    OS << N.NId << " ";
    for (auto AdjN : N.adjNodes) {
      OS << AdjN << " ";
    }
    OS << "\n";
  }
}

void PP2Dummy::coloring(PP2::Graph &G, std::string ExportGraphFileName) {
  if (!PP2DummySkip) {
    coloringMIS(G, ExportGraphFileName);
  }
  if (!PP2DummyRegAlloc.compare("greedy")) {
    (new RAGreedy())->runOnMachineFunctionCustom(G.MF, *VRM, *LIS, *Matrix, Indexes, MBFI, DomTree, ORE, Loops, Bundles, SpillPlacer, DebugVars, AA, spiller);
  } else if (!PP2DummyRegAlloc.compare("basic")) {
    (new RABasic())->runOnMachineFunctionCustom(G.MF, *VRM, *LIS, *Matrix, Loops, MBFI, spiller);
  } else {

  }
}

void PP2Dummy::coloringMIS(PP2::Graph &G, std::string ExportGraphFileName) {
  const MachineRegisterInfo &MRI = G.MF.getRegInfo();
  const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();

  std::ifstream mvc_graph_file(ExportGraphFileName);
  assert (mvc_graph_file.is_open() && "MVC Graph file cannot be open!");

  std::string vertices;
  getline(mvc_graph_file, vertices);
  mvc_graph_file.close();

  pp2::trim(vertices);
  std::map<unsigned, bool> m;
  std::stringstream ss(vertices);
  unsigned idx;
  while (ss >> idx) {
    m[idx] = true;
  }

  for (auto const& N : G.Nodes) {
    if (m.count(N.NId) == 0) {
      AllocationOrder Order(N.VReg, *VRM, RegClassInfo, Matrix);
      unsigned PhysReg = Order.next();
      errs() << "[PP2] " << printReg(PhysReg, TRI) << " -> ";

      if (Matrix->checkInterference(LIS->getInterval(N.VReg), PhysReg) == LiveRegMatrix::IK_Free) {
        Matrix->assign(LIS->getInterval(N.VReg), PhysReg);
        errs() << printReg(N.VReg, TRI) << "\n";
      } else {
        errs() << "Failed to allocate VReg: " << printReg(N.VReg, TRI) << "\n";
      }
    }
  }
  Matrix->invalidateVirtRegs();
}

bool PP2Dummy::runOnMachineFunction(MachineFunction &MF) {
#ifndef NDEBUG
  if (PP2DummyViewCFG)
    MF.viewCFG();
#endif

  LIS = &getAnalysis<LiveIntervals>();
  VRM = &getAnalysis<VirtRegMap>();
  Matrix = &getAnalysis<LiveRegMatrix>();

  Indexes = &getAnalysis<SlotIndexes>();
  MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
  DomTree = &getAnalysis<MachineDominatorTree>();
  ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();
  Loops = &getAnalysis<MachineLoopInfo>();
  Bundles = &getAnalysis<EdgeBundles>();
  SpillPlacer = &getAnalysis<SpillPlacement>();
  DebugVars = &getAnalysis<LiveDebugVariables>();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  spiller = createInlineSpiller(*this, MF, *VRM);
  // VRegSpiller.reset(spiller);
  MF.getRegInfo().freezeReservedRegs(MF);
  RegClassInfo.runOnMachineFunction(VRM->getMachineFunction());

  errs() << "[PP2] Dummy start!\n";
  errs() << "[PP2] Current function: " << MF.getFunction().getName().str() << "\n";

  // Find the vreg intervals in need of allocation.
  findVRegIntervalsToAlloc(MF, *LIS);

#ifndef NDEBUG
  const Function &F = MF.getFunction();
  std::string FullyQualifiedName =
    F.getParent()->getModuleIdentifier() + "." + F.getName().str();
#endif

  if (!VRegsToAlloc.empty()) {
    PP2::Graph G(MF, *LIS, *Matrix, *VRM);
    initializeGraph(G, *VRM, *VRegSpiller);

#ifndef NDEBUG
  if (PP2DummyDumpGraphs) {
    std::string GraphFileName = FullyQualifiedName + ".dump.pp2graph";
    std::error_code EC;
    raw_fd_ostream OS(GraphFileName, EC, sys::fs::OF_Text);
    LLVM_DEBUG(dbgs() << "Dumping graph to \""
                      << GraphFileName << "\"\n");
    G.dump(OS);
  }
  if (PP2DummyExportGraphs) {
    std::string GraphFileName = FullyQualifiedName + ".export.pp2graph";
    std::error_code EC;
    raw_fd_ostream OS(GraphFileName, EC, sys::fs::OF_Text);
    LLVM_DEBUG(dbgs() << "Exporting graph to \""
                      << GraphFileName << "\"\n");
    G.exportToNetworkx(OS);
  }
#endif
  std::string ExportGraphFileName = FullyQualifiedName + ".export.pp2graph.clr";
  coloring(G, ExportGraphFileName);
  }

  VRegsToAlloc.clear();
  EmptyIntervalVRegs.clear();

  errs() << "[PP2] Dummy end!\n";

  return true;
}
