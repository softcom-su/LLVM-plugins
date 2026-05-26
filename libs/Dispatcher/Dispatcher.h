#ifndef LLVM_TUTOR_DISPATCHER_H
#define LLVM_TUTOR_DISPATCHER_H

#include "llvm/IR/IRBuilder.h"
#include "DispatcherContext.h"
#include <vector>

enum class DispatcherType {
    Trivial,
    MinimalT,
    KlimovShamir,
    EncryptedTable
};

struct Dispatcher {
    virtual ~Dispatcher() {}

    // Allocate a dispatcher-specific context
    virtual std::unique_ptr<DispatcherContext> createContext() = 0;

    // Allocate state variables and precompute per-state data. Called once
    // per function, before any computeIndex/updateState. Requires
    // ctx.totalStates to be set
    virtual void initializeContext(
        DispatcherContext &ctx,
        llvm::Function *F,
        const std::vector<llvm::BasicBlock*> &BBs) = 0;

    // Emit IR inside the dispatcher block: returns the value used as the
    // switch index, possibly mutating the stored state
    virtual llvm::Value *computeIndex(
        llvm::IRBuilder<> &B,
        DispatcherContext &ctx) = 0;

    // Emit IR that prepares the state so that the next computeIndex returns
    // the case value of targetState. sourceState = -1 means initial
    // transition from the function entry
    virtual void updateState(
        llvm::IRBuilder<> &B,
        DispatcherContext &ctx,
        unsigned targetState,
        int sourceState = -1) = 0;
};

#endif