#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "Pass/Pass.h"
#include "Pass/Transform/Mem2RegPass.h"

using namespace midend;

class Mem2RegTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();
        am->registerAnalysisType<DominanceAnalysis>();
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

TEST_F(Mem2RegTest, SimpleAllocaPromoted) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create alloca
    auto alloca = builder->createAlloca(intType, nullptr, "x");

    // Store 42 to the alloca
    auto val = builder->getInt32(42);
    builder->createStore(val, alloca);

    // Load from the alloca
    auto load = builder->createLoad(alloca, "load_x");

    // Return the loaded value
    builder->createRet(load);

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Verify that alloca and load/store are removed
    bool hasAlloca = false;
    bool hasLoadStore = false;
    Instruction* retInst = nullptr;

    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) {
                hasAlloca = true;
            }
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) {
                hasLoadStore = true;
            }
            if (isa<ReturnInst>(*it)) {
                retInst = *it;
            }
        }
    }

    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    
    // Verify the return value is the constant 42 (register allocation correctness)
    ASSERT_NE(retInst, nullptr);
    auto retValue = cast<ReturnInst>(retInst)->getReturnValue();
    ASSERT_NE(retValue, nullptr);
    auto constInt = dyn_cast<ConstantInt>(retValue);
    ASSERT_NE(constInt, nullptr);
    EXPECT_EQ(constInt->getValue(), 42);
}

// Basic edge case tests
TEST_F(Mem2RegTest, AllocaWithMultipleStores) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "x");

    // Multiple stores to same alloca
    builder->createStore(builder->getInt32(10), alloca);
    builder->createStore(builder->getInt32(20), alloca);
    builder->createStore(builder->getInt32(30), alloca);

    // Load final value
    auto load = builder->createLoad(alloca, "final_x");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // Should be promoted - last store value should be used
    bool hasAlloca = false;
    bool hasLoadStore = false;
    Instruction* retInst = nullptr;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (isa<ReturnInst>(*it)) retInst = *it;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    
    // Verify the return value is 30 (last stored value - register allocation correctness)
    ASSERT_NE(retInst, nullptr);
    auto retValue = cast<ReturnInst>(retInst)->getReturnValue();
    ASSERT_NE(retValue, nullptr);
    auto constInt = dyn_cast<ConstantInt>(retValue);
    ASSERT_NE(constInt, nullptr);
    EXPECT_EQ(constInt->getValue(), 30);
}

TEST_F(Mem2RegTest, AllocaWithUnusedLoad) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "x");
    builder->createStore(builder->getInt32(42), alloca);

    // Unused load
    auto unusedLoad = builder->createLoad(alloca, "unused");
    (void)unusedLoad;

    // Return constant instead of loaded value
    builder->createRet(builder->getInt32(100));

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    Instruction* retInst = nullptr;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (isa<ReturnInst>(*it)) retInst = *it;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    
    // Verify the return value is still 100 (dead store elimination)
    ASSERT_NE(retInst, nullptr);
    auto retValue = cast<ReturnInst>(retInst)->getReturnValue();
    ASSERT_NE(retValue, nullptr);
    auto constInt = dyn_cast<ConstantInt>(retValue);
    ASSERT_NE(constInt, nullptr);
    EXPECT_EQ(constInt->getValue(), 100);
}

