#include "TrivialDispatcher.h"
#include "DispatcherFactory.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"

#include <cassert>

using namespace llvm;

Value *TrivialDispatcher::computeIndex(IRBuilder<> &B, DispatcherContext &ctx) {
  assert(ctx.qPtrs.size() == 1 && "Trivial dispatcher uses exactly one state");

  Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.qPtrs[0]);
  return q1;
}

void TrivialDispatcher::updateState(IRBuilder<> &B, DispatcherContext &ctx, unsigned blockIncrementIndex) {
  assert(ctx.qPtrs.size() == 1 && "Trivial dispatcher uses exactly one state");

  B.CreateStore(B.getInt64(blockIncrementIndex), ctx.qPtrs[0]);
}

namespace {
bool registered = [] {
  DispatcherRegistry::instance().registerDispatcher(
      DispatcherType::Trivial,
      [] { return std::make_unique<TrivialDispatcher>(); });
  return true;
}();
}