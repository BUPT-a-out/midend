#include "Pass/Transform/ADCEPass.h"

#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Instructions/OtherOps.h"
#include "Support/Casting.h"

namespace midend {

bool ADCEPass::runOnFunction(Function& function, AnalysisManager& am) {
    liveInstructions_.clear();
    while (!workList_.empty()) {
        workList_.pop();
    }

    performLivenessAnalysis(function);
    return removeDeadInstructions(function);
}

void ADCEPass::markInstructionLive(Instruction* inst) {
    if (!inst || liveInstructions_.count(inst)) {
        return;
    }
    
    liveInstructions_.insert(inst);
    workList_.push(inst);
}

bool ADCEPass::isAlwaysLive(Instruction* inst) {
    // Terminator instructions are always live
    if (inst->isTerminator()) {
        return true;
    }
    
    // Store instructions have side effects
    if (isa<StoreInst>(inst)) {
        return true;
    }
    
    // Call instructions might have side effects
    if (auto* call = dyn_cast<CallInst>(inst)) {
        // For simplicity, assume all calls have side effects
        // In a more sophisticated implementation, we would check if the function is pure
        return true;
    }
    
    return false;
}

void ADCEPass::performLivenessAnalysis(Function& function) {
    // Step 1: Mark all always-live instructions
    for (auto& bb : function) {
        for (auto* inst : *bb) {
            if (isAlwaysLive(inst)) {
                markInstructionLive(inst);
            }
        }
    }
    
    // Step 2: Process the worklist to propagate liveness through use-def chains
    while (!workList_.empty()) {
        Instruction* inst = workList_.front();
        workList_.pop();
        
        // Mark all operands as live
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            Value* operand = inst->getOperand(i);
            if (auto* operandInst = dyn_cast<Instruction>(operand)) {
                markInstructionLive(operandInst);
            }
        }
        
        // For PHI nodes, we need to ensure that branch instructions 
        // controlling the incoming edges are also marked as live
        if (auto* phi = dyn_cast<PHINode>(inst)) {
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                BasicBlock* incomingBlock = phi->getIncomingBlock(i);
                if (incomingBlock) {
                    // Mark the terminator of the incoming block as live
                    if (auto* terminator = incomingBlock->getTerminator()) {
                        markInstructionLive(terminator);
                    }
                }
            }
        }
    }
}

bool ADCEPass::removeDeadInstructions(Function& function) {
    bool changed = false;
    std::vector<Instruction*> deadInstructions;
    
    // Collect all dead instructions
    for (auto& bb : function) {
        for (auto* inst : *bb) {
            if (!liveInstructions_.count(inst)) {
                deadInstructions.push_back(inst);
            }
        }
    }
    
    // Remove dead instructions
    for (auto* inst : deadInstructions) {
        inst->eraseFromParent();
        changed = true;
    }
    
    return changed;
}

REGISTER_PASS(ADCEPass, "adce")

}  // namespace midend