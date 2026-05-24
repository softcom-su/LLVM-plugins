#ifndef LLVM_TUTOR_DISPATCHER_CONTEXT_H
#define LLVM_TUTOR_DISPATCHER_CONTEXT_H

#include <cstdint>

struct DispatcherContext {
    virtual ~DispatcherContext() = default;

    unsigned totalStates = 0;

    virtual uint64_t getCaseValue(unsigned stateIndex) const {
        return stateIndex;
    }
};

#endif