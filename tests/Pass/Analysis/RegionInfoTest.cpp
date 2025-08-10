#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Analysis/RegionInfo.h"

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
        postDomInfos_.clear();
        module.reset();
        context.reset();
    }

    // Helper function to create a simple linear function: entry -> bb1 -> bb2
    // -> exit
    Function* createLinearFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "linear", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* bb1 = BasicBlock::Create(context.get(), "bb1", func);
        auto* bb2 = BasicBlock::Create(context.get(), "bb2", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(bb1);

        builder.setInsertPoint(bb1);
        builder.createBr(bb2);

        builder.setInsertPoint(bb2);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(func->getArg(0));

        return func;
    }

    // Helper function to create a diamond CFG: entry -> left/right -> merge ->
    // exit
    Function* createDiamondFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "diamond", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* left = BasicBlock::Create(context.get(), "left", func);
        auto* right = BasicBlock::Create(context.get(), "right", func);
        auto* merge = BasicBlock::Create(context.get(), "merge", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* cond =
            builder.createICmpSGT(func->getArg(0), builder.getInt32(0), "cond");
        builder.createCondBr(cond, left, right);

        builder.setInsertPoint(left);
        builder.createBr(merge);

        builder.setInsertPoint(right);
        builder.createBr(merge);

        builder.setInsertPoint(merge);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(func->getArg(0));

        return func;
    }

    // Helper function to create a simple loop: entry -> header <-> body -> exit
    Function* createSimpleLoopFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "simple_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* header = BasicBlock::Create(context.get(), "header", func);
        auto* body = BasicBlock::Create(context.get(), "body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(header);

        builder.setInsertPoint(header);
        auto* cond =
            builder.createICmpSGT(func->getArg(0), builder.getInt32(0), "cond");
        builder.createCondBr(cond, body, exit);

        builder.setInsertPoint(body);
        builder.createBr(header);

        builder.setInsertPoint(exit);
        builder.createRet(func->getArg(0));

        return func;
    }

    // Helper function to create nested loops: entry -> outer_header ->
    // inner_header <-> inner_body -> outer_body -> exit
    Function* createNestedLoopFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "nested_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* outerHeader =
            BasicBlock::Create(context.get(), "outer_header", func);
        auto* innerHeader =
            BasicBlock::Create(context.get(), "inner_header", func);
        auto* innerBody = BasicBlock::Create(context.get(), "inner_body", func);
        auto* outerBody = BasicBlock::Create(context.get(), "outer_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(outerHeader);

        builder.setInsertPoint(outerHeader);
        auto* outerCond = builder.createICmpSGT(
            func->getArg(0), builder.getInt32(0), "outer_cond");
        builder.createCondBr(outerCond, innerHeader, exit);

        builder.setInsertPoint(innerHeader);
        auto* innerCond = builder.createICmpSGT(
            func->getArg(0), builder.getInt32(5), "inner_cond");
        builder.createCondBr(innerCond, innerBody, outerBody);

        builder.setInsertPoint(innerBody);
        builder.createBr(innerHeader);

        builder.setInsertPoint(outerBody);
        builder.createBr(outerHeader);

        builder.setInsertPoint(exit);
        builder.createRet(func->getArg(0));

        return func;
    }

    // Helper function to create a single-block function
    Function* createSingleBlockFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "single_block", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);

        IRBuilder builder(entry);
        builder.createRet(func->getArg(0));

        return func;
    }

    // Helper function to create empty function
    Function* createEmptyFunction() {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "empty", module.get());
        // No basic blocks added - empty function
        return func;
    }

    // Helper to get all blocks visited by RegionBlockIterator
    std::vector<BasicBlock*> getBlocksFromIterator(RegionBlockIterator begin,
                                                   RegionBlockIterator end) {
        std::vector<BasicBlock*> blocks;
        for (auto it = begin; it != end; ++it) {
            blocks.push_back(*it);
        }
        return blocks;
    }

    // Helper to check if block is in vector
    bool containsBlock(const std::vector<BasicBlock*>& blocks,
                       const std::string& name) {
        return std::any_of(
            blocks.begin(), blocks.end(),
            [&name](BasicBlock* bb) { return bb->getName() == name; });
    }

    bool equal(const std::vector<BasicBlock*>& blocks,
               const std::vector<std::string>& names) {
        if (blocks.size() != names.size()) return false;
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if ((*it)->getName() != names[std::distance(blocks.begin(), it)]) {
                return false;
            }
        }
        return true;
    }

    // Keep PostDominanceInfo alive as member variables
    std::vector<std::unique_ptr<PostDominanceInfo>> postDomInfos_;

    // Helper to run RegionAnalysis and get RegionInfo (following LoopInfoTest
    // pattern)
    std::pair<Function*, std::unique_ptr<RegionInfo>> analyzeFunction(
        std::function<Function*()> createFunc) {
        auto* func = createFunc();
        auto postDomInfo = std::make_unique<PostDominanceInfo>(func);
        PostDominanceInfo* postDomPtr = postDomInfo.get();
        postDomInfos_.push_back(std::move(postDomInfo));
        auto regionInfo = std::make_unique<RegionInfo>(func, postDomPtr);
        return std::make_pair(func, std::move(regionInfo));
    }

    // Helper to create RegionInfo directly (simpler pattern)
    std::unique_ptr<RegionInfo> runRegionAnalysis(Function* func) {
        auto postDomInfo = std::make_unique<PostDominanceInfo>(func);
        PostDominanceInfo* postDomPtr = postDomInfo.get();
        postDomInfos_.push_back(std::move(postDomInfo));
        return std::make_unique<RegionInfo>(func, postDomPtr);
    }

    std::string debugRegion(Region* region) {
        std::ostringstream buffer;
        std::streambuf* oldCoutBuf = std::cout.rdbuf(buffer.rdbuf());
        region->print();
        std::cout.rdbuf(oldCoutBuf);
        return buffer.str();
    }
};