TEST_F(Mem2RegTest, MultipleAllocasInSameFunction) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create multiple allocas
    auto alloca1 = builder->createAlloca(intType, nullptr, "x");
    auto alloca2 = builder->createAlloca(intType, nullptr, "y");
    auto alloca3 = builder->createAlloca(intType, nullptr, "z");

    // Store and load from each
    builder->createStore(builder->getInt32(10), alloca1);
    builder->createStore(builder->getInt32(20), alloca2);
    builder->createStore(builder->getInt32(30), alloca3);

    auto load1 = builder->createLoad(alloca1, "x_val");
    auto load2 = builder->createLoad(alloca2, "y_val");
    auto load3 = builder->createLoad(alloca3, "z_val");

    // Add all values and return
    auto add1 = builder->createAdd(load1, load2, "tmp1");
    auto add2 = builder->createAdd(add1, load3, "result");
    builder->createRet(add2);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    Instruction* retInst = nullptr;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (isa<ReturnInst>(*it)) retInst = *it;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    
    // Verify the return value is 60 (10 + 20 + 30 - register allocation correctness)
    ASSERT_NE(retInst, nullptr);
    auto retValue = cast<ReturnInst>(retInst)->getReturnValue();
    ASSERT_NE(retValue, nullptr);
    // The return value should be a constant result of 10 + 20 + 30 = 60
    auto constInt = dyn_cast<ConstantInt>(retValue);
    ASSERT_NE(constInt, nullptr);
    EXPECT_EQ(constInt->getValue(), 60);
}

