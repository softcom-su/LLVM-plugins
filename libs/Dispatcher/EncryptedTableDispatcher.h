#ifndef LLVM_TUTOR_ENCRYPTED_TABLE_DISPATCHER_H
#define LLVM_TUTOR_ENCRYPTED_TABLE_DISPATCHER_H

#include "Dispatcher.h"

struct EncryptedTableDispatcher : Dispatcher {
    std::unique_ptr<DispatcherContext> createContext() override;

    void initializeContext(
        DispatcherContext &ctx,
        llvm::Function *F,
        const std::vector<llvm::BasicBlock *> &BBs) override;

    llvm::Value *computeIndex(
        llvm::IRBuilder<> &B,
        DispatcherContext &ctx) override;

    void updateState(
        llvm::IRBuilder<> &B,
        DispatcherContext &ctx,
        unsigned targetState,
        int sourceState = -1) override;
};

#endif