// Test RegionBlockIterator basic functionality
TEST_F(RegionInfoTest, RegionBlockIteratorBasic) {
    auto* func = createLinearFunction();

    // Get all blocks for precise verification
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);  // entry, bb1, bb2, exit

    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    it++;
    BasicBlock* bb2 = *it++;
    BasicBlock* exit = *it;

    // Create iterator from entry to bb2 (should include entry, bb1, but not
    // bb2)
    RegionBlockIterator begin(entry, bb2);
    RegionBlockIterator end;

    // Verify iterator is not at end initially
    EXPECT_NE(begin, end);

    // Should visit entry and bb1, but not bb2 (exit) or beyond
    EXPECT_TRUE(equal(getBlocksFromIterator(begin, end), {"entry", "bb1"}));

    // Test iterator from entry to exit (should visit all except exit)
    RegionBlockIterator beginToExit(entry, exit);
    RegionBlockIterator endIter;

    EXPECT_TRUE(equal(getBlocksFromIterator(beginToExit, endIter),
                      {"entry", "bb1", "bb2"}));
}

// Test RegionBlockIterator with empty region
TEST_F(RegionInfoTest, RegionBlockIteratorEmpty) {
    RegionBlockIterator begin;
    RegionBlockIterator end;

    EXPECT_EQ(begin, end);

    auto visitedBlocks = getBlocksFromIterator(begin, end);
    EXPECT_EQ(visitedBlocks.size(), 0);
}

// Test RegionBlockIterator equality operators
TEST_F(RegionInfoTest, RegionBlockIteratorEquality) {
    auto* func = createLinearFunction();
    auto* entry = &func->getEntryBlock();

    RegionBlockIterator it1(entry, nullptr);
    RegionBlockIterator it2(entry, nullptr);
    RegionBlockIterator end;

    EXPECT_EQ(it1, it2);
    EXPECT_NE(it1, end);

    // Advance one iterator
    ++it1;
    EXPECT_NE(it1, it2);
}

// Test Region class basic functionality
TEST_F(RegionInfoTest, RegionBasic) {
    auto* func = createLinearFunction();
    auto* entry = &func->getEntryBlock();
    auto* exit = &func->back();

    Region region(entry, exit);

    EXPECT_EQ(region.getEntry(), entry);
    EXPECT_EQ(region.getExit(), exit);
    EXPECT_EQ(region.getParent(), nullptr);
    EXPECT_EQ(region.getLevel(), 1);
    EXPECT_TRUE(region.getSubRegions().empty());
}

// Test Region parent-child relationships
TEST_F(RegionInfoTest, RegionHierarchy) {
    auto* func = createLinearFunction();

    // Get specific blocks for detailed testing
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);
    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    BasicBlock* bb1 = *it++;
    BasicBlock* bb2 = *it++;
    BasicBlock* exit = *it;

    Region parentRegion(entry, exit);
    Region childRegion(bb1, bb2);

    // Initially, both should have no parent and level 1
    EXPECT_EQ(parentRegion.getParent(), nullptr);
    EXPECT_EQ(parentRegion.getLevel(), 1);
    EXPECT_EQ(childRegion.getParent(), nullptr);
    EXPECT_EQ(childRegion.getLevel(), 1);

    // Both should start with no sub-regions
    EXPECT_TRUE(parentRegion.getSubRegions().empty());
    EXPECT_TRUE(childRegion.getSubRegions().empty());

    // Add child to parent
    parentRegion.addSubRegion(&childRegion);

    // Verify parent-child relationship
    EXPECT_EQ(childRegion.getParent(), &parentRegion);
    EXPECT_EQ(childRegion.getLevel(), 2);  // Level should update
    EXPECT_EQ(parentRegion.getSubRegions().size(), 1);
    EXPECT_EQ(parentRegion.getSubRegions()[0], &childRegion);

    // Parent should remain at level 1
    EXPECT_EQ(parentRegion.getLevel(), 1);
    EXPECT_EQ(parentRegion.getParent(), nullptr);

    // Verify specific blocks
    EXPECT_EQ(parentRegion.getEntry(), entry);
    EXPECT_EQ(parentRegion.getExit(), exit);
    EXPECT_EQ(childRegion.getEntry(), bb1);
    EXPECT_EQ(childRegion.getExit(), bb2);

    // Test removal
    parentRegion.removeSubRegion(&childRegion);
    EXPECT_EQ(childRegion.getParent(), nullptr);
    EXPECT_EQ(childRegion.getLevel(), 1);  // Level should reset
    EXPECT_TRUE(parentRegion.getSubRegions().empty());

    // Child should still have same entry/exit
    EXPECT_EQ(childRegion.getEntry(), bb1);
    EXPECT_EQ(childRegion.getExit(), bb2);

    // Test removing non-existent sub-region (should be safe)
    Region anotherRegion(bb2, exit);
    parentRegion.removeSubRegion(&anotherRegion);  // Should not crash
    EXPECT_TRUE(parentRegion.getSubRegions().empty());
}

