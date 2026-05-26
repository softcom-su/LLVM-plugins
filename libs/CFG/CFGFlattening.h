#pragma once

#include <random>
#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Passes/PassPlugin.h"

#include "DispatcherContext.h"
#include "Dispatcher.h"

using namespace llvm;

struct CFGFlatteningPlugin : public llvm::PassInfoMixin<CFGFlatteningPlugin> {
    CFGFlatteningPlugin(std::string DispatcherName = "trivial", int M = 4);

    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
    StringRef DispatcherFailureStrName = "DispFailString";
    std::string dispatcherName;
    int m;

    void FlatFunction(Function *F, Constant *DispFailureStr);
    BasicBlock *CreateDefaultBlock(Function *F, Constant *DispFailureStr);

    void ChangeUnconditionalBranch(
        Dispatcher &disp, BasicBlock *BB, BasicBlock *DispBB,
        DispatcherContext &ctx,
        int targetState, int fakeState,
        int sourceState);

    void ChangeConditionalBranch(
        Dispatcher &disp, BasicBlock *BB, BasicBlock *DispBB,
        DispatcherContext &ctx,
        int stateTrueTarget, int stateFalseTarget,
        int sourceState);

    BasicBlock *CreateTermForCondition(
        Dispatcher &disp, Function *F, BasicBlock *InsertBefore,
        BasicBlock *DispBB, DispatcherContext &ctx,
        int targetState, int sourceState = -1);

    void ChangeTarget(BasicBlock *BB, BasicBlock *Target);

    int IndexOfBBs(const std::vector<BasicBlock *> &arr, BasicBlock *element);

    BasicBlock *generateDeadBasicBlock(
        Dispatcher &disp, Function *F, BasicBlock *Basis, BasicBlock *DispBB,
        DispatcherContext &ctx, int state, int sourceState);

    void generateBasicBlockContent(BasicBlock *BB);
};