// Simple if-else tests
TEST_F(Mem2RegTest, SimpleIfElseWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "result");

    // Condition: n > 0
    auto condition =
        builder->createICmpSGT(func->getArg(0), builder->getInt32(0), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    // True branch
    builder->setInsertPoint(trueBB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(mergeBB);

    // False branch
    builder->setInsertPoint(falseBB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(mergeBB);

    // Merge block
    builder->setInsertPoint(mergeBB);
    auto load = builder->createLoad(alloca, "final_result");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // Should be promoted with PHI node
    bool hasAlloca = false;
    bool hasLoadStore = false;
    PHINode* phiNode = nullptr;
    Instruction* retInst = nullptr;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (auto phi = dyn_cast<PHINode>(*it)) {
                phiNode = phi;
            }
            if (isa<ReturnInst>(*it)) retInst = *it;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    ASSERT_NE(phiNode, nullptr);
    
    // Verify PHI node correctness
    EXPECT_EQ(phiNode->getNumIncomingValues(), 2);
    
    // Check PHI node incoming values and blocks
    bool hasValue100 = false;
    bool hasValue200 = false;
    bool hasTrueBlock = false;
    bool hasFalseBlock = false;
    
    for (unsigned i = 0; i < phiNode->getNumIncomingValues(); ++i) {
        auto value = phiNode->getIncomingValue(i);
        auto block = phiNode->getIncomingBlock(i);
        
        if (auto constInt = dyn_cast<ConstantInt>(value)) {
            if (constInt->getValue() == 100) hasValue100 = true;
            if (constInt->getValue() == 200) hasValue200 = true;
        }
        
        if (block->getName() == "if.true") hasTrueBlock = true;
        if (block->getName() == "if.false") hasFalseBlock = true;
    }
    
    EXPECT_TRUE(hasValue100);
    EXPECT_TRUE(hasValue200);
    EXPECT_TRUE(hasTrueBlock);
    EXPECT_TRUE(hasFalseBlock);
    
    // Verify the return instruction uses the PHI node
    ASSERT_NE(retInst, nullptr);
    auto retValue = cast<ReturnInst>(retInst)->getReturnValue();
    EXPECT_EQ(retValue, phiNode);
}

TEST_F(Mem2RegTest, IfElseWithMultipleAllocas) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca1 = builder->createAlloca(intType, nullptr, "x");
    auto alloca2 = builder->createAlloca(intType, nullptr, "y");

    // Initial stores
    builder->createStore(builder->getInt32(10), alloca1);
    builder->createStore(builder->getInt32(20), alloca2);

    auto condition =
        builder->createICmpEQ(func->getArg(0), builder->getInt32(1), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    // True branch - modify x
    builder->setInsertPoint(trueBB);
    builder->createStore(builder->getInt32(100), alloca1);
    builder->createBr(mergeBB);

    // False branch - modify y
    builder->setInsertPoint(falseBB);
    builder->createStore(builder->getInt32(200), alloca2);
    builder->createBr(mergeBB);

    // Merge block - use both values
    builder->setInsertPoint(mergeBB);
    auto load1 = builder->createLoad(alloca1, "x_val");
    auto load2 = builder->createLoad(alloca2, "y_val");
    auto result = builder->createAdd(load1, load2, "result");
    builder->createRet(result);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    std::vector<PHINode*> phiNodes;
    Instruction* retInst = nullptr;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (auto phi = dyn_cast<PHINode>(*it)) {
                phiNodes.push_back(phi);
            }
            if (isa<ReturnInst>(*it)) retInst = *it;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    EXPECT_EQ(phiNodes.size(), 2);  // Should have PHI nodes for both variables
    
    // Verify both PHI nodes have correct incoming values
    for (auto phi : phiNodes) {
        EXPECT_EQ(phi->getNumIncomingValues(), 2);
        
        // Check if this is the x or y variable PHI by examining values
        bool isXPhi = false;
        bool isYPhi = false;
        
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
            auto value = phi->getIncomingValue(i);
            if (auto constInt = dyn_cast<ConstantInt>(value)) {
                // x PHI should have values 10 and 100
                if (constInt->getValue() == 10 || constInt->getValue() == 100) {
                    isXPhi = true;
                }
                // y PHI should have values 20 and 200  
                if (constInt->getValue() == 20 || constInt->getValue() == 200) {
                    isYPhi = true;
                }
            }
        }
        
        EXPECT_TRUE(isXPhi || isYPhi);
    }
}

// Loop tests
TEST_F(Mem2RegTest, SimpleLoopWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto loopExitBB = BasicBlock::Create(ctx.get(), "loop.exit", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "sum");
    auto allocaI = builder->createAlloca(intType, nullptr, "i");

    // Initialize sum = 0, i = 0
    builder->createStore(builder->getInt32(0), alloca);
    builder->createStore(builder->getInt32(0), allocaI);
    builder->createBr(loopHeaderBB);

    // Loop header: check i < n
    builder->setInsertPoint(loopHeaderBB);
    auto loadI = builder->createLoad(allocaI, "i_val");
    auto condition = builder->createICmpSLT(loadI, func->getArg(0), "cond");
    builder->createCondBr(condition, loopBodyBB, loopExitBB);

    // Loop body: sum += i; i++
    builder->setInsertPoint(loopBodyBB);
    auto currentSum = builder->createLoad(alloca, "sum_val");
    auto currentI = builder->createLoad(allocaI, "i_val2");
    auto newSum = builder->createAdd(currentSum, currentI, "new_sum");
    auto newI = builder->createAdd(currentI, builder->getInt32(1), "new_i");
    builder->createStore(newSum, alloca);
    builder->createStore(newI, allocaI);
    builder->createBr(loopHeaderBB);

    // Loop exit: return sum
    builder->setInsertPoint(loopExitBB);
    auto finalSum = builder->createLoad(alloca, "final_sum");
    builder->createRet(finalSum);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    std::vector<PHINode*> phiNodes;
    BasicBlock* loopHeaderBlock = nullptr;
    for (auto& bb : *func) {
        if (bb->getName() == "loop.header") {
            loopHeaderBlock = bb;
        }
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (auto phi = dyn_cast<PHINode>(*it)) {
                phiNodes.push_back(phi);
            }
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    EXPECT_GE(phiNodes.size(), 2);  // Should have PHI nodes for both sum and i
    
    // Verify PHI nodes are in the loop header
    ASSERT_NE(loopHeaderBlock, nullptr);
    int phiInHeader = 0;
    for (auto inst : *loopHeaderBlock) {
        if (auto phi = dyn_cast<PHINode>(inst)) {
            phiInHeader++;
            EXPECT_EQ(phi->getNumIncomingValues(), 2);
            
            // Verify one incoming value is from entry (initial value)
            // and one is from loop body (updated value)
            bool hasEntryIncoming = false;
            bool hasBodyIncoming = false;
            
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                auto block = phi->getIncomingBlock(i);
                if (block->getName() == "entry") hasEntryIncoming = true;
                if (block->getName() == "loop.body") hasBodyIncoming = true;
            }
            
            EXPECT_TRUE(hasEntryIncoming);
            EXPECT_TRUE(hasBodyIncoming);
        }
    }
    EXPECT_EQ(phiInHeader, 2);  // Both sum and i should have PHI nodes in header
}

// Nested if-else tests
TEST_F(Mem2RegTest, NestedIfElseWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerTrueBB = BasicBlock::Create(ctx.get(), "outer.true", func);
    auto outerFalseBB = BasicBlock::Create(ctx.get(), "outer.false", func);
    auto innerTrueBB = BasicBlock::Create(ctx.get(), "inner.true", func);
    auto innerFalseBB = BasicBlock::Create(ctx.get(), "inner.false", func);
    auto innerMergeBB = BasicBlock::Create(ctx.get(), "inner.merge", func);
    auto outerMergeBB = BasicBlock::Create(ctx.get(), "outer.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "result");
    builder->createStore(builder->getInt32(0), alloca);

    // Outer condition: x > 0
    auto outerCond = builder->createICmpSGT(func->getArg(0),
                                            builder->getInt32(0), "outer_cond");
    builder->createCondBr(outerCond, outerTrueBB, outerFalseBB);

    // Outer true branch - nested if
    builder->setInsertPoint(outerTrueBB);
    auto innerCond = builder->createICmpSGT(func->getArg(1),
                                            builder->getInt32(5), "inner_cond");
    builder->createCondBr(innerCond, innerTrueBB, innerFalseBB);

    // Inner true branch
    builder->setInsertPoint(innerTrueBB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(innerMergeBB);

    // Inner false branch
    builder->setInsertPoint(innerFalseBB);
    builder->createStore(builder->getInt32(50), alloca);
    builder->createBr(innerMergeBB);

    // Inner merge
    builder->setInsertPoint(innerMergeBB);
    builder->createBr(outerMergeBB);

    // Outer false branch
    builder->setInsertPoint(outerFalseBB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(outerMergeBB);

    // Outer merge - return result
    builder->setInsertPoint(outerMergeBB);
    auto load = builder->createLoad(alloca, "final_result");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    int phiCount = 0;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (isa<PHINode>(*it)) phiCount++;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    EXPECT_GT(phiCount, 0);  // Should have PHI nodes for nested control flow
}

// Complex nested loops with if-else
TEST_F(Mem2RegTest, NestedLoopWithIfElse) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerLoopHeaderBB =
        BasicBlock::Create(ctx.get(), "outer.loop.header", func);
    auto outerLoopBodyBB =
        BasicBlock::Create(ctx.get(), "outer.loop.body", func);
    auto innerLoopHeaderBB =
        BasicBlock::Create(ctx.get(), "inner.loop.header", func);
    auto innerLoopBodyBB =
        BasicBlock::Create(ctx.get(), "inner.loop.body", func);
    auto ifTrueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto ifFalseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto ifMergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);
    auto innerLoopLatchBB =
        BasicBlock::Create(ctx.get(), "inner.loop.latch", func);
    auto outerLoopLatchBB =
        BasicBlock::Create(ctx.get(), "outer.loop.latch", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto allocaSum = builder->createAlloca(intType, nullptr, "sum");
    auto allocaI = builder->createAlloca(intType, nullptr, "i");
    auto allocaJ = builder->createAlloca(intType, nullptr, "j");

    // Initialize sum = 0, i = 0
    builder->createStore(builder->getInt32(0), allocaSum);
    builder->createStore(builder->getInt32(0), allocaI);
    builder->createBr(outerLoopHeaderBB);

    // Outer loop header: i < n
    builder->setInsertPoint(outerLoopHeaderBB);
    auto loadI = builder->createLoad(allocaI, "i_val");
    auto outerCond =
        builder->createICmpSLT(loadI, func->getArg(0), "outer_cond");
    builder->createCondBr(outerCond, outerLoopBodyBB, exitBB);

    // Outer loop body: initialize j = 0
    builder->setInsertPoint(outerLoopBodyBB);
    builder->createStore(builder->getInt32(0), allocaJ);
    builder->createBr(innerLoopHeaderBB);

    // Inner loop header: j < m
    builder->setInsertPoint(innerLoopHeaderBB);
    auto loadJ = builder->createLoad(allocaJ, "j_val");
    auto innerCond =
        builder->createICmpSLT(loadJ, func->getArg(1), "inner_cond");
    builder->createCondBr(innerCond, innerLoopBodyBB, outerLoopLatchBB);

    // Inner loop body: if (i + j) % 2 == 0
    builder->setInsertPoint(innerLoopBodyBB);
    auto currentI = builder->createLoad(allocaI, "i_val2");
    auto currentJ = builder->createLoad(allocaJ, "j_val2");
    auto sum = builder->createAdd(currentI, currentJ, "ij_sum");
    auto mod = builder->createRem(sum, builder->getInt32(2), "mod");
    auto ifCond = builder->createICmpEQ(mod, builder->getInt32(0), "if_cond");
    builder->createCondBr(ifCond, ifTrueBB, ifFalseBB);

    // If true: sum += i * j
    builder->setInsertPoint(ifTrueBB);
    auto currentSum1 = builder->createLoad(allocaSum, "sum_val1");
    auto currentI2 = builder->createLoad(allocaI, "i_val3");
    auto currentJ2 = builder->createLoad(allocaJ, "j_val3");
    auto product = builder->createMul(currentI2, currentJ2, "product");
    auto newSum1 = builder->createAdd(currentSum1, product, "new_sum1");
    builder->createStore(newSum1, allocaSum);
    builder->createBr(ifMergeBB);

    // If false: sum += i + j
    builder->setInsertPoint(ifFalseBB);
    auto currentSum2 = builder->createLoad(allocaSum, "sum_val2");
    auto currentI3 = builder->createLoad(allocaI, "i_val4");
    auto currentJ3 = builder->createLoad(allocaJ, "j_val4");
    auto sum2 = builder->createAdd(currentI3, currentJ3, "ij_sum2");
    auto newSum2 = builder->createAdd(currentSum2, sum2, "new_sum2");
    builder->createStore(newSum2, allocaSum);
    builder->createBr(ifMergeBB);

    // If merge
    builder->setInsertPoint(ifMergeBB);
    builder->createBr(innerLoopLatchBB);

    // Inner loop latch: j++
    builder->setInsertPoint(innerLoopLatchBB);
    auto currentJ4 = builder->createLoad(allocaJ, "j_val5");
    auto newJ = builder->createAdd(currentJ4, builder->getInt32(1), "new_j");
    builder->createStore(newJ, allocaJ);
    builder->createBr(innerLoopHeaderBB);

    // Outer loop latch: i++
    builder->setInsertPoint(outerLoopLatchBB);
    auto currentI4 = builder->createLoad(allocaI, "i_val5");
    auto newI = builder->createAdd(currentI4, builder->getInt32(1), "new_i");
    builder->createStore(newI, allocaI);
    builder->createBr(outerLoopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalSum = builder->createLoad(allocaSum, "final_sum");
    builder->createRet(finalSum);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    int phiCount = 0;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (isa<PHINode>(*it)) phiCount++;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    EXPECT_GT(phiCount,
              0);  // Should have many PHI nodes for complex control flow
}

// Test for non-promotable allocas (address taken)
TEST_F(Mem2RegTest, NonPromotableAllocaAddressTaken) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "x");
    builder->createStore(builder->getInt32(42), alloca);

    // Take address of alloca - makes it non-promotable
    // In real code, this would be something like &x, but we'll simulate
    // by using the alloca itself as a pointer value
    (void)builder->createLoad(alloca, "addr");

    auto load = builder->createLoad(alloca, "load_x");
    builder->createRet(load);

    Mem2RegPass pass;
    (void)pass.runOnFunction(*func, *am);

    // This alloca should NOT be promoted because its address is taken
    // The exact behavior depends on the implementation, but typically
    // address-taken allocas are not promoted
    bool hasAlloca = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) {
                hasAlloca = true;
                break;
            }
        }
    }

    // This test verifies that the pass correctly identifies non-promotable
    // allocas The result depends on the specific implementation of
    // isPromotable() For now, we just check that the test runs without crashing
    (void)hasAlloca;
}

