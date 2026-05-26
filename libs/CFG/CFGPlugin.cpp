#include "CFG.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Support/RandomNumberGenerator.h"

#include<random>
#include<vector>
#include<iterator>
#include<string>

#define DEBUG_TYPE "duplicate-bb"

STATISTIC(DuplicateBBCountStats, "The # of duplicated blocks");

using namespace llvm;



PreservedAnalyses CFGPlugin::run(Module &M, ModuleAnalysisManager &MAM){
    for(auto &F : M){
        splitBasicBlock(F);
        //shafleBasicBlock(F);
        generateDeadBasicBlocks(F);
    }
    return llvm::PreservedAnalyses::all();
}

void CFGPlugin::generateBasicBlockContent(llvm::BasicBlock *BB, llvm::BasicBlock* Prev, llvm::BasicBlock* Next){
    IRBuilder<> builder(BB);
    builder.SetInsertPoint(BB, BB->end());
    Instruction* tmp = nullptr;
    Value* Val4 = nullptr; 
    Value* Val84 = nullptr;
    for(Instruction &Inst : *Prev){
        if(Inst.getType()->isIntegerTy()){
            tmp = &Inst;
            break;
        }
    }
    if(tmp != nullptr){
        Val4 = tmp->getOperand(0);
        if(tmp->getNumOperands() > 1){
            Val84 = tmp->getOperand(1);
        } else {
            Val84 = ConstantInt::get(IntegerType::getInt32Ty(BB->getContext()), 63);
        }
        
    }else{
        Val4 = ConstantInt::get(IntegerType::getInt32Ty(BB->getContext()), 4);
        Val84 = ConstantInt::get(IntegerType::getInt32Ty(BB->getContext()), 84);
    }
    Value* Val42 = ConstantInt::get(IntegerType::getInt32Ty(BB->getContext()), 42);
    Value* RandVal = ConstantInt::get(IntegerType::getInt32Ty(BB->getContext()), rand()/65536);
    for(int i = 0; i < 5; i++){
        Instruction* inst = BinaryOperator::CreateSub(builder.CreateMul(builder.CreateShl(Val4, Val84), Val42), RandVal);
        #if __clang_major__ == 17
            inst->insertInto(BB, BB->end());
        #elif __clang_major__ == 15
            builder.Insert(inst);
        #endif
    }
    builder.CreateBr(Next);
}

bool hasPHINode(BasicBlock* BB){
    for (BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; I++){
        if (isa<PHINode>(*I)) {
            return true;
        }
    }
    return false;
}

bool CFGPlugin::generateDeadBasicBlocks(Function &F){
    int size = F.size();
    std::vector<BasicBlock*> ToAdd;
    int i = 0;
    for(BasicBlock &BB : F){
        BranchInst* Terminator = dyn_cast<BranchInst>(BB.getTerminator());
        if(BB.getSingleSuccessor() && Terminator && !hasPHINode(BB.getSingleSuccessor())){
            BasicBlock* DeadBB = BasicBlock::Create(F.getContext(), "Dead_code_" + std::to_string(i));
            generateBasicBlockContent(DeadBB, &BB, BB.getSingleSuccessor());
            BranchInst* ConditionalBr = BranchInst::Create(DeadBB, BB.getSingleSuccessor(), ConstantInt::get(IntegerType::getInt32Ty(BB.getContext()), 0));
            ReplaceInstWithInst(Terminator, ConditionalBr);
            ToAdd.push_back(DeadBB);
        }
        i++;
    }
    for(BasicBlock* BB : ToAdd){
        BB->insertInto(&F);
    }
    return true;
}

bool CFGPlugin::splitBasicBlock(Function &F){
    for(auto &BB : F){
        if(BB.size() > 5 && !hasPHINode(&BB)){
            unsigned counter = 0;
            for(auto &Inst : BB){
                if((counter > 0) && ((counter % 5) == 0)){
                    SplitBlock(&BB, &Inst);
                    break;
                }
                counter++;
            }
        }
    }
    return true;
}

bool CFGPlugin::shafleBasicBlock(Function &F){
    std::vector<BasicBlock*> BBs;
    for(BasicBlock &BB : F){
        BBs.push_back(&BB);
    }
    for(BasicBlock* BB : BBs){
        BB->removeFromParent();
    }
    BasicBlock* entryPoint = BasicBlock::Create(F.getContext(), F.getName() + "Entry", &F);
    IRBuilder<> builder(entryPoint);
    builder.SetInsertPoint(entryPoint);
    Instruction* JumpInst = builder.CreateBr(BBs[0]);
    entryPoint->insertInto(&F);
    while(BBs.size() > 0){
        unsigned index = rand() % BBs.size();
        #if __clang_major__ == 17
            F.insert(F.end(), BBs[index]);
        #elif __clang_major__ == 15
            BBs[index]->insertInto(&F);
        #endif
        BBs.erase(BBs.begin() + index);
    }
    return true;
}

llvm::PassPluginLibraryInfo getCFGPluginInfo(){
    return {LLVM_PLUGIN_API_VERSION, "CFGPlugin", "0.0.1", [](PassBuilder &PB){
                    PB.registerOptimizerLastEPCallback(
                [&](ModulePassManager &MPM, OptimizationLevel Level)
                {
                    MPM.addPass(CFGPlugin());
                    return true;
                });
    }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo(){
    return getCFGPluginInfo();
}