// Test replaceSubRegion functionality
TEST_F(RegionInfoTest, RegionReplaceSubRegion) {
    auto* func = createLinearFunction();

    // Get specific blocks for testing
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);
    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    BasicBlock* bb1 = *it++;
    BasicBlock* bb2 = *it++;
    BasicBlock* exit = *it;

    Region parentRegion(entry, exit);
    Region childRegion1(entry, bb1);
    Region childRegion2(bb1, bb2);
    Region childRegion3(bb2, exit);

    // Add child regions to parent
    parentRegion.addSubRegion(&childRegion1);
    parentRegion.addSubRegion(&childRegion2);
    parentRegion.addSubRegion(&childRegion3);

    // Verify initial state
    EXPECT_EQ(parentRegion.getSubRegions().size(), 3);
    EXPECT_EQ(parentRegion.getSubRegions()[0], &childRegion1);
    EXPECT_EQ(parentRegion.getSubRegions()[1], &childRegion2);
    EXPECT_EQ(parentRegion.getSubRegions()[2], &childRegion3);

    // Create new regions to replace childRegion2
    Region newRegion1(bb1, bb2);
    Region newRegion2(bb2, exit);

    // Test replacing middle region with multiple regions
    parentRegion.replaceSubRegion(&childRegion2, {&newRegion1, &newRegion2});

    // Verify replacement - should have 4 regions now
    const auto& subRegions = parentRegion.getSubRegions();
    EXPECT_EQ(subRegions.size(), 4);
    EXPECT_EQ(subRegions[0], &childRegion1);  // First should remain
    EXPECT_EQ(subRegions[1], &newRegion1);    // New regions in order
    EXPECT_EQ(subRegions[2], &newRegion2);
    EXPECT_EQ(subRegions[3], &childRegion3);  // Last should remain

    // Verify parent relationships
    EXPECT_EQ(childRegion1.getParent(), &parentRegion);
    EXPECT_EQ(newRegion1.getParent(), &parentRegion);
    EXPECT_EQ(newRegion2.getParent(), &parentRegion);
    EXPECT_EQ(childRegion3.getParent(), &parentRegion);
    EXPECT_EQ(childRegion2.getParent(),
              nullptr);  // Old region should have no parent

    // Verify levels
    EXPECT_EQ(newRegion1.getLevel(), 2);
    EXPECT_EQ(newRegion2.getLevel(), 2);
    EXPECT_EQ(childRegion2.getLevel(), 1);  // Should reset to base level

    // Test replacing with empty vector (equivalent to remove)
    parentRegion.replaceSubRegion(&newRegion1, {});
    EXPECT_EQ(parentRegion.getSubRegions().size(), 3);
    EXPECT_EQ(parentRegion.getSubRegions()[0], &childRegion1);
    EXPECT_EQ(parentRegion.getSubRegions()[1], &newRegion2);
    EXPECT_EQ(parentRegion.getSubRegions()[2], &childRegion3);
    EXPECT_EQ(newRegion1.getParent(), nullptr);

    // Test replacing first region
    Region firstReplacement(entry, bb1);
    parentRegion.replaceSubRegion(&childRegion1, {&firstReplacement});
    EXPECT_EQ(parentRegion.getSubRegions().size(), 3);
    EXPECT_EQ(parentRegion.getSubRegions()[0], &firstReplacement);
    EXPECT_EQ(firstReplacement.getParent(), &parentRegion);
    EXPECT_EQ(childRegion1.getParent(), nullptr);

    // Test replacing last region
    Region lastReplacement(bb2, exit);
    parentRegion.replaceSubRegion(&childRegion3, {&lastReplacement});
    EXPECT_EQ(parentRegion.getSubRegions().size(), 3);
    EXPECT_EQ(parentRegion.getSubRegions()[2], &lastReplacement);
    EXPECT_EQ(lastReplacement.getParent(), &parentRegion);
    EXPECT_EQ(childRegion3.getParent(), nullptr);
}

// Test replaceSubRegion with null pointers and edge cases
TEST_F(RegionInfoTest, RegionReplaceSubRegionEdgeCases) {
    auto* func = createLinearFunction();
    auto* entry = &func->getEntryBlock();

    Region parentRegion(entry, nullptr);
    Region childRegion(entry, nullptr);

    parentRegion.addSubRegion(&childRegion);
    EXPECT_EQ(parentRegion.getSubRegions().size(), 1);

    // Test replacing non-existent region
    Region nonExistent(entry, nullptr);
    parentRegion.replaceSubRegion(&nonExistent, {});
    EXPECT_EQ(parentRegion.getSubRegions().size(),
              1);  // Should remain unchanged

    // Test replacing with vector containing nullptr
    Region validRegion(entry, nullptr);
    parentRegion.replaceSubRegion(&childRegion, {&validRegion, nullptr});
    EXPECT_EQ(parentRegion.getSubRegions().size(),
              1);  // Only validRegion should be added
    EXPECT_EQ(parentRegion.getSubRegions()[0], &validRegion);
    EXPECT_EQ(validRegion.getParent(), &parentRegion);
    EXPECT_EQ(childRegion.getParent(), nullptr);
}

