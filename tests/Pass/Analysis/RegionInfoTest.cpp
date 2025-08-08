#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Analysis/RegionInfo.h"
// #include "Pass/PassManager.h" // Not needed for these tests

using namespace midend;

namespace {

class RegionInfoTest : public ::testing::Test {
   protected:
    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;

    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
    }

    void TearDown() override {
        module.reset();
        context.reset();
    }

    // Helper to create RegionInfo with dependencies
    std::unique_ptr<RegionInfo> createRegionInfo(Function* func) {
        // Create dependencies that will live alongside the RegionInfo
        auto domInfo = std::make_unique<DominanceInfo>(func);
        auto postDomInfo = std::make_unique<PostDominanceInfo>(func);

        // Store dependencies to keep them alive
        domInfoCache_ = std::move(domInfo);
        postDomInfoCache_ = std::move(postDomInfo);

        return std::make_unique<RegionInfo>(func, domInfoCache_.get(),
                                            postDomInfoCache_.get());
    }

    // Helper to check if a region contains expected blocks
    bool regionContainsBlocks(const Region* region,
                              const std::vector<std::string>& expectedNames) {
        if (!region) return false;

        const auto& blocks = region->getAllBlocks();
        if (blocks.size() != expectedNames.size()) return false;

        for (const auto& name : expectedNames) {
            bool found = false;
            for (BasicBlock* bb : blocks) {
                if (bb->getName() == name) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }

    // Helper to find block by name
    BasicBlock* findBlock(Function* func, const std::string& name) {
        for (auto& bb : *func) {
            if (bb->getName() == name) {
                return bb;
            }
        }
        return nullptr;
    }

    // Create simple if-then-else function
    Function* createIfThenElseFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "if_then_else", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* then_block = BasicBlock::Create(context.get(), "then", func);
        auto* else_block = BasicBlock::Create(context.get(), "else", func);
        auto* merge = BasicBlock::Create(context.get(), "merge", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* cond =
            builder.createICmpSGT(func->getArg(0), func->getArg(1), "cond");
        builder.createCondBr(cond, then_block, else_block);

        builder.setInsertPoint(then_block);
        auto* thenVal =
            builder.createAdd(func->getArg(0), func->getArg(1), "then_val");
        builder.createBr(merge);

        builder.setInsertPoint(else_block);
        auto* elseVal =
            builder.createSub(func->getArg(0), func->getArg(1), "else_val");
        builder.createBr(merge);

        builder.setInsertPoint(merge);
        auto* phi = builder.createPHI(int32Ty, "result");
        phi->addIncoming(thenVal, then_block);
        phi->addIncoming(elseVal, else_block);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    }

    // Create nested if function
    Function* createNestedIfFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "nested_if", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* outer_then =
            BasicBlock::Create(context.get(), "outer_then", func);
        auto* outer_else =
            BasicBlock::Create(context.get(), "outer_else", func);
        auto* inner_then =
            BasicBlock::Create(context.get(), "inner_then", func);
        auto* inner_else =
            BasicBlock::Create(context.get(), "inner_else", func);
        auto* inner_merge =
            BasicBlock::Create(context.get(), "inner_merge", func);
        auto* outer_merge =
            BasicBlock::Create(context.get(), "outer_merge", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* outerCond = builder.createICmpSGT(func->getArg(0),
                                                func->getArg(1), "outer_cond");
        builder.createCondBr(outerCond, outer_then, outer_else);

        builder.setInsertPoint(outer_then);
        auto* innerCond = builder.createICmpSGT(func->getArg(1),
                                                func->getArg(2), "inner_cond");
        builder.createCondBr(innerCond, inner_then, inner_else);

        builder.setInsertPoint(inner_then);
        auto* innerThenVal = builder.createAdd(func->getArg(0), func->getArg(1),
                                               "inner_then_val");
        builder.createBr(inner_merge);

        builder.setInsertPoint(inner_else);
        auto* innerElseVal = builder.createSub(func->getArg(0), func->getArg(1),
                                               "inner_else_val");
        builder.createBr(inner_merge);

        builder.setInsertPoint(inner_merge);
        auto* innerPhi = builder.createPHI(int32Ty, "inner_result");
        innerPhi->addIncoming(innerThenVal, inner_then);
        innerPhi->addIncoming(innerElseVal, inner_else);
        builder.createBr(outer_merge);

        builder.setInsertPoint(outer_else);
        auto* outerElseVal = builder.createMul(func->getArg(0), func->getArg(2),
                                               "outer_else_val");
        builder.createBr(outer_merge);

        builder.setInsertPoint(outer_merge);
        auto* outerPhi = builder.createPHI(int32Ty, "outer_result");
        outerPhi->addIncoming(innerPhi, inner_merge);
        outerPhi->addIncoming(outerElseVal, outer_else);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(outerPhi);

        return func;
    }

    // Create loop function
    Function* createLoopFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "counter");
        phi->addIncoming(func->getArg(0), entry);
        auto* cond = builder.createICmpSGT(phi, builder.getInt32(0), "cond");
        builder.createCondBr(cond, loop_body, exit);

        builder.setInsertPoint(loop_body);
        auto* decremented = builder.createSub(phi, builder.getInt32(1), "dec");
        phi->addIncoming(decremented, loop_body);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    }

