#include <random>
#include <cassert>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cstdlib>

#include "CFGFlattening.h"
#include "DispatcherFactory.h"
#include "DispatcherContext.h"

using namespace llvm;

static DispatcherType resolveDispatcherType(const std::string &name) {
    if (name == "trivial")          return DispatcherType::Trivial;
    if (name == "minimal_t")        return DispatcherType::MinimalT;
    if (name == "klimov_shamir")    return DispatcherType::KlimovShamir;
    if (name == "encrypted_table")  return DispatcherType::EncryptedTable;
    errs() << "[CFGFlattening] Unknown dispatcher '" << name
           << "', falling back to trivial\n";
    return DispatcherType::Trivial;
}

// Read the active dispatcher name: env var CFF_DISPATCHER, else "trivial"
static std::string readDispatcherName() {
    const char *env = std::getenv("CFF_DISPATCHER");
    if (env && env[0] != '\0') return std::string(env);
    return "trivial";
}


// DispatcherName intentionally ignored: the active dispatcher is selected via
// the CFF_DISPATCHER environment variable so that it can be changed without
// rebuilding the plugin. The parameter is kept only for API compatibility
CFGFlatteningPlugin::CFGFlatteningPlugin(std::string DispatcherName, int M)
    : dispatcherName(readDispatcherName()),
      m((M > 0) ? M : 4)
{
    (void)DispatcherName;
}

PreservedAnalyses CFGFlatteningPlugin::run(Module &M, ModuleAnalysisManager &MAM) {
    LLVMContext &Context = M.getContext();

    PointerType *PrintfArgTy = PointerType::getUnqual(Type::getInt8Ty(Context));
    FunctionType *PrintfTy =
        FunctionType::get(IntegerType::getInt32Ty(Context), PrintfArgTy, true);
    FunctionCallee Printf = M.getOrInsertFunction("printf", PrintfTy);
    if (Function *PF = dyn_cast<Function>(Printf.getCallee())) {
        PF->setDoesNotThrow();
        PF->addParamAttr(0, Attribute::NoCapture);
        PF->addParamAttr(0, Attribute::ReadOnly);
    }

    Constant *DispFailString =
        ConstantDataArray::getString(Context, "CFG dispatcher fail, execution terminated\n");
    Constant *DispFailStringVar =
        M.getOrInsertGlobal(DispatcherFailureStrName, DispFailString->getType());
    if (GlobalVariable *Var = dyn_cast<GlobalVariable>(DispFailStringVar))
        Var->setInitializer(DispFailString);

    for (Function &F : M)
        if (!F.isDeclaration())
            FlatFunction(&F, DispFailStringVar);

    return PreservedAnalyses::all();
}


