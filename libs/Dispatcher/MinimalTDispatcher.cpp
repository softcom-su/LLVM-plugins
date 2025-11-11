#include "MinimalTDispatcher.h"
#include "DispatcherFactory.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"

#include <cassert>
#include <random>

// Generates deterministic non-zero increments (a1, a2) for Minimal T-function state updates in basic blocks
std::vector<int64_t> pickIncrementsForBlock(unsigned blockIndex, unsigned m) {
    std::mt19937_64 rng(blockIndex * 0x9e3779b97f4a7c15ULL);
    
    int64_t a1, a2;
    do {
        a1 = (int64_t)(rng() % 10000) + 1;
        a2 = (int64_t)(rng() % 10000) + 1;
    } while (a1 == 0 || a2 == 0); // Ensure non-zero
    
    return {a1, a2};
}


llvm::Value *MinimalTDispatcher::computeIndex(
    llvm::IRBuilder<> &B,
    DispatcherContext &ctx) {
  assert(ctx.qPtrs.size() >= 2 && "MinimalT needs two state variables");

  llvm::Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.qPtrs[0]);
  llvm::Value *q2 = B.CreateLoad(B.getInt64Ty(), ctx.qPtrs[1]);

  llvm::Value *q1_new = B.CreateMul(q1, q2);
  B.CreateStore(q1_new, ctx.qPtrs[0]);
  B.CreateStore(q2, ctx.qPtrs[1]);

  uint64_t shiftAmt = 64 - (ctx.m > 0 ? ctx.m : 1);
  llvm::Value *shift = B.getInt64(shiftAmt);
  llvm::Value *index = B.CreateLShr(q1_new, shift);

  return index;
}

void MinimalTDispatcher::updateState(
    llvm::IRBuilder<> &B,
    DispatcherContext &ctx,
    unsigned blockIncrementIndex) {
  assert(ctx.qPtrs.size() >= 2 && "MinimalT needs two state variables");

  llvm::Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.qPtrs[0]);
  llvm::Value *q2 = B.CreateLoad(B.getInt64Ty(), ctx.qPtrs[1]);

  if (blockIncrementIndex >= ctx.blockIncrements.size()) {
      ctx.blockIncrements.resize(blockIncrementIndex + 1);
  }

  if (ctx.blockIncrements[blockIncrementIndex].empty()) {
      ctx.blockIncrements[blockIncrementIndex] = pickIncrementsForBlock(blockIncrementIndex, ctx.m);
  }

  auto &incs = ctx.blockIncrements[blockIncrementIndex];
  llvm::ConstantInt *a1 = llvm::ConstantInt::get(B.getInt64Ty(), incs[0]);
  llvm::ConstantInt *a2 = llvm::ConstantInt::get(B.getInt64Ty(), incs[1]);

  llvm::Value *q1_new = B.CreateAdd(q1, a1);
  llvm::Value *q2_new = B.CreateAdd(q2, a2);

  B.CreateStore(q1_new, ctx.qPtrs[0]);
  B.CreateStore(q2_new, ctx.qPtrs[1]);
}

namespace {
bool registered = [] {
  DispatcherRegistry::instance().registerDispatcher(
      DispatcherType::MinimalT,
      [] { return std::make_unique<MinimalTDispatcher>(); });
  return true;
}();
}