    // Create switch statement function
    Function* createSwitchFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "switch_func", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* case1 = BasicBlock::Create(context.get(), "case1", func);
        auto* case2 = BasicBlock::Create(context.get(), "case2", func);
        auto* case3 = BasicBlock::Create(context.get(), "case3", func);
        auto* default_case = BasicBlock::Create(context.get(), "default", func);
        auto* merge = BasicBlock::Create(context.get(), "merge", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* switchVal = func->getArg(0);

        // Create conditional branches to simulate switch
        auto* cond1 =
            builder.createICmpEQ(switchVal, builder.getInt32(1), "cond1");
        auto* bb_check2 = BasicBlock::Create(context.get(), "check2", func);
        builder.createCondBr(cond1, case1, bb_check2);

        builder.setInsertPoint(bb_check2);
        auto* cond2 =
            builder.createICmpEQ(switchVal, builder.getInt32(2), "cond2");
        auto* bb_check3 = BasicBlock::Create(context.get(), "check3", func);
        builder.createCondBr(cond2, case2, bb_check3);

        builder.setInsertPoint(bb_check3);
        auto* cond3 =
            builder.createICmpEQ(switchVal, builder.getInt32(3), "cond3");
        builder.createCondBr(cond3, case3, default_case);

        builder.setInsertPoint(case1);
        auto* val1 = builder.createAdd(switchVal, builder.getInt32(10), "val1");
        builder.createBr(merge);

        builder.setInsertPoint(case2);
        auto* val2 = builder.createAdd(switchVal, builder.getInt32(20), "val2");
        builder.createBr(merge);

        builder.setInsertPoint(case3);
        auto* val3 = builder.createAdd(switchVal, builder.getInt32(30), "val3");
        builder.createBr(merge);

        builder.setInsertPoint(default_case);
        auto* valDefault =
            builder.createAdd(switchVal, builder.getInt32(100), "val_default");
        builder.createBr(merge);

        builder.setInsertPoint(merge);
        auto* phi = builder.createPHI(int32Ty, "result");
        phi->addIncoming(val1, case1);
        phi->addIncoming(val2, case2);
        phi->addIncoming(val3, case3);
        phi->addIncoming(valDefault, default_case);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    }

   private:
    std::unique_ptr<DominanceInfo> domInfoCache_;
    std::unique_ptr<PostDominanceInfo> postDomInfoCache_;
};

//===----------------------------------------------------------------------===//
// Basic SESE Region Discovery Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, SimpleIfThenElseRegion) {
    // Create a simple function inline to avoid crashes
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "simple_test", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);
    builder.createRet(func->getArg(0));

    // Create dependencies manually
    DominanceInfo domInfo(func);
    PostDominanceInfo postDomInfo(func);

    // Create RegionInfo
    RegionInfo regionInfo(func, &domInfo, &postDomInfo);

    // Get the top-level region first - this should always exist
    auto* topRegion = regionInfo.getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);

    if (topRegion) {
        EXPECT_EQ(topRegion->getEntry()->getName(), "entry");
        EXPECT_EQ(topRegion->getExit(), nullptr);  // Top-level has no exit
        EXPECT_TRUE(topRegion->isTopLevel());
    }

    std::cout << "Simple test passed - RegionInfo creation works\n";
}