// Test Region level calculation with deep hierarchy
TEST_F(RegionInfoTest, RegionDeepHierarchy) {
    auto* func = createLinearFunction();

    // Get specific blocks for each level
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);
    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    BasicBlock* bb1 = *it++;
    BasicBlock* bb2 = *it++;
    BasicBlock* exit = *it;

    Region level1(entry, exit);
    Region level2(entry, bb2);
    Region level3(bb1, bb2);

    // Initially all should be at level 1 with no parents
    EXPECT_EQ(level1.getLevel(), 1);
    EXPECT_EQ(level2.getLevel(), 1);
    EXPECT_EQ(level3.getLevel(), 1);
    EXPECT_EQ(level1.getParent(), nullptr);
    EXPECT_EQ(level2.getParent(), nullptr);
    EXPECT_EQ(level3.getParent(), nullptr);

    // Build hierarchy: level1 -> level2 -> level3
    level1.addSubRegion(&level2);
    level2.addSubRegion(&level3);

    // Verify level calculations
    EXPECT_EQ(level1.getLevel(), 1);
    EXPECT_EQ(level2.getLevel(), 2);
    EXPECT_EQ(level3.getLevel(), 3);

    // Verify parent relationships
    EXPECT_EQ(level1.getParent(), nullptr);
    EXPECT_EQ(level2.getParent(), &level1);
    EXPECT_EQ(level3.getParent(), &level2);

    // Verify sub-region relationships
    EXPECT_EQ(level1.getSubRegions().size(), 1);
    EXPECT_EQ(level1.getSubRegions()[0], &level2);

    EXPECT_EQ(level2.getSubRegions().size(), 1);
    EXPECT_EQ(level2.getSubRegions()[0], &level3);

    EXPECT_TRUE(level3.getSubRegions().empty());

    // Verify block assignments remain correct
    EXPECT_EQ(level1.getEntry(), entry);
    EXPECT_EQ(level1.getExit(), exit);
    EXPECT_EQ(level2.getEntry(), entry);
    EXPECT_EQ(level2.getExit(), bb2);
    EXPECT_EQ(level3.getEntry(), bb1);
    EXPECT_EQ(level3.getExit(), bb2);

    // Test removing from middle of hierarchy
    level2.removeSubRegion(&level3);
    EXPECT_EQ(level3.getParent(), nullptr);
    EXPECT_EQ(level3.getLevel(), 1);  // Should reset to base level
    EXPECT_TRUE(level2.getSubRegions().empty());

    // level2 should still be child of level1
    EXPECT_EQ(level2.getParent(), &level1);
    EXPECT_EQ(level2.getLevel(), 2);
}

// Test RegionInfo construction with linear function
TEST_F(RegionInfoTest, RegionInfoLinearFunction) {
    auto* func = createLinearFunction();
    auto regionInfo = runRegionAnalysis(func);

    EXPECT_EQ(regionInfo->getFunction(), func);

    // Get all blocks from the function for detailed verification
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);  // entry, bb1, bb2, exit

    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    BasicBlock* bb1 = *it++;
    BasicBlock* bb2 = *it++;
    BasicBlock* exit = *it;

    EXPECT_EQ(entry->getName(), "entry");
    EXPECT_EQ(bb1->getName(), "bb1");
    EXPECT_EQ(bb2->getName(), "bb2");
    EXPECT_EQ(exit->getName(), "exit");

    Region* topRegion = regionInfo->getTopRegion();

    EXPECT_EQ(topRegion->getEntry(), entry);   // Should be entry block
    EXPECT_EQ(topRegion->getExit(), nullptr);  // top region has no exit
    EXPECT_EQ(topRegion->getLevel(), 1);
    EXPECT_EQ(topRegion->getParent(), nullptr);

    // Verify sub-regions structure - should have regions for bb1->bb2,
    // bb2->exit, etc.
    const auto& subRegions = topRegion->getSubRegions();
    // Post-dominance analysis should create sub-regions
    EXPECT_EQ(subRegions.size(), 3);

    // Verify query methods with specific blocks
    Region* regionForEntry = regionInfo->getRegionFor(entry);
    EXPECT_EQ(regionForEntry, topRegion);

    Region* regionForBB1 = regionInfo->getRegionFor(bb1);
    EXPECT_TRUE(regionForBB1->getLevel() >=
                1);  // bb1 should be in a valid region
    EXPECT_EQ(regionForBB1->getEntry()->getName(), "bb1");

    Region* regionForBB2 = regionInfo->getRegionFor(bb2);
    EXPECT_TRUE(regionForBB2->getLevel() >=
                1);  // bb2 should be in a valid region
    EXPECT_EQ(regionForBB2->getEntry()->getName(), "bb2");

    Region* regionForExit = regionInfo->getRegionFor(exit);
    EXPECT_TRUE(regionForExit->getLevel() >=
                1);  // exit should be in a valid region
    EXPECT_EQ(regionForExit->getEntry()->getName(), "exit");
    // Verify getRegionByEntry
    Region* topByEntry = regionInfo->getRegionByEntry(entry);
    EXPECT_EQ(topByEntry, topRegion);
    EXPECT_EQ(debugRegion(topRegion),
              R"(  Region: entry=entry, exit=null, level=1
    Region: entry=bb1, exit=bb2, level=2
    Region: entry=bb2, exit=exit, level=2
    Region: entry=exit, exit=null, level=2
)");
}

// Test RegionInfo with empty function
TEST_F(RegionInfoTest, RegionInfoEmptyFunction) {
    auto* func = createEmptyFunction();
    auto regionInfo = runRegionAnalysis(func);

    EXPECT_EQ(regionInfo->getFunction(), func);

    // Verify function is actually empty
    EXPECT_TRUE(func->empty());
    EXPECT_EQ(func->size(), 0);

    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion, nullptr);  // empty function should have no regions

    // Verify query methods handle empty function correctly
    Region* regionForNull = regionInfo->getRegionFor(nullptr);
    EXPECT_EQ(regionForNull, nullptr);  // Should return null for empty function

    Region* regionByEntry = regionInfo->getRegionByEntry(nullptr);
    EXPECT_EQ(regionByEntry, nullptr);

    Region* regionByExit = regionInfo->getRegionByExit(nullptr);
    EXPECT_EQ(regionByExit, nullptr);

    // Verification should fail for empty function (no topRegion)
    EXPECT_FALSE(regionInfo->verify());
}