void CFGFlatteningPlugin::FlatFunction(Function *F, Constant *DispFailureStr) {
    // --- SAFETY: skip functions that use C++ EH / personality or have invoke/landingpad ---
    // These functions are hard to transform correctly without a full EH-aware implementation.
    if (F->hasPersonalityFn()) {
        errs() << "[CFGFlattening] Skipping EH function: " << F->getName() << "\n";
        return;
    }

    BasicBlock *OrigEntry = &F->getEntryBlock();
    for (Instruction &Inst : *OrigEntry) {
        if (!(isa<AllocaInst>(Inst) || isa<StoreInst>(Inst))) {
            SplitBlock(OrigEntry, &Inst);
            break;
        }
    }

    // Demote every PHI to a stack slot before flattening. This removes the need
    // for the old PHI-aware path (PHIMap / UpdatedNodes) and avoids broken control
    // flow when a basic block ends up with a different set of predecessors after
    // the switch is inserted
    {
        std::vector<PHINode *> PHIs;
        for (BasicBlock &BB : *F) {
            for (Instruction &I : BB) {
                if (auto *PHI = dyn_cast<PHINode>(&I))
                    PHIs.push_back(PHI);
            }
        }
        for (PHINode *PHI : PHIs) {
            DemotePHIToStack(PHI, &*F->getEntryBlock().getFirstInsertionPt());
        }
    }

    std::vector<BasicBlock *> BBs;
    BBs.reserve(F->size());
    for (BasicBlock &BB : *F) {
        if (&BB == &F->getEntryBlock()) continue;
        BBs.push_back(&BB);
    }
    int numBlocks = (int)BBs.size();
    if (numBlocks == 0) return;

    int dead_code_counter = (numBlocks / 5 < 1) ? 1 : numBlocks / 5;

    // +8: safety margin; not strictly required but avoids off-by-one issues if a
    // dispatcher allocates auxiliary states
    int totalStates = numBlocks + dead_code_counter + 8;

    // After SplitBlock above, the original entry block ends with an unconditional
    // branch to the "real" first block of the function. That target is the state
    // the dispatcher must jump to first
    BasicBlock *SplitOffBlock = nullptr;
    if (auto *Br = dyn_cast<BranchInst>(OrigEntry->getTerminator()))
        if (!Br->isConditional())
            SplitOffBlock = Br->getSuccessor(0);
    int entry_idx = (SplitOffBlock != nullptr) ? IndexOfBBs(BBs, SplitOffBlock) : -1;
    if (entry_idx < 0) entry_idx = 0;

    DispatcherType dispType = resolveDispatcherType(dispatcherName);
    errs() << "[CFGFlattening] " << F->getName()
           << ": using dispatcher '" << dispatcherName << "'\n";
    auto disp = DispatcherRegistry::instance().create(dispType);
    auto ctx = disp->createContext();

    ctx->totalStates = (unsigned)totalStates;

    disp->initializeContext(*ctx, F, BBs);

    BasicBlock *DispatcherBB =
        BasicBlock::Create(F->getContext(), "Dispatcher", F,
                           SplitOffBlock ? SplitOffBlock : BBs[0]);

    ChangeTarget(OrigEntry, DispatcherBB);

    BasicBlock *Default = CreateDefaultBlock(F, DispFailureStr);

    {
        IRBuilder<> EntryBuilder(OrigEntry->getTerminator());
        disp->updateState(EntryBuilder, *ctx, (unsigned)entry_idx, -1 /*initial*/);
    }

    {
        IRBuilder<> Builder(DispatcherBB, DispatcherBB->begin());
        Value *index = disp->computeIndex(Builder, *ctx);

        SwitchInst *Switch =
            Builder.CreateSwitch(index, Default, (unsigned)totalStates);

        // ---- Dead blocks ----
        for (int i = 0; i < dead_code_counter; ++i) {
            int deadStateIdx = numBlocks + i;
            
            // Dead blocks form a closed loop among themselves: each one branches either
            // to the next dead block or to itself. They are unreachable from real code
            // but keep the CFG well-formed
            int loopTarget = numBlocks + ((i + 1) % dead_code_counter);
            BasicBlock *DB = generateDeadBasicBlock(
                *disp, F, BBs[(unsigned)(rand() % numBlocks)], DispatcherBB, *ctx,
                deadStateIdx, loopTarget);
            Switch->addCase(
                Builder.getInt64((int64_t)ctx->getCaseValue((unsigned)deadStateIdx)),
                DB);
        }

        for (int i = 0; i < numBlocks; ++i) {
            Switch->addCase(
                Builder.getInt64((int64_t)ctx->getCaseValue((unsigned)i)),
                BBs[i]);
        }

        for (int i = 0; i < numBlocks; ++i) {
            BasicBlock *BB = BBs[i];
            Instruction *Term = BB->getTerminator();
            if (!isa<BranchInst>(Term) && !isa<SwitchInst>(Term)) continue;

            int numSucc = (int)Term->getNumSuccessors();

            if (numSucc == 1) {
                int trueTarget = IndexOfBBs(BBs, Term->getSuccessor(0));
                if (trueTarget < 0) continue;

                int fakeTarget = numBlocks + (rand() % dead_code_counter);
                ChangeUnconditionalBranch(*disp, BB, DispatcherBB, *ctx,
                                          trueTarget, fakeTarget, i);
            } else {
                for (int k = 0; k < numSucc; ++k) {
                    int kTarget = IndexOfBBs(BBs, Term->getSuccessor(k));
                    if (kTarget < 0) continue;

                    BasicBlock *Result = CreateTermForCondition(
                        *disp, F, BB->getNextNode(), DispatcherBB, *ctx,
                        kTarget, i);
                    Term->setSuccessor(k, Result);
                }
            }
        }
    }
}