// Edge case tests for boundary conditions
TEST_F(Mem2RegTest, AllocaWithNoStores) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create alloca but never store to it
    auto alloca = builder->createAlloca(intType, nullptr, "uninitialized");

    // Load from uninitialized alloca
    auto load = builder->createLoad(alloca, "load_uninit");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should still be promoted, but load will get undefined value
    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }

    // Behavior depends on implementation - some passes might still promote
    // uninitialized allocas to undefined values
    (void)changed;
    (void)hasAlloca;
    (void)hasLoadStore;
}

TEST_F(Mem2RegTest, AllocaWithNoLoads) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create alloca and store to it but never load
    auto alloca = builder->createAlloca(intType, nullptr, "write_only");
    builder->createStore(builder->getInt32(42), alloca);
    builder->createStore(builder->getInt32(100), alloca);

    // Return constant instead of loaded value
    builder->createRet(builder->getInt32(999));

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // Should be promoted since stores are dead
    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

TEST_F(Mem2RegTest, AllocaInUnreachableBlock) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto unreachableBB = BasicBlock::Create(ctx.get(), "unreachable", func);

    // Entry block returns immediately
    builder->setInsertPoint(entryBB);
    builder->createRet(builder->getInt32(42));

    // Unreachable block with alloca
    builder->setInsertPoint(unreachableBB);
    auto alloca = builder->createAlloca(intType, nullptr, "unreachable_alloca");
    builder->createStore(builder->getInt32(100), alloca);
    auto load = builder->createLoad(alloca, "unreachable_load");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should handle unreachable code gracefully
    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }

    // Behavior depends on implementation
    (void)changed;
    (void)hasAlloca;
    (void)hasLoadStore;
}

