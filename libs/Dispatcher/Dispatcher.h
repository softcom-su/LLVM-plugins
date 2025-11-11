#ifndef LLVM_TUTOR_DISPATCHER_H
#define LLVM_TUTOR_DISPATCHER_H

#include "llvm/IR/IRBuilder.h"
#include "DispatcherContext.h"

enum class DispatcherType {
  Trivial,
  MinimalT,
  KlimovShamir,
  EncryptedTable
};

struct Dispatcher {
  virtual ~Dispatcher() {}

  virtual llvm::Value *computeIndex(
      llvm::IRBuilder<> &B,
      DispatcherContext &ctx) = 0;

  virtual void updateState(
      llvm::IRBuilder<> &B,
      DispatcherContext &ctx,
      unsigned blockIncrementIndex) = 0;

  virtual unsigned requiredStates() const = 0;
};

#endif