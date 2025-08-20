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
    // Only handle single-index GEPs
    if (gep->getNumIndices() != 1) {
        return false;
    }

    // Get the source element type
    Type* sourceType = gep->getSourceElementType();

    // Check if the base element type (after all array dimensions) is i32
    Type* baseType = sourceType->getBaseElementType();
    if (!isa<IntegerType>(baseType) || baseType->getBitWidth() != 32) {
        return false;
    }

    auto* intType = cast<IntegerType>(baseType);
    if (intType->getBitWidth() != 32) {
        return false;
    }

    // Get the context and create IRBuilder
    Context* ctx = gep->getType()->getContext();
    IRBuilder builder(ctx);

    // Set insert point to the location of the original GEP
    builder.setInsertPoint(gep);

    // Get the pointer operand and the single index
    Value* ptrOperand = gep->getPointerOperand();
    Value* index = gep->getIndex(0);

    unsigned elementSize = sourceType->getSingleElementType()->getSizeInBytes();

    // Create the multiplication: index * elementSize
    auto* sizeConstant = ConstantInt::get(ctx->getInt32Type(), elementSize);
    auto* scaledIndex =
        builder.createMul(index, sizeConstant, index->getName() + ".scaled");

    auto* i8Type = ctx->getIntegerType(8);

    // Create new GEP with i8 type and scaled index
    // The pointer operand stays the same, just the element type changes to i8
    std::vector<Value*> newIndices = {scaledIndex};
    auto* newGep = builder.createGEP(i8Type, ptrOperand, newIndices,
                                     gep->getName() + ".byte");

    // Replace all uses of the old GEP with the new GEP
    gep->replaceAllUsesWith(newGep);

    // Remove the old GEP instruction
    gep->eraseFromParent();

    return true;
}

REGISTER_PASS(GEPToByteOffsetPass, "gep-to-byte-offset")

}  // namespace midend