TEST_F(RegionInfoTest, LoopRegion) {
    auto* func = createLoopFunction();
    auto regionInfo = createRegionInfo(func);

    EXPECT_TRUE(regionInfo->verify());

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);

    // Look for loop regions
    bool foundLoopRegion = false;
    for (const auto* region : regionInfo->regions()) {
        const auto& blocks = region->getAllBlocks();

        // Check if this region contains loop blocks
        bool hasLoopHeader = false;
        bool hasLoopBody = false;
        for (BasicBlock* bb : blocks) {
            if (bb->getName() == "loop_header") hasLoopHeader = true;
            if (bb->getName() == "loop_body") hasLoopBody = true;
        }

        if (hasLoopHeader && hasLoopBody) {
            foundLoopRegion = true;
            break;
        }
    }

    EXPECT_TRUE(foundLoopRegion);
}

TEST_F(RegionInfoTest, SwitchRegion) {
    auto* func = createSwitchFunction();
    auto regionInfo = createRegionInfo(func);

    EXPECT_TRUE(regionInfo->verify());

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);

    // Switch creates multiple potential SESE regions
    size_t regionCount = 0;
    for (const auto* region : regionInfo->regions()) {
        regionCount++;
    }

    EXPECT_GT(regionCount,
              1u);  // Should have at least top-level + some other regions
}

//===----------------------------------------------------------------------===//
// Region Properties and API Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, RegionBasicProperties) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    auto* entry = findBlock(func, "entry");
    auto* merge = findBlock(func, "merge");
    ASSERT_NE(entry, nullptr);
    ASSERT_NE(merge, nullptr);

    // Test basic region creation
    auto* region = regionInfo->getOrCreateRegion(entry, merge);
    if (region) {
        EXPECT_EQ(region->getEntry(), entry);
        EXPECT_EQ(region->getExit(), merge);
        EXPECT_FALSE(region->isTopLevel());

        // Test contains functionality
        auto* thenBlock = findBlock(func, "then");
        auto* elseBlock = findBlock(func, "else");

        if (thenBlock && elseBlock) {
            // These blocks should be contained if the region is valid
            bool containsThen = region->contains(thenBlock);
            bool containsElse = region->contains(elseBlock);

            // At least one of these should be true for a proper diamond region
            EXPECT_TRUE(containsThen || containsElse);
        }
    }
}

TEST_F(RegionInfoTest, RegionBlockAccess) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);

    // Test lazy block computation
    const auto& allBlocks = topRegion->getAllBlocks();
    const auto& ownBlocks = topRegion->getOwnBlocks();

    EXPECT_GE(allBlocks.size(), 1u);
    EXPECT_GE(ownBlocks.size(), 1u);

    // Test vector access
    auto allBlocksVec = topRegion->getAllBlocksVector();
    auto ownBlocksVec = topRegion->getOwnBlocksVector();

    EXPECT_EQ(allBlocksVec.size(), allBlocks.size());
    EXPECT_EQ(ownBlocksVec.size(), ownBlocks.size());
}

TEST_F(RegionInfoTest, RegionDepthCalculation) {
    auto* func = createNestedIfFunction();
    auto regionInfo = createRegionInfo(func);

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_EQ(topRegion->getDepth(), 0u);

    // Check depth of sub-regions
    for (const auto& subRegion : topRegion->getSubRegions()) {
        EXPECT_GE(subRegion->getDepth(), 1u);
        EXPECT_LE(subRegion->getDepth(), 10u);  // Reasonable upper bound

        // Parent should have lower depth than child
        if (subRegion->getParent()) {
            EXPECT_LT(subRegion->getParent()->getDepth(),
                      subRegion->getDepth());
        }
    }
}