void CFGFlatteningPlugin::ChangeTarget(BasicBlock *BB, BasicBlock *Target) {
    Instruction *Term = BB->getTerminator();
    if (auto *Br = dyn_cast<BranchInst>(Term)) {
        if (!Br->isConditional()) {
            IRBuilder<> Builder(BB);
            Br->eraseFromParent();
            Builder.CreateBr(Target);
        }
    }
}

int CFGFlatteningPlugin::IndexOfBBs(
    const std::vector<BasicBlock *> &BBs, BasicBlock *BB)
{
    for (int i = 0; i < (int)BBs.size(); ++i)
        if (BBs[i] == BB) return i;
    return -1;
}

BasicBlock *CFGFlatteningPlugin::CreateTermForCondition(
    Dispatcher &disp, Function *F, BasicBlock *InsertBefore,
    BasicBlock *DispBB, DispatcherContext &ctx,
    int targetState, int sourceState)
{
    BasicBlock *Result =
        BasicBlock::Create(F->getContext(), "", F, InsertBefore);
    IRBuilder<> Builder(Result);
    disp.updateState(Builder, ctx, (unsigned)targetState, sourceState);
    Builder.CreateBr(DispBB);
    return Result;
}

void CFGFlatteningPlugin::ChangeUnconditionalBranch(
    Dispatcher &disp, BasicBlock *BB, BasicBlock *DispBB,
    DispatcherContext &ctx,
    int targetState, int fakeState, int sourceState)
{
    Instruction *Term = BB->getTerminator();
    Term->eraseFromParent();
    IRBuilder<> Builder(BB);

    Value *Cond = Builder.CreateICmpEQ(Builder.getInt64(0), Builder.getInt64(0));

    BasicBlock *TrueBB = CreateTermForCondition(
        disp, BB->getParent(), BB->getNextNode(), DispBB, ctx,
        targetState, sourceState);
    BasicBlock *FalseBB = CreateTermForCondition(
        disp, BB->getParent(), BB->getNextNode(), DispBB, ctx,
        fakeState, sourceState);

    Builder.CreateCondBr(Cond, TrueBB, FalseBB);
}

void CFGFlatteningPlugin::ChangeConditionalBranch(
    Dispatcher &disp, BasicBlock *BB, BasicBlock *DispBB,
    DispatcherContext &ctx,
    int stateTrueTarget, int stateFalseTarget, int sourceState)
{
    auto *Br = dyn_cast<BranchInst>(BB->getTerminator());
    assert(Br && Br->isConditional());
    Value *Cond = Br->getCondition();
    Br->eraseFromParent();

    BasicBlock *TrueBB = CreateTermForCondition(
        disp, BB->getParent(), BB->getNextNode(), DispBB, ctx,
        stateTrueTarget, sourceState);
    BasicBlock *FalseBB = CreateTermForCondition(
        disp, BB->getParent(), BB->getNextNode(), DispBB, ctx,
        stateFalseTarget, sourceState);

    IRBuilder<> Builder(BB);
    Builder.CreateCondBr(Cond, TrueBB, FalseBB);
}