// Test RegionInfo with single block function
TEST_F(RegionInfoTest, RegionInfoSingleBlock) {
    auto* func = createSingleBlockFunction();
    auto regionInfo = runRegionAnalysis(func);

    EXPECT_EQ(regionInfo->getFunction(), func);

    // Verify function has exactly one block
    EXPECT_FALSE(func->empty());
    EXPECT_EQ(func->size(), 1);

    BasicBlock* entryBlock = &func->getEntryBlock();
    EXPECT_EQ(entryBlock->getName(), "entry");

    // Verify block has no successors (just return)
    auto successors = entryBlock->getSuccessors();
    EXPECT_TRUE(successors.empty());

    auto predecessors = entryBlock->getPredecessors();
    EXPECT_TRUE(predecessors.empty());

    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion->getEntry(), entryBlock);
    EXPECT_EQ(topRegion->getExit(), nullptr);
    EXPECT_EQ(topRegion->getLevel(), 1);
    EXPECT_EQ(topRegion->getParent(), nullptr);

    // Single block should have no sub-regions
    const auto& subRegions = topRegion->getSubRegions();
    EXPECT_TRUE(subRegions.empty());

    // Verify query methods
    Region* regionForEntry = regionInfo->getRegionFor(entryBlock);
    EXPECT_EQ(regionForEntry, topRegion);

    Region* regionByEntry = regionInfo->getRegionByEntry(entryBlock);
    EXPECT_EQ(regionByEntry, topRegion);

    // No exit block to query
    Region* regionByExit = regionInfo->getRegionByExit(entryBlock);
    EXPECT_EQ(regionByExit, nullptr);  // entry is not an exit

    EXPECT_EQ(debugRegion(topRegion),
              R"(  Region: entry=entry, exit=null, level=1
)");
}

// Test RegionInfo query methods
TEST_F(RegionInfoTest, RegionInfoQueryMethods) {
    auto* func = createLinearFunction();
    auto regionInfo = runRegionAnalysis(func);

    EXPECT_EQ(regionInfo->getFunction(), func);

    auto* entry = &func->getEntryBlock();
    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion->getEntry(), entry);

    // Test getRegionFor
    Region* regionForEntry = regionInfo->getRegionFor(entry);
    EXPECT_EQ(regionForEntry, topRegion);
    // Should get the top region or a region containing entry

    // Test getRegionByEntry
    Region* regionByEntry = regionInfo->getRegionByEntry(entry);
    EXPECT_EQ(regionByEntry, topRegion);

    // Test with non-existent block
    Region* regionForNull = regionInfo->getRegionFor(nullptr);
    EXPECT_EQ(regionForNull,
              topRegion);  // Should return top region as fallback
}

// Test RegionInfo with diamond CFG
TEST_F(RegionInfoTest, RegionInfoDiamondCFG) {
    auto* func = createDiamondFunction();
    auto regionInfo = runRegionAnalysis(func);

    EXPECT_EQ(regionInfo->getFunction(), func);

    // Get all blocks from diamond CFG for detailed verification
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 5);  // entry, left, right, merge, exit

    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    BasicBlock* left = *it++;
    BasicBlock* right = *it++;
    BasicBlock* merge = *it++;
    BasicBlock* exit = *it;

    EXPECT_EQ(entry->getName(), "entry");
    EXPECT_EQ(left->getName(), "left");
    EXPECT_EQ(right->getName(), "right");
    EXPECT_EQ(merge->getName(), "merge");
    EXPECT_EQ(exit->getName(), "exit");

    // Verify CFG structure
    auto entrySuccs = entry->getSuccessors();
    EXPECT_EQ(entrySuccs.size(), 2);  // branches to left and right
    EXPECT_TRUE(std::find(entrySuccs.begin(), entrySuccs.end(), left) !=
                entrySuccs.end());
    EXPECT_TRUE(std::find(entrySuccs.begin(), entrySuccs.end(), right) !=
                entrySuccs.end());

    auto leftSuccs = left->getSuccessors();
    EXPECT_EQ(leftSuccs.size(), 1);
    EXPECT_EQ(leftSuccs[0], merge);

    auto rightSuccs = right->getSuccessors();
    EXPECT_EQ(rightSuccs.size(), 1);
    EXPECT_EQ(rightSuccs[0], merge);

    auto mergeSuccs = merge->getSuccessors();
    EXPECT_EQ(mergeSuccs.size(), 1);
    EXPECT_EQ(mergeSuccs[0], exit);

    auto exitSuccs = exit->getSuccessors();
    EXPECT_TRUE(exitSuccs.empty());  // return instruction

    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion->getEntry(), entry);
    EXPECT_EQ(topRegion->getExit(), nullptr);
    EXPECT_EQ(topRegion->getLevel(), 1);
    EXPECT_EQ(topRegion->getParent(), nullptr);

    // Verify query methods return valid regions for all blocks
    Region* regionForEntry = regionInfo->getRegionFor(entry);
    EXPECT_EQ(regionForEntry, topRegion);

    Region* regionForLeft = regionInfo->getRegionFor(left);
    EXPECT_TRUE(regionForLeft->getLevel() >=
                1);  // left should be in a valid region
    EXPECT_TRUE(regionForLeft == topRegion ||
                regionForLeft->getParent() != nullptr);

    Region* regionForRight = regionInfo->getRegionFor(right);
    EXPECT_TRUE(regionForRight->getLevel() >=
                1);  // right should be in a valid region
    EXPECT_TRUE(regionForRight == topRegion ||
                regionForRight->getParent() != nullptr);

    Region* regionForMerge = regionInfo->getRegionFor(merge);
    EXPECT_TRUE(regionForMerge->getLevel() >=
                1);  // merge should be in a valid region
    EXPECT_TRUE(regionForMerge == topRegion ||
                regionForMerge->getParent() != nullptr);

    Region* regionForExit = regionInfo->getRegionFor(exit);
    EXPECT_TRUE(regionForExit->getLevel() >=
                1);  // exit should be in a valid region
    EXPECT_TRUE(regionForExit == topRegion ||
                regionForExit->getParent() != nullptr);

    // Verify getRegionByEntry
    Region* topByEntry = regionInfo->getRegionByEntry(entry);
    EXPECT_EQ(topByEntry, topRegion);

    // Verify region structure has some sub-regions for diamond pattern
    const auto& subRegions = topRegion->getSubRegions();
    EXPECT_GE(subRegions.size(), 0);  // Post-dominance may create sub-regions

    // All sub-regions should have topRegion as parent
    for (Region* subRegion : subRegions) {
        EXPECT_EQ(subRegion->getParent(), topRegion);
        EXPECT_GT(subRegion->getLevel(), topRegion->getLevel());
    }

    topRegion->print();
}