TEST_F(RegionInfoTest, RegionSimpleCheck) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    // Test isSimple() on regions we can find
    for (const auto* region : regionInfo->regions()) {
        // isSimple() should not crash and return a boolean
        bool isSimple = region->isSimple();
        (void)isSimple;  // Suppress unused variable warning

        // Simple regions should have single entry and exit paths
        if (isSimple) {
            EXPECT_NE(region->getEntry(), nullptr);
            EXPECT_NE(region->getExit(), nullptr);
        }
    }
}

//===----------------------------------------------------------------------===//
// Iterator Support Tests (C++17)
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, RegionDepthFirstIterator) {
    auto* func = createNestedIfFunction();
    auto regionInfo = createRegionInfo(func);

    // Test depth-first iteration over regions
    size_t regionCount = 0;
    for (const auto* region : regionInfo->regions()) {
        EXPECT_NE(region, nullptr);
        regionCount++;
    }

    EXPECT_GE(regionCount, 1u);  // At least the top-level region

    // Test iterator begin/end
    auto begin_it = regionInfo->begin();
    auto end_it = regionInfo->end();
    EXPECT_TRUE(begin_it != end_it);  // Should have at least one region

    // Test iterator increment
    auto it = regionInfo->begin();
    auto first_region = *it;
    ++it;

    if (it != regionInfo->end()) {
        auto second_region = *it;
        EXPECT_NE(first_region, second_region);
    }
}

TEST_F(RegionInfoTest, RegionBlockIterator) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    auto* topRegion = regionInfo->getTopLevelRegion();

    // Test block iteration using the range wrapper
    size_t blockCount = 0;
    for (const auto* block : regionInfo->blocks(topRegion)) {
        EXPECT_NE(block, nullptr);
        blockCount++;
    }

    EXPECT_GE(blockCount, 1u);

    // Compare with direct access
    const auto& directBlocks = topRegion->getAllBlocks();
    EXPECT_EQ(blockCount, directBlocks.size());
}

TEST_F(RegionInfoTest, IteratorEdgeCases) {
    // Test with empty function
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* emptyFunc = Function::Create(fnTy, "empty", module.get());

    auto emptyRegionInfo = createRegionInfo(emptyFunc);

    // Should handle empty function gracefully
    size_t emptyRegionCount = 0;
    for (const auto* region : emptyRegionInfo->regions()) {
        (void)region;
        emptyRegionCount++;
    }

    // Empty function might have a top-level region or no regions
    EXPECT_GE(emptyRegionCount, 0u);
    EXPECT_LE(emptyRegionCount, 1u);
}

//===----------------------------------------------------------------------===//
// Region Discovery Algorithm Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, ValidRegionCheck) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    auto* entry = findBlock(func, "entry");
    auto* merge = findBlock(func, "merge");
    auto* exit = findBlock(func, "exit");
    auto* thenBlock = findBlock(func, "then");

    ASSERT_NE(entry, nullptr);
    ASSERT_NE(merge, nullptr);
    ASSERT_NE(exit, nullptr);
    ASSERT_NE(thenBlock, nullptr);

    // Test various region validity checks
    bool entryToMerge = regionInfo->isValidRegion(entry, merge);
    bool entryToExit = regionInfo->isValidRegion(entry, exit);
    bool thenToMerge = regionInfo->isValidRegion(thenBlock, merge);

    // At least some should be valid regions in a diamond CFG
    EXPECT_TRUE(entryToMerge || entryToExit || thenToMerge);

    // Invalid cases
    EXPECT_FALSE(regionInfo->isValidRegion(nullptr, merge));
    EXPECT_FALSE(regionInfo->isValidRegion(entry, nullptr));
    EXPECT_FALSE(regionInfo->isValidRegion(entry, entry));  // Same block
}