BasicBlock *CFGFlatteningPlugin::CreateDefaultBlock(
    Function *F, Constant *DispFailureStr)
{
    Module *M = F->getParent();
    LLVMContext &CTX = M->getContext();

    PointerType *CharPtrType = PointerType::getUnqual(Type::getInt8Ty(CTX));
    FunctionType *PutsType =
        FunctionType::get(IntegerType::getInt32Ty(CTX), CharPtrType, false);
    FunctionCallee Puts = M->getOrInsertFunction("puts", PutsType);
    if (Function *PF = dyn_cast<Function>(Puts.getCallee())) {
        PF->setDoesNotThrow();
        PF->addParamAttr(0, Attribute::NoCapture);
        PF->addParamAttr(0, Attribute::ReadOnly);
    }

    FunctionType *ExitType =
        FunctionType::get(Type::getVoidTy(CTX), IntegerType::getInt32Ty(CTX), false);
    FunctionCallee Exit = M->getOrInsertFunction("exit", ExitType);
    if (Function *EF = dyn_cast<Function>(Exit.getCallee()))
        EF->setDoesNotThrow();

    BasicBlock *Result =
        BasicBlock::Create(F->getContext(), "OBF_DISP_DEFAULT", F);
    IRBuilder<> Builder(Result);
    Builder.CreateCall(
        Puts, {Builder.CreatePointerCast(DispFailureStr, CharPtrType, "DF")});
    Builder.CreateCall(Exit, {Builder.getInt32(-1)});
    Builder.CreateUnreachable();
    return Result;
}

BasicBlock *CFGFlatteningPlugin::generateDeadBasicBlock(
    Dispatcher &disp, Function *F, BasicBlock *Basis, BasicBlock *DispBB,
    DispatcherContext &ctx, int stateIdx, int loopTargetIdx)
{
    BasicBlock *Dead = BasicBlock::Create(F->getContext(), "");
    Dead->setName("Dead_" + std::to_string(stateIdx));
    IRBuilder<> Builder(Dead);

    for (auto I = Basis->begin(), IE = Basis->end(); I != IE; ++I) {
        if (isa<UnaryInstruction>(*I) || isa<BinaryOperator>(*I) ||
            isa<CmpInst>(*I))
            Builder.Insert(I->clone());
    }
    Builder.CreateUnreachable();
    Dead->insertInto(F);

    generateBasicBlockContent(Dead);

    Instruction *Unreach = Dead->getTerminator();
    Unreach->eraseFromParent();
    IRBuilder<> B2(Dead);
    Value *Cond = B2.CreateICmpEQ(B2.getInt64(0), B2.getInt64(0));

    BasicBlock *DeadNext = Dead->getNextNode();
    BasicBlock *TrueBB = CreateTermForCondition(
        disp, F, DeadNext, DispBB, ctx,
        loopTargetIdx, stateIdx);
    BasicBlock *FalseBB = CreateTermForCondition(
        disp, F, DeadNext, DispBB, ctx,
        stateIdx, stateIdx);

    B2.CreateCondBr(Cond, TrueBB, FalseBB);
    return Dead;
}

void CFGFlatteningPlugin::generateBasicBlockContent(BasicBlock *BB) {
    IRBuilder<> Builder(BB);
    Builder.SetInsertPoint(BB, BB->begin());

    Value *V4   = ConstantInt::get(IntegerType::getInt64Ty(BB->getContext()), 4);
    Value *V3   = ConstantInt::get(IntegerType::getInt64Ty(BB->getContext()), 3);
    Value *V42  = ConstantInt::get(IntegerType::getInt64Ty(BB->getContext()), 42);
    Value *Rand = ConstantInt::get(IntegerType::getInt64Ty(BB->getContext()),
                                   (uint64_t)rand() * 6364136223846793005ULL + 1);

    Value *Shifted = Builder.CreateShl(V4, V3);
    Value *Mul     = Builder.CreateMul(Shifted, V42);
    Instruction *Inst = BinaryOperator::CreateSub(Mul, Rand);
    #if __clang_major__ >= 16
        Inst->insertInto(BB, BB->begin());
    #else
        Builder.Insert(Inst);
    #endif
}

PassPluginLibraryInfo getCFGPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "CFGFlatPlugin", "0.0.2",
            [](PassBuilder &PB) {
                PB.registerOptimizerLastEPCallback(
                    [&](ModulePassManager &MPM, OptimizationLevel /*Level*/) {
                        MPM.addPass(CFGFlatteningPlugin());
                        return true;
                    });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getCFGPluginInfo();
}