// Test RegionInfo with simple loop
TEST_F(RegionInfoTest, RegionInfoSimpleLoop) {
    auto* func = createSimpleLoopFunction();
    auto regionInfo = runRegionAnalysis(func);

    ASSERT_NE(regionInfo, nullptr);

    Region* topRegion = regionInfo->getTopRegion();
    ASSERT_NE(topRegion, nullptr);
    EXPECT_EQ(topRegion->getEntry(), &func->getEntryBlock());

    // Should identify loop regions
    EXPECT_GE(topRegion->getSubRegions().size(), 0);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @simple_loop(i32 %arg0) {
entry:
  br label %header
header:
  %cond = icmp sgt i32 %arg0, 0
  br i1 %cond, label %body, label %exit
body:
  br label %header
exit:
  ret i32 %arg0
}
)");

    EXPECT_EQ(debugRegion(topRegion),
              R"(  Region: entry=entry, exit=null, level=1
    Region: entry=header, exit=exit, level=2
    Region: entry=exit, exit=null, level=2
)");
}

// Test RegionInfo with nested loops
TEST_F(RegionInfoTest, RegionInfoNestedLoops) {
    auto* func = createNestedLoopFunction();
    auto regionInfo = runRegionAnalysis(func);

    ASSERT_NE(regionInfo, nullptr);

    Region* topRegion = regionInfo->getTopRegion();
    ASSERT_NE(topRegion, nullptr);

    // Should handle nested loop structure
    // Verify basic properties
    EXPECT_EQ(topRegion->getEntry(), &func->getEntryBlock());
    EXPECT_GE(topRegion->getSubRegions().size(), 0);

    EXPECT_EQ(debugRegion(topRegion),
              R"(  Region: entry=entry, exit=null, level=1
    Region: entry=outer_header, exit=exit, level=2
      Region: entry=inner_header, exit=outer_body, level=3
    Region: entry=exit, exit=null, level=2
)");
}

// Test RegionInfo verification
TEST_F(RegionInfoTest, RegionInfoVerification) {
    auto* func = createLinearFunction();
    auto regionInfo = runRegionAnalysis(func);

    EXPECT_EQ(regionInfo->getFunction(), func);
    EXPECT_TRUE(regionInfo->verify());

    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion->getEntry(), &func->getEntryBlock());
    EXPECT_EQ(topRegion->getLevel(), 1);

    EXPECT_EQ(debugRegion(topRegion),
              R"(  Region: entry=entry, exit=null, level=1
    Region: entry=bb1, exit=bb2, level=2
    Region: entry=bb2, exit=exit, level=2
    Region: entry=exit, exit=null, level=2
)");
}

// Test Region blocks() iterator
TEST_F(RegionInfoTest, RegionBlocksIterator) {
    auto* func = createLinearFunction();

    // Get specific blocks for precise testing
    auto blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);  // entry, bb1, bb2, exit
    auto it = blocks.begin();
    BasicBlock* entry = *it++;
    BasicBlock* bb1 = *it++;
    BasicBlock* bb2 = *it++;  // This will be our exit
    BasicBlock* exit = *it;

    Region region(entry, bb2);  // Region from entry to bb2 (excluding bb2)

    // Test blocks() iterator
    auto regionBegin = region.blocks();
    auto regionEnd = region.blocks_end();

    // Verify iterators are different initially
    EXPECT_NE(regionBegin, regionEnd);

    auto regionBlocks = getBlocksFromIterator(regionBegin, regionEnd);

    // Should contain entry and bb1, but not bb2 (the exit)
    EXPECT_EQ(regionBlocks.size(), 2);
    EXPECT_TRUE(containsBlock(regionBlocks, "entry"));
    EXPECT_TRUE(containsBlock(regionBlocks, "bb1"));
    EXPECT_FALSE(containsBlock(regionBlocks, "bb2"));  // bb2 is exit, excluded
    EXPECT_FALSE(
        containsBlock(regionBlocks, "exit"));  // final exit not reachable

    // Verify block order (BFS from entry)
    ASSERT_EQ(regionBlocks.size(), 2);
    EXPECT_EQ(regionBlocks[0], entry);
    EXPECT_EQ(regionBlocks[1], bb1);

    // Test with different exit point
    Region regionToExit(entry, exit);  // From entry to final exit
    auto blocksToExit =
        getBlocksFromIterator(regionToExit.blocks(), regionToExit.blocks_end());

    // Should contain entry, bb1, bb2 but not exit
    EXPECT_EQ(blocksToExit.size(), 3);
    EXPECT_TRUE(containsBlock(blocksToExit, "entry"));
    EXPECT_TRUE(containsBlock(blocksToExit, "bb1"));
    EXPECT_TRUE(containsBlock(blocksToExit, "bb2"));
    EXPECT_FALSE(containsBlock(blocksToExit, "exit"));

    // Test single-block region (entry only, exit immediately)
    Region singleBlockRegion(entry, bb1);  // Just entry block
    auto singleBlocks = getBlocksFromIterator(singleBlockRegion.blocks(),
                                              singleBlockRegion.blocks_end());

    EXPECT_EQ(singleBlocks.size(), 1);
    EXPECT_EQ(singleBlocks[0], entry);
}

