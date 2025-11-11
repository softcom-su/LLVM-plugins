#ifndef LLVM_TUTOR_MINIMAL_T_DISPATCHER_H
#define LLVM_TUTOR_MINIMAL_T_DISPATCHER_H

#include "Dispatcher.h"

struct MinimalTDispatcher : Dispatcher {
  llvm::Value *computeIndex(
      llvm::IRBuilder<> &B,
      DispatcherContext &ctx) override;

  void updateState(
      llvm::IRBuilder<> &B,
      DispatcherContext &ctx,
      unsigned blockIncrementIndex) override;

  unsigned requiredStates() const override {
    return 2;
  }
};

#endif