#ifndef LLVM_TUTOR_DISPATCHER_CONTEXT_H
#define LLVM_TUTOR_DISPATCHER_CONTEXT_H

#include <vector>
#include "llvm/IR/Value.h"

struct DispatcherContext {
  std::vector<llvm::Value *> qPtrs;
  int m = 4;
  std::vector<std::vector<int64_t>> blockIncrements;
};

#endif