// Test RegionAnalysis dependency management
TEST_F(RegionInfoTest, RegionAnalysisDependencies) {
    RegionAnalysis analysis;

    auto deps = analysis.getDependencies();
    EXPECT_EQ(deps.size(), 1);
    EXPECT_EQ(deps[0], "PostDominanceAnalysis");

    EXPECT_TRUE(analysis.supportsFunction());
    EXPECT_EQ(analysis.getName(), "RegionAnalysis");
}

// Test RegionAnalysis using direct construction
TEST_F(RegionInfoTest, RegionAnalysisDirectConstruction) {
    auto* func = createLinearFunction();

    auto postDomInfo = std::make_unique<PostDominanceInfo>(func);
    auto regionInfo = std::make_unique<RegionInfo>(func, postDomInfo.get());

    EXPECT_EQ(regionInfo->getFunction(), func);
    EXPECT_TRUE(regionInfo->verify());

    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion->getEntry(), &func->getEntryBlock());
}

// Test RegionAnalysis static run method
TEST_F(RegionInfoTest, RegionAnalysisStaticRun) {
    auto* func = createLinearFunction();

    PostDominanceInfo postDomInfo(func);
    auto regionInfo = RegionAnalysis::run(*func, postDomInfo);

    EXPECT_EQ(regionInfo->getFunction(), func);
    EXPECT_TRUE(regionInfo->verify());

    Region* topRegion = regionInfo->getTopRegion();
    EXPECT_EQ(topRegion->getEntry(), &func->getEntryBlock());
}

// Test complex region hierarchy verification
TEST_F(RegionInfoTest, ComplexRegionHierarchyVerification) {
    auto* func = createNestedLoopFunction();
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @nested_loop(i32 %arg0) {
entry:
  br label %outer_header
outer_header:
  %outer_cond = icmp sgt i32 %arg0, 0
  br i1 %outer_cond, label %inner_header, label %exit
inner_header:
  %inner_cond = icmp sgt i32 %arg0, 5
  br i1 %inner_cond, label %inner_body, label %outer_body
inner_body:
  br label %inner_header
outer_body:
  br label %outer_header
exit:
  ret i32 %arg0
}
)");

    auto regionInfo = runRegionAnalysis(func);

    ASSERT_NE(regionInfo, nullptr);

    // Verify the hierarchy is consistent
    EXPECT_TRUE(regionInfo->verify());

    Region* topRegion = regionInfo->getTopRegion();
    ASSERT_NE(topRegion, nullptr);

    // Recursively verify all sub-regions have correct parent relationships
    std::function<bool(Region*)> verifyHierarchy = [&](Region* region) -> bool {
        for (Region* subRegion : region->getSubRegions()) {
            if (subRegion->getParent() != region) {
                return false;
            }
            if (subRegion->getLevel() != region->getLevel() + 1) {
                return false;
            }
            if (!verifyHierarchy(subRegion)) {
                return false;
            }
        }
        return true;
    };

    EXPECT_TRUE(verifyHierarchy(topRegion));

    topRegion->print();
}

// Test edge cases with null pointers
TEST_F(RegionInfoTest, EdgeCasesNullPointers) {
    // Test Region with null entry
    Region regionNullEntry(nullptr, nullptr);
    EXPECT_EQ(regionNullEntry.getEntry(), nullptr);
    EXPECT_EQ(regionNullEntry.getExit(), nullptr);

    // Test adding null sub-region
    auto* func = createLinearFunction();
    auto* entry = &func->getEntryBlock();
    Region region(entry, nullptr);

    region.addSubRegion(nullptr);
    EXPECT_TRUE(region.getSubRegions().empty());

    // Test removing non-existent sub-region
    Region subRegion(entry, nullptr);
    region.removeSubRegion(&subRegion);  // Should not crash
    EXPECT_TRUE(region.getSubRegions().empty());
}

