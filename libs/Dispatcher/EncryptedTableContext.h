#ifndef LLVM_TUTOR_ENCRYPTED_TABLE_CONTEXT_H
#define LLVM_TUTOR_ENCRYPTED_TABLE_CONTEXT_H

#include "DispatcherContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/GlobalVariable.h"
#include <vector>
#include <cstdint>

struct EncStateVals {
    uint64_t X;
    uint64_t Y;
};

struct EncryptedTableContext : DispatcherContext {
    llvm::Value *q1Ptr = nullptr;
    llvm::Value *q2Ptr = nullptr;

    uint64_t secretKey = 0;
    unsigned tableSize = 0;

    llvm::GlobalVariable *tableGV = nullptr;

    static constexpr unsigned NUM_KEY_SEEDS = 8;
    llvm::GlobalVariable *keySeedGVs[NUM_KEY_SEEDS] = {};

    std::vector<EncStateVals> states;

    uint64_t getCaseValue(unsigned i) const override {
        return i;
    }
};

#endif