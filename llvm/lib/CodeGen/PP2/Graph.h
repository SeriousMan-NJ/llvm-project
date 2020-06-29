//===- Graph.h - PP2 Graph -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PP2 Graph class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PP2_GRAPH_H
#define LLVM_CODEGEN_PP2_GRAPH_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <vector>
#include <map>
#include <iostream>

namespace llvm {

class MachineFunction;
class LiveIntervals;

namespace PP2 {

  class Graph {
  private:
    class Node {
    public:
    using NodeId = unsigned;
      Node(NodeId NId, unsigned VReg)
        : NId(NId), VReg(VReg) {};

      NodeId NId;
      unsigned VReg;
      std::vector<NodeId> adjNodes;
    };

  public:
    using NodeId = unsigned;
    using NodeVector = std::vector<Node>;
    using VRegToNIdMap = std::map<unsigned, NodeId>;

    Graph(MachineFunction &MF, LiveIntervals &LIS, LiveRegMatrix& Matrix, VirtRegMap& VRM)
      : MF(MF), LIS(LIS), nextNodeId(0), Matrix(Matrix), VRM(VRM) {};

    NodeId addNodeForVReg(unsigned VReg) {
      Nodes.push_back(Node(nextNodeId, VReg));
      return nextNodeId++;
    }

    void setNodeIdForVReg(unsigned VReg, NodeId NId) {
      VRegToNId[VReg] = NId;
    }

    void addEdgeForVReg(unsigned VReg1, unsigned VReg2) {
      Node& N1 = Nodes[VRegToNId[VReg1]];
      N1.adjNodes.push_back(VRegToNId[VReg2]);
    }

    void dump(raw_ostream &OS) const;
    void exportToNetworkx(raw_ostream &OS) const;

    MachineFunction &MF;
    LiveIntervals &LIS;
    NodeId nextNodeId;
    LiveRegMatrix& Matrix;
    VirtRegMap& VRM;
    NodeVector Nodes;
    VRegToNIdMap VRegToNId;
  };
} // end namespace PP2
} // end namespace llvm

#endif // LLVM_CODEGEN_PP2_GRAPH_HPP
