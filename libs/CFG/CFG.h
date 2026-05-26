//==============================================================================
// FILE:
//    DuplicateBB.h
//
// DESCRIPTION:
//    Declares the DuplicateBB pass
//
// License: MIT
//==============================================================================
#ifndef LLVM_TUTOR_DUPLICATE_BB_H
#define LLVM_TUTOR_DUPLICATE_BB_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"

#include <map>
#include <memory>

namespace llvm {
class RandomNumberGenerator;
} // namespace llvm

//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
struct CFGPlugin : public llvm::PassInfoMixin<CFGPlugin> {
  llvm::PreservedAnalyses run(llvm::Module &F,
                              llvm::ModuleAnalysisManager &);

  using BBToSingleRIVMap =
      std::vector<std::tuple<llvm::BasicBlock *, llvm::Value *>>;

  using ValueToPhiMap = std::map<llvm::Value *, llvm::Value *>;

  void cloneBB(llvm::BasicBlock &BB, llvm::Value *ContextValue,
               ValueToPhiMap &ReMapper);

  unsigned DuplicateBBCount = 0;

  static bool isRequired() { return true; }

  std::unique_ptr<llvm::RandomNumberGenerator> pRNG;

  private:
    bool splitBasicBlock(llvm::Function &F);
    bool generateDeadBasicBlocks(llvm::Function &F);
    bool shafleBasicBlock(llvm::Function &F);
    void generateBasicBlockContent(llvm::BasicBlock *BB, llvm::BasicBlock* Prev, llvm::BasicBlock* Next);
};

#endif
