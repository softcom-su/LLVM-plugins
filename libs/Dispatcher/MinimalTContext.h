#ifndef LLVM_TUTOR_MINIMAL_T_CONTEXT_H
#define LLVM_TUTOR_MINIMAL_T_CONTEXT_H

#include "DispatcherContext.h"
#include "llvm/IR/Value.h"
#include <vector>
#include <cstdint>

struct MinimalTBlockState {
    uint64_t qTilde1;
    uint64_t qTilde2;
    uint64_t q1;
    uint64_t caseVal;
};

struct MinimalTContext : DispatcherContext {
    llvm::Value *q1Ptr = nullptr;
    llvm::Value *q2Ptr = nullptr;

    unsigned m = 4;

    std::vector<MinimalTBlockState> states;

    uint64_t getCaseValue(unsigned i) const override {
        if (i < states.size()) return states[i].caseVal;
        return i;
    }
};

#endif