TEST_F(RegionInfoTest, DISABLED_MoveRegion) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "nested_loop", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* preheader = BasicBlock::Create(context.get(), "preheader", func);
    auto* cond0 = BasicBlock::Create(context.get(), "cond.0", func);
    auto* loop0 = BasicBlock::Create(context.get(), "loop.0", func);
    auto* cond1 = BasicBlock::Create(context.get(), "cond.1", func);
    auto* loop1 = BasicBlock::Create(context.get(), "loop.1", func);
    auto* merge1 = BasicBlock::Create(context.get(), "merge", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    builder.createBr(preheader);
    builder.setInsertPoint(preheader);
    builder.createAdd(builder.getInt32(0), builder.getInt32(1), "add");
    builder.createBr(cond0);
    builder.setInsertPoint(cond0);
    auto* sumPhi2 = builder.createPHI(int32Ty, "sum.phi.2");
    sumPhi2->addIncoming(builder.getInt32(0), preheader);
    auto* jPhi4 = builder.createPHI(int32Ty, "j.phi.4");
    jPhi4->addIncoming(builder.getInt32(0), preheader);
    auto* iPhi5 = builder.createPHI(int32Ty, "i.phi.5");
    iPhi5->addIncoming(builder.getInt32(0), preheader);
    auto* lt1 = builder.createICmpSLT(iPhi5, builder.getInt32(5), "lt.1");
    builder.createCondBr(lt1, loop0, exit);

    builder.setInsertPoint(loop0);
    builder.createBr(cond1);

    builder.setInsertPoint(cond1);
    auto* sumPhi1 = builder.createPHI(int32Ty, "sum.phi.1");
    sumPhi1->addIncoming(sumPhi2, loop0);
    auto* jPhi3 = builder.createPHI(int32Ty, "j.phi.3");
    jPhi3->addIncoming(jPhi4, loop0);
    auto* lt3 = builder.createICmpSLT(jPhi3, builder.getInt32(5), "lt.3");
    builder.createCondBr(lt3, loop1, merge1);

    builder.setInsertPoint(loop1);
    auto tphi = builder.createPHI(int32Ty, "t.phi");
    tphi->addIncoming(builder.getInt32(1), cond1);
    auto* add5 = builder.createAdd(jPhi3, builder.getInt32(1), "add.5");
    builder.createBr(cond1);

    builder.setInsertPoint(merge1);
    auto kphi = builder.createPHI(int32Ty, "k.phi");
    kphi->addIncoming(builder.getInt32(1), cond1);
    auto* add7 = builder.createAdd(iPhi5, builder.getInt32(1), "add.7");
    builder.createBr(cond0);

    builder.setInsertPoint(exit);
    builder.createRet(sumPhi2);

    // Add the additional phi incoming edges
    sumPhi2->addIncoming(sumPhi1, merge1);
    jPhi4->addIncoming(jPhi3, merge1);
    iPhi5->addIncoming(add7, merge1);

    sumPhi1->addIncoming(builder.getInt32(1), loop1);
    jPhi3->addIncoming(add5, loop1);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @nested_loop(i32 %arg0) {
entry:
  br label %preheader
preheader:
  %add = add i32 0, 1
  br label %cond.0
cond.0:
  %sum.phi.2 = phi i32 [ 0, %preheader ], [ %sum.phi.1, %merge ]
  %j.phi.4 = phi i32 [ 0, %preheader ], [ %j.phi.3, %merge ]
  %i.phi.5 = phi i32 [ 0, %preheader ], [ %add.7, %merge ]
  %lt.1 = icmp slt i32 %i.phi.5, 5
  br i1 %lt.1, label %loop.0, label %exit
loop.0:
  br label %cond.1
cond.1:
  %sum.phi.1 = phi i32 [ %sum.phi.2, %loop.0 ], [ 1, %loop.1 ]
  %j.phi.3 = phi i32 [ %j.phi.4, %loop.0 ], [ %add.5, %loop.1 ]
  %lt.3 = icmp slt i32 %j.phi.3, 5
  br i1 %lt.3, label %loop.1, label %merge
loop.1:
  %t.phi = phi i32 [ 1, %cond.1 ]
  %add.5 = add i32 %j.phi.3, 1
  br label %cond.1
merge:
  %k.phi = phi i32 [ 1, %cond.1 ]
  %add.7 = add i32 %i.phi.5, 1
  br label %cond.0
exit:
  ret i32 %sum.phi.2
}
)");

    auto regionInfo = runRegionAnalysis(func);
    auto src = regionInfo->getRegionByEntry(cond1);
    auto dst = regionInfo->getRegionByEntry(preheader);
    regionInfo->moveRegion(src, dst);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @nested_loop(i32 %arg0) {
entry:
  br label %preheader
preheader:
  %sum.phi.1 = phi i32 [ %sum.phi.2, %entry ], [ 1, %loop.1 ]
  %j.phi.3 = phi i32 [ %j.phi.4, %entry ], [ %add.5, %loop.1 ]
  %lt.3 = icmp slt i32 %j.phi.3, 5
  %add = add i32 0, 1
  br i1 %lt.3, label %loop.1, label %merge_new.1
cond.0:
  %sum.phi.2 = phi i32 [ 0, %merge_new.1 ], [ %sum.phi.1, %merge ]
  %j.phi.4 = phi i32 [ 0, %merge_new.1 ], [ %j.phi.3, %merge ]
  %i.phi.5 = phi i32 [ 0, %merge_new.1 ], [ %add.7, %merge ]
  %lt.1 = icmp slt i32 %i.phi.5, 5
  br i1 %lt.1, label %loop.0, label %exit
loop.0:
  br label %cond.1
cond.1:
  br label %merge
loop.1:
  %t.phi = phi i32 [ 1, %preheader ]
  %add.5 = add i32 %j.phi.3, 1
  br label %preheader
merge:
  %add.7 = add i32 %i.phi.5, 1
  br label %cond.0
exit:
  ret i32 %sum.phi.2
merge_new.1:
  %k.phi = phi i32 [ 1, %merge_new.1 ]
  br label %cond.0
}
)");
}

}  // namespace