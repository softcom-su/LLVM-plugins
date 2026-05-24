#ifndef LLVM_TUTOR_KLIMOV_SHAMIR_CONTEXT_H
#define LLVM_TUTOR_KLIMOV_SHAMIR_CONTEXT_H

#include "DispatcherContext.h"
#include "llvm/IR/Value.h"
#include <vector>
#include <cstdint>

struct KSBlockState {
    uint64_t qTilde1;
    uint64_t qTilde2;
    uint64_t q1;
    uint64_t caseVal;
};

struct KlimovShamirContext : DispatcherContext {
    llvm::Value *q1Ptr = nullptr;
    llvm::Value *q2Ptr = nullptr;

    unsigned m = 4;

    std::vector<KSBlockState> states;

    uint64_t getCaseValue(unsigned i) const override {
        if (i < states.size()) return states[i].caseVal;
        return i;
    }
};

#endif