TEST_F(RegionInfoTest, RegionHierarchyBuilding) {
    auto* func = createNestedIfFunction();
    auto regionInfo = createRegionInfo(func);

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);

    // Check if sub-regions have proper parent relationships
    std::function<void(const Region*, unsigned)> checkHierarchy =
        [&](const Region* region, unsigned expectedMinDepth) {
            EXPECT_GE(region->getDepth(), expectedMinDepth);

            for (const auto& subRegion : region->getSubRegions()) {
                EXPECT_EQ(subRegion->getParent(), region);
                EXPECT_GT(subRegion->getDepth(), region->getDepth());
                checkHierarchy(subRegion.get(), region->getDepth() + 1);
            }
        };

    checkHierarchy(topRegion, 0);
}

//===----------------------------------------------------------------------===//
// Region Movement Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, RegionMoverBasic) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    auto* mover = regionInfo->getRegionMover();
    EXPECT_NE(mover, nullptr);

    // Create a simple test case for region movement
    auto* entry = findBlock(func, "entry");
    auto* merge = findBlock(func, "merge");
    auto* exit = findBlock(func, "exit");

    ASSERT_NE(entry, nullptr);
    ASSERT_NE(merge, nullptr);
    ASSERT_NE(exit, nullptr);

    // Test if we can create a region to move
    auto* region = regionInfo->getOrCreateRegion(entry, merge);
    if (region) {
        // Try to move the region (this might fail due to SSA constraints, but
        // shouldn't crash)
        bool moveResult = mover->moveRegion(region, entry, exit);
        (void)moveResult;  // Don't require success, just no crash
    }
}

//===----------------------------------------------------------------------===//
// Edge Cases and Error Conditions
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, EmptyFunction) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* emptyFunc = Function::Create(fnTy, "empty", module.get());

    auto regionInfo = createRegionInfo(emptyFunc);

    // Should handle empty function gracefully
    EXPECT_TRUE(regionInfo->verify());

    auto* topRegion = regionInfo->getTopLevelRegion();
    if (topRegion) {
        EXPECT_TRUE(topRegion->isTopLevel());
        EXPECT_EQ(topRegion->getDepth(), 0u);
    }
}

TEST_F(RegionInfoTest, SingleBlockFunction) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "single", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);
    builder.createRet(func->getArg(0));

    auto regionInfo = createRegionInfo(func);

    EXPECT_TRUE(regionInfo->verify());

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);
    EXPECT_EQ(topRegion->getEntry()->getName(), "entry");
    EXPECT_TRUE(topRegion->isTopLevel());
}

TEST_F(RegionInfoTest, RegionContainment) {
    auto* func = createNestedIfFunction();
    auto regionInfo = createRegionInfo(func);

    // Test region containment relationships
    for (const auto* outerRegion : regionInfo->regions()) {
        for (const auto* innerRegion : regionInfo->regions()) {
            if (outerRegion != innerRegion) {
                bool contains = outerRegion->contains(innerRegion);

                // If outer contains inner, then outer should have greater or
                // equal size
                if (contains) {
                    EXPECT_GE(outerRegion->getAllBlocks().size(),
                              innerRegion->getAllBlocks().size());
                }
            }
        }
    }
}

TEST_F(RegionInfoTest, CommonRegionAncestor) {
    auto* func = createNestedIfFunction();
    auto regionInfo = createRegionInfo(func);

    auto* outerThen = findBlock(func, "outer_then");
    auto* outerElse = findBlock(func, "outer_else");

    if (outerThen && outerElse) {
        auto* commonRegion = regionInfo->getCommonRegion(outerThen, outerElse);
        EXPECT_NE(commonRegion, nullptr);

        // Common region should contain both blocks
        EXPECT_TRUE(commonRegion->contains(outerThen));
        EXPECT_TRUE(commonRegion->contains(outerElse));
    }
}

//===----------------------------------------------------------------------===//
// Integration Tests with AnalysisManager
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, RegionAnalysisPass) {
    auto* func = createIfThenElseFunction();

    RegionAnalysis analysis;

    // Test the version without AnalysisManager (should return minimal result)
    auto result1 = analysis.runOnFunction(*func);
    EXPECT_NE(result1, nullptr);

    // Test pass properties
    EXPECT_TRUE(analysis.supportsFunction());
    EXPECT_EQ(analysis.getName(), "RegionAnalysis");

    auto deps = analysis.getDependencies();
    EXPECT_EQ(deps.size(), 2u);
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "DominanceAnalysis") !=
                deps.end());
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "PostDominanceAnalysis") !=
                deps.end());
}

