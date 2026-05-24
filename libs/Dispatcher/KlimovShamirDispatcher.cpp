#include "KlimovShamirDispatcher.h"
#include "KlimovShamirContext.h"
#include "DispatcherFactory.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"

#include <random>
#include <unordered_set>
#include <cassert>

using namespace llvm;

// g({q1|q2}) = {q1 + (q1^2 OR q2) | q2}, formula 5 from the article
static inline uint64_t ks_g(uint64_t qT1, uint64_t qT2) {
    return qT1 + (qT1 * qT1 | qT2);
}

static unsigned computeM(unsigned total) {
    unsigned m = 4;
    while ((1u << m) < total) ++m;
    m += 2;
    if (m > 20) m = 20;
    return m;
}


std::unique_ptr<DispatcherContext> KlimovShamirDispatcher::createContext() {
    return std::make_unique<KlimovShamirContext>();
}

void KlimovShamirDispatcher::initializeContext(
    DispatcherContext &base,
    Function *F,
    const std::vector<BasicBlock *> &BBs)
{
    auto &ctx = static_cast<KlimovShamirContext &>(base);
    assert(ctx.totalStates > 0 && "totalStates must be set before initializeContext");

    ctx.m = computeM(ctx.totalStates);

    IRBuilder<> B(&F->getEntryBlock().front());
    ctx.q1Ptr = B.CreateAlloca(B.getInt64Ty(), nullptr, "ks.q1");
    ctx.q2Ptr = B.CreateAlloca(B.getInt64Ty(), nullptr, "ks.q2");
    B.CreateStore(B.getInt64(1), ctx.q1Ptr);
    B.CreateStore(B.getInt64(1), ctx.q2Ptr);

    std::mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
    std::unordered_set<uint64_t> usedCaseVals;
    ctx.states.resize(ctx.totalStates);

    for (unsigned i = 0; i < ctx.totalStates; ++i) {
        uint64_t qT1, qT2, q1, cv;
        int attempts = 0;
        const uint64_t shift = 64 - ctx.m;

        do {
            qT1 = rng();
            
            // Force bits 0 and 2 set (q2 is odd and q2 & 4 != 0) —
            // required by K-S single-cycle condition (Section 4, formula 5)
            qT2 = rng() | 5ULL;
            q1  = ks_g(qT1, qT2);
            cv  = q1 >> shift;

            if (++attempts > 50000) {
                ctx.m++;
                if (ctx.m > 20) ctx.m = 20;
                usedCaseVals.clear();
                ctx.states.clear();
                ctx.states.resize(ctx.totalStates);
                i = static_cast<unsigned>(-1);
                rng.seed(0xDEADBEEFCAFEBABEULL);
                break;
            }
        } while (usedCaseVals.count(cv));

        if (attempts > 50000) continue;  // restart loop with updated m/shift

        usedCaseVals.insert(cv);
        ctx.states[i] = {qT1, qT2, q1, cv};
    }
}

Value *KlimovShamirDispatcher::computeIndex(IRBuilder<> &B, DispatcherContext &base) {
    auto &ctx = static_cast<KlimovShamirContext &>(base);

    Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.q1Ptr, "ks.q1");
    Value *q2 = B.CreateLoad(B.getInt64Ty(), ctx.q2Ptr, "ks.q2");

    // g({q1|q2}) = {q1 + (q1^2 OR q2) | q2}, formula 5 from the article
    Value *q1_sq  = B.CreateMul(q1, q1, "ks.q1sq");
    Value *q1_or  = B.CreateOr(q1_sq, q2, "ks.q1or");
    Value *q1_new = B.CreateAdd(q1, q1_or, "ks.q1new");

    // f({q1|q2}) = q1 >> (64-m), formula 6 from the article
    Value *index = B.CreateLShr(q1_new, B.getInt64(64 - ctx.m), "ks.idx");

    B.CreateStore(q1_new, ctx.q1Ptr);

    return index;
}

void KlimovShamirDispatcher::updateState(
    IRBuilder<> &B,
    DispatcherContext &base,
    unsigned targetState,
    int sourceState)
{
    auto &ctx = static_cast<KlimovShamirContext &>(base);
    assert(targetState < ctx.states.size() && "target state out of range");

    uint64_t cur_q1, cur_q2;
    if (sourceState < 0) {
        cur_q1 = 1;
        cur_q2 = 1;
    } else {
        const auto &src = ctx.states[(unsigned)sourceState];
        cur_q1 = src.q1;
        cur_q2 = src.qTilde2;
    }

    const auto &tgt = ctx.states[targetState];
    uint64_t delta1 = tgt.qTilde1 - cur_q1;
    uint64_t delta2 = tgt.qTilde2 - cur_q2;

    Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.q1Ptr, "ks.q1.upd");
    Value *q2 = B.CreateLoad(B.getInt64Ty(), ctx.q2Ptr, "ks.q2.upd");

    if (delta1 != 0)
        q1 = B.CreateAdd(q1, B.getInt64((int64_t)delta1), "ks.q1.add");
    if (delta2 != 0)
        q2 = B.CreateAdd(q2, B.getInt64((int64_t)delta2), "ks.q2.add");

    B.CreateStore(q1, ctx.q1Ptr);
    B.CreateStore(q2, ctx.q2Ptr);
}

namespace {
bool registered = [] {
    DispatcherRegistry::instance().registerDispatcher(
        DispatcherType::KlimovShamir,
        [] { return std::make_unique<KlimovShamirDispatcher>(); });
    return true;
}();
}