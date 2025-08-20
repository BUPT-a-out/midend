#include "Pass/Transform/GEPToByteOffsetPass.h"

#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Type.h"
#include "Support/Casting.h"

namespace midend {

bool GEPToByteOffsetPass::runOnFunction(Function& function, AnalysisManager&) {
    if (!function.isDefinition()) {
        return false;
    }

    return transformGEPs(function);
}

bool GEPToByteOffsetPass::transformGEPs(Function& function) {
    bool changed = false;
    std::vector<GetElementPtrInst*> gepsToTransform;

    // Collect all GEP instructions first
    for (auto* block : function) {
        for (auto* inst : *block) {
            if (auto* gep = dyn_cast<GetElementPtrInst>(inst)) {
                gepsToTransform.push_back(gep);
            }
        }
    }

    // Transform each GEP instruction
    for (auto* gep : gepsToTransform) {
        if (transformGEP(gep)) {
            changed = true;
        }
    }

    return changed;
}

bool GEPToByteOffsetPass::transformGEP(GetElementPtrInst* gep) {
    // Get the source element type
    Type* sourceType = gep->getSourceElementType();
    
    // Check if this is an i32* or [i32 x n] type
    bool shouldTransform = false;
    unsigned elementSize = 0;
    
    if (auto* intType = dyn_cast<IntegerType>(sourceType)) {
        if (intType->getBitWidth() == 32) {
            shouldTransform = true;
            elementSize = 4;  // i32 is 4 bytes
        }
    } else if (auto* arrayType = dyn_cast<ArrayType>(sourceType)) {
        if (auto* elemIntType = dyn_cast<IntegerType>(arrayType->getElementType())) {
            if (elemIntType->getBitWidth() == 32) {
                shouldTransform = true;
                elementSize = 4;  // i32 is 4 bytes
            }
        }
    }
    
    // If not i32* or [i32 x n], don't transform
    if (!shouldTransform) {
        return false;
    }
    
    // Get the context and create IRBuilder
    Context* ctx = gep->getType()->getContext();
    IRBuilder builder(ctx);
    
    // Set insert point to the location of the original GEP
    builder.setInsertPoint(gep);
    
    // Get the pointer operand and indices
    Value* ptrOperand = gep->getPointerOperand();
    
    // For simplicity, we'll handle single-index GEPs
    // GEP i32*, ptr, i -> GEP i8*, ptr, i * 4
    if (gep->getNumIndices() == 1) {
        Value* index = gep->getIndex(0);
        
        // Create the multiplication: index * 4
        auto* four = ConstantInt::get(ctx->getInt32Type(), elementSize);
        auto* scaledIndex = builder.createMul(index, four, "scaled.idx");
        
        // Get i8 type (not i8*)
        auto* i8Type = ctx->getIntegerType(8);
        
        // Create new GEP with i8 type and scaled index
        // The pointer operand stays the same, just the element type changes
        std::vector<Value*> newIndices = {scaledIndex};
        auto* newGep = builder.createGEP(i8Type, ptrOperand, newIndices, gep->getName() + ".byte");
        
        // Replace all uses of the old GEP with the new GEP
        gep->replaceAllUsesWith(newGep);
        
        // Remove the old GEP instruction
        gep->eraseFromParent();
        
        return true;
    }
    
    // For multi-index GEPs with arrays [i32 x n], ptr, i, j
    // We need to handle both indices properly
    if (gep->getNumIndices() == 2 && isa<ArrayType>(sourceType)) {
        auto* arrayType = cast<ArrayType>(sourceType);
        
        Value* idx0 = gep->getIndex(0);
        Value* idx1 = gep->getIndex(1);
        
        // Calculate byte offset: (idx0 * arraySize + idx1) * 4
        auto* arraySize = ConstantInt::get(ctx->getInt32Type(), arrayType->getNumElements());
        auto* baseOffset = builder.createMul(idx0, arraySize, "base.offset");
        auto* totalIndex = builder.createAdd(baseOffset, idx1, "total.idx");
        
        // Scale by element size
        auto* four = ConstantInt::get(ctx->getInt32Type(), elementSize);
        auto* scaledIndex = builder.createMul(totalIndex, four, "scaled.idx");
        
        // Get i8 type (not i8*)
        auto* i8Type = ctx->getIntegerType(8);
        
        // Create new GEP with i8 type and scaled index
        // The pointer operand stays the same, just the element type changes
        std::vector<Value*> newIndices = {scaledIndex};
        auto* newGep = builder.createGEP(i8Type, ptrOperand, newIndices, gep->getName() + ".byte");
        
        // Replace all uses of the old GEP with the new GEP
        gep->replaceAllUsesWith(newGep);
        
        // Remove the old GEP instruction
        gep->eraseFromParent();
        
        return true;
    }
    
    return false;
}

REGISTER_PASS(GEPToByteOffsetPass, "gep-to-byte-offset")

}  // namespace midend