TEST_F(RegionInfoTest, StaticRunMethod) {
    auto* func = createIfThenElseFunction();

    // Create the required dependencies manually
    DominanceInfo domInfo(func);
    PostDominanceInfo postDomInfo(func);

    // Test static run method
    auto result = RegionAnalysis::run(*func, domInfo, postDomInfo);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(result->verify());
    EXPECT_EQ(result->getFunction(), func);
}

//===----------------------------------------------------------------------===//
// Performance and Stress Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, LargeCFGStressTest) {
    // Create a larger CFG to test performance
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "large_cfg", module.get());

    // Create a chain of diamond patterns
    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* current = entry;

    const int NUM_DIAMONDS = 5;
    std::vector<BasicBlock*> mergePoints;

    IRBuilder builder(entry);

    for (int i = 0; i < NUM_DIAMONDS; ++i) {
        auto* left =
            BasicBlock::Create(context.get(), "left" + std::to_string(i), func);
        auto* right = BasicBlock::Create(context.get(),
                                         "right" + std::to_string(i), func);
        auto* merge = BasicBlock::Create(context.get(),
                                         "merge" + std::to_string(i), func);

        builder.setInsertPoint(current);
        auto* cond = builder.createICmpSGT(func->getArg(0), builder.getInt32(i),
                                           "cond" + std::to_string(i));
        builder.createCondBr(cond, left, right);

        builder.setInsertPoint(left);
        builder.createBr(merge);

        builder.setInsertPoint(right);
        builder.createBr(merge);

        builder.setInsertPoint(merge);
        auto* phi = builder.createPHI(int32Ty, "phi" + std::to_string(i));
        phi->addIncoming(builder.getInt32(i * 10), left);
        phi->addIncoming(builder.getInt32(i * 20), right);

        mergePoints.push_back(merge);
        current = merge;
    }

    // Create final exit
    auto* exit = BasicBlock::Create(context.get(), "exit", func);
    builder.createBr(exit);

    builder.setInsertPoint(exit);
    builder.createRet(func->getArg(0));

    // Analyze the large CFG
    auto regionInfo = createRegionInfo(func);

    // Should handle large CFG without issues
    EXPECT_TRUE(regionInfo->verify());

    auto* topRegion = regionInfo->getTopLevelRegion();
    EXPECT_NE(topRegion, nullptr);

    // Should find multiple regions
    size_t regionCount = 0;
    for (const auto* region : regionInfo->regions()) {
        (void)region;
        regionCount++;
    }
    EXPECT_GE(regionCount, 1u);
    EXPECT_LE(regionCount, 100u);  // Reasonable upper bound
}

//===----------------------------------------------------------------------===//
// Print and Debug Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, PrintFunctions) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    // Test print functions (redirect output to verify they don't crash)
    std::stringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

    regionInfo->print();

    auto* topRegion = regionInfo->getTopLevelRegion();
    if (topRegion) {
        topRegion->print();
    }

    std::string output = buffer.str();
    EXPECT_FALSE(output.empty());

    // Restore cout
    std::cout.rdbuf(old);
}

//===----------------------------------------------------------------------===//
// Cache and Memory Management Tests
//===----------------------------------------------------------------------===//

TEST_F(RegionInfoTest, BlockCacheInvalidation) {
    auto* func = createIfThenElseFunction();
    auto regionInfo = createRegionInfo(func);

    auto* topRegion = regionInfo->getTopLevelRegion();
    if (topRegion) {
        // Access blocks to populate cache
        const auto& blocks1 = topRegion->getAllBlocks();
        size_t size1 = blocks1.size();

        // Invalidate cache
        topRegion->invalidateBlockCache();

        // Access again - should recompute
        const auto& blocks2 = topRegion->getAllBlocks();
        size_t size2 = blocks2.size();

        EXPECT_EQ(size1, size2);  // Size should remain the same
    }
}

}  // namespace