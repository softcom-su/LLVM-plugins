#ifndef LLVM_TUTOR_TRIVIAL_CONTEXT_H
#define LLVM_TUTOR_TRIVIAL_CONTEXT_H

#include "DispatcherContext.h"
#include "llvm/IR/Value.h"
#include <vector>
#include <cstdint>

struct TrivialContext : DispatcherContext {
    llvm::Value *statePtr = nullptr;

    std::vector<uint64_t> caseValues;

    uint64_t getCaseValue(unsigned i) const override {
        if (i < caseValues.size()) return caseValues[i];
        return i;
    }
};

#endif