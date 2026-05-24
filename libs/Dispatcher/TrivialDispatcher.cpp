#include "TrivialDispatcher.h"
#include "TrivialContext.h"
#include "DispatcherFactory.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"

#include <algorithm>
#include <random>

using namespace llvm;

std::unique_ptr<DispatcherContext> TrivialDispatcher::createContext() {
    return std::make_unique<TrivialContext>();
}

void TrivialDispatcher::initializeContext(
    DispatcherContext &base,
    Function *F,
    const std::vector<BasicBlock *> &BBs)
{
    auto &ctx = static_cast<TrivialContext &>(base);

    IRBuilder<> B(&F->getEntryBlock().front());
    ctx.statePtr = B.CreateAlloca(B.getInt64Ty(), nullptr, "trivial.state");
    B.CreateStore(B.getInt64(0), ctx.statePtr);

    unsigned total = (ctx.totalStates > 0) ? ctx.totalStates
                                            : (unsigned)BBs.size() + 4;
    ctx.caseValues.resize(total);
    for (unsigned i = 0; i < total; ++i)
        ctx.caseValues[i] = i;

    std::mt19937_64 rng(0xABCDEF0123456789ULL);
    std::shuffle(ctx.caseValues.begin(), ctx.caseValues.end(), rng);
}

Value *TrivialDispatcher::computeIndex(IRBuilder<> &B, DispatcherContext &base) {
    auto &ctx = static_cast<TrivialContext &>(base);
    return B.CreateLoad(B.getInt64Ty(), ctx.statePtr, "trivial.idx");
}

void TrivialDispatcher::updateState(
    IRBuilder<> &B,
    DispatcherContext &base,
    unsigned targetState,
    int /*sourceState*/)
{
    auto &ctx = static_cast<TrivialContext &>(base);
    uint64_t cv = ctx.getCaseValue(targetState);
    B.CreateStore(B.getInt64((int64_t)cv), ctx.statePtr);
}

namespace {
bool registered = [] {
    DispatcherRegistry::instance().registerDispatcher(
        DispatcherType::Trivial,
        [] { return std::make_unique<TrivialDispatcher>(); });
    return true;
}();
}