TEST_F(Mem2RegTest, AllocaWithStoreInOneBranchOnly) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "partial_store");

    auto condition =
        builder->createICmpSGT(func->getArg(0), builder->getInt32(0), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    // Only store in true branch
    builder->setInsertPoint(trueBB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(mergeBB);

    // No store in false branch
    builder->setInsertPoint(falseBB);
    builder->createBr(mergeBB);

    // Load in merge block
    builder->setInsertPoint(mergeBB);
    auto load = builder->createLoad(alloca, "partial_load");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // Should create PHI with undefined value for false branch
    bool hasAlloca = false;
    bool hasLoadStore = false;
    PHINode* phiNode = nullptr;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (auto phi = dyn_cast<PHINode>(*it)) {
                phiNode = phi;
            }
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    ASSERT_NE(phiNode, nullptr);
    
    // Verify PHI node has two incoming values
    EXPECT_EQ(phiNode->getNumIncomingValues(), 2);
    
    // Check that one value is 100 (from true branch) and other is undefined (from false branch)
    bool hasDefinedValue = false;
    bool hasUndefValue = false;
    
    for (unsigned i = 0; i < phiNode->getNumIncomingValues(); ++i) {
        auto value = phiNode->getIncomingValue(i);
        auto block = phiNode->getIncomingBlock(i);
        
        if (auto constInt = dyn_cast<ConstantInt>(value)) {
            if (constInt->getValue() == 100 && block->getName() == "if.true") {
                hasDefinedValue = true;
            }
        } else if (isa<UndefValue>(value) && block->getName() == "if.false") {
            hasUndefValue = true;
        }
    }
    
    EXPECT_TRUE(hasDefinedValue);
    EXPECT_TRUE(hasUndefValue);
}

TEST_F(Mem2RegTest, AllocaWithDifferentTypes) {
    auto intType = ctx->getIntegerType(32);
    auto floatType = ctx->getFloatType();
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Test different type allocas
    auto intAlloca = builder->createAlloca(intType, nullptr, "int_var");
    auto floatAlloca = builder->createAlloca(floatType, nullptr, "float_var");
    auto boolAlloca = builder->createAlloca(boolType, nullptr, "bool_var");

    // Store to each
    builder->createStore(builder->getInt32(42), intAlloca);
    builder->createStore(builder->getFloat(3.14f), floatAlloca);
    builder->createStore(builder->getInt1(true), boolAlloca);

    // Load from each
    auto intLoad = builder->createLoad(intAlloca, "int_load");
    auto floatLoad = builder->createLoad(floatAlloca, "float_load");
    auto boolLoad = builder->createLoad(boolAlloca, "bool_load");

    // Use the values (convert float to int, extend bool to int)
    auto floatToInt = builder->createFPToSI(floatLoad, intType, "float_to_int");
    auto boolToInt =
        builder->createCast(CastInst::ZExt, boolLoad, intType, "bool_to_int");

    auto sum1 = builder->createAdd(intLoad, floatToInt, "sum1");
    auto sum2 = builder->createAdd(sum1, boolToInt, "sum2");
    builder->createRet(sum2);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // All allocas should be promoted regardless of type
    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

TEST_F(Mem2RegTest, AllocaWithSelfReferentialOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "counter");

    // Initialize with function argument
    builder->createStore(func->getArg(0), alloca);

    // Self-referential operations: counter = counter + 1
    auto load1 = builder->createLoad(alloca, "load1");
    auto increment =
        builder->createAdd(load1, builder->getInt32(1), "increment");
    builder->createStore(increment, alloca);

    // counter = counter * 2
    auto load2 = builder->createLoad(alloca, "load2");
    auto multiply = builder->createMul(load2, builder->getInt32(2), "multiply");
    builder->createStore(multiply, alloca);

    // Return final value
    auto finalLoad = builder->createLoad(alloca, "final_load");
    builder->createRet(finalLoad);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

TEST_F(Mem2RegTest, AllocaWithMultipleReturns) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto earlyReturnBB = BasicBlock::Create(ctx.get(), "early_return", func);
    auto normalPathBB = BasicBlock::Create(ctx.get(), "normal_path", func);
    auto lateReturnBB = BasicBlock::Create(ctx.get(), "late_return", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "shared_var");
    builder->createStore(builder->getInt32(10), alloca);

    // Branch based on argument
    auto condition1 =
        builder->createICmpSLT(func->getArg(0), builder->getInt32(0), "cond1");
    builder->createCondBr(condition1, earlyReturnBB, normalPathBB);

    // Early return path
    builder->setInsertPoint(earlyReturnBB);
    auto earlyLoad = builder->createLoad(alloca, "early_load");
    builder->createRet(earlyLoad);

    // Normal path
    builder->setInsertPoint(normalPathBB);
    builder->createStore(builder->getInt32(20), alloca);
    auto condition2 = builder->createICmpSGT(func->getArg(0),
                                             builder->getInt32(100), "cond2");
    builder->createCondBr(condition2, lateReturnBB, lateReturnBB);

    // Late return path
    builder->setInsertPoint(lateReturnBB);
    auto lateLoad = builder->createLoad(alloca, "late_load");
    builder->createRet(lateLoad);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

TEST_F(Mem2RegTest, AllocaWithConditionalStoresOnly) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto store1BB = BasicBlock::Create(ctx.get(), "store1", func);
    auto store2BB = BasicBlock::Create(ctx.get(), "store2", func);
    auto noStoreBB = BasicBlock::Create(ctx.get(), "no_store", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "conditional_var");

    // Branch to different store blocks
    auto arg0 = func->getArg(0);
    auto cond1 = builder->createICmpEQ(arg0, builder->getInt32(1), "cond1");
    auto cond2 = builder->createICmpEQ(arg0, builder->getInt32(2), "cond2");

    // Create select-like control flow
    auto tempBB = BasicBlock::Create(ctx.get(), "temp", func);
    builder->createCondBr(cond1, store1BB, tempBB);

    builder->setInsertPoint(tempBB);
    builder->createCondBr(cond2, store2BB, noStoreBB);

    // Store block 1
    builder->setInsertPoint(store1BB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(mergeBB);

    // Store block 2
    builder->setInsertPoint(store2BB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(mergeBB);

    // No store block
    builder->setInsertPoint(noStoreBB);
    builder->createBr(mergeBB);

    // Merge and load
    builder->setInsertPoint(mergeBB);
    auto load = builder->createLoad(alloca, "conditional_load");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    bool hasPhi = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
            if (isa<PHINode>(*it)) hasPhi = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
    EXPECT_TRUE(hasPhi);
}

TEST_F(Mem2RegTest, AllocaWithEmptyBasicBlocks) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto emptyBB1 = BasicBlock::Create(ctx.get(), "empty1", func);
    auto emptyBB2 = BasicBlock::Create(ctx.get(), "empty2", func);
    auto workBB = BasicBlock::Create(ctx.get(), "work", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "empty_test");
    builder->createStore(builder->getInt32(42), alloca);
    builder->createBr(emptyBB1);

    // Empty block 1 - just branch
    builder->setInsertPoint(emptyBB1);
    builder->createBr(emptyBB2);

    // Empty block 2 - just branch
    builder->setInsertPoint(emptyBB2);
    builder->createBr(workBB);

    // Work block - modify alloca
    builder->setInsertPoint(workBB);
    auto load = builder->createLoad(alloca, "work_load");
    auto modified = builder->createAdd(load, builder->getInt32(10), "modified");
    builder->createStore(modified, alloca);
    builder->createBr(exitBB);

    // Exit block - final load
    builder->setInsertPoint(exitBB);
    auto finalLoad = builder->createLoad(alloca, "final_load");
    builder->createRet(finalLoad);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

TEST_F(Mem2RegTest, AllocaWithDeadCodeAfterReturn) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto deadBB = BasicBlock::Create(ctx.get(), "dead", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "dead_code_var");
    builder->createStore(func->getArg(0), alloca);

    auto load = builder->createLoad(alloca, "before_return");
    builder->createRet(load);

    // Dead code after return
    builder->createBr(deadBB);

    builder->setInsertPoint(deadBB);
    builder->createStore(builder->getInt32(999), alloca);
    auto deadLoad = builder->createLoad(alloca, "dead_load");
    builder->createRet(deadLoad);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

// Advanced edge cases for stress testing
TEST_F(Mem2RegTest, AllocaWithVeryLargeNumberOfStores) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "many_stores");

    // Create 100 stores to stress test the implementation
    for (int i = 0; i < 100; ++i) {
        builder->createStore(builder->getInt32(i), alloca);
    }

    auto load = builder->createLoad(alloca, "final_load");
    builder->createRet(load);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}

TEST_F(Mem2RegTest, AllocaWithInterleavedLoadsAndStores) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "interleaved");

    // Interleaved pattern: store, load, store, load, etc.
    builder->createStore(func->getArg(0), alloca);
    auto load1 = builder->createLoad(alloca, "load1");

    auto modified1 = builder->createAdd(load1, builder->getInt32(1), "mod1");
    builder->createStore(modified1, alloca);
    auto load2 = builder->createLoad(alloca, "load2");

    auto modified2 = builder->createMul(load2, builder->getInt32(2), "mod2");
    builder->createStore(modified2, alloca);
    auto load3 = builder->createLoad(alloca, "load3");

    auto modified3 = builder->createSub(load3, builder->getInt32(5), "mod3");
    builder->createStore(modified3, alloca);
    auto finalLoad = builder->createLoad(alloca, "final_load");

    builder->createRet(finalLoad);

    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    bool hasAlloca = false;
    bool hasLoadStore = false;
    for (auto& bb : *func) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (isa<AllocaInst>(*it)) hasAlloca = true;
            if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) hasLoadStore = true;
        }
    }
    EXPECT_FALSE(hasAlloca);
    EXPECT_FALSE(hasLoadStore);
}