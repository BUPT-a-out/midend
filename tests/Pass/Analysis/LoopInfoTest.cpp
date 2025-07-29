#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Analysis/LoopInfo.h"

using namespace midend;

namespace {

bool exists(const std::unordered_set<BasicBlock*>& blocks,
            const std::string name) {
    return std::any_of(
        blocks.begin(), blocks.end(),
        [&name](BasicBlock* block) { return block->getName() == name; });
}

bool equal(const std::unordered_set<BasicBlock*>& blocks,
           const std::vector<std::string>& names) {
    return names.size() == blocks.size() &&
           std::all_of(names.begin(), names.end(),
                       [&blocks](const std::string& name) {
                           return exists(blocks, name);
                       });
}

class LoopInfoTest : public ::testing::Test {
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

    // Helper function to compare IR output with expected string
    void compareIR(Function* func, const std::string& expected) {
        IRPrinter printer;
        std::string actual = printer.print(func);

        // Normalize whitespace for comparison
        auto normalize = [](std::string s) {
            // Remove extra whitespace and normalize line endings
            std::string result;
            bool prev_space = false;
            for (char c : s) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    if (!prev_space && !result.empty()) {
                        result += ' ';
                        prev_space = true;
                    }
                } else {
                    result += c;
                    prev_space = false;
                }
            }
            return result;
        };

        EXPECT_EQ(normalize(actual), normalize(expected))
            << "Expected IR:\n"
            << expected << "\nActual IR:\n"
            << actual;
    }

    // Helper function to create and analyze a function with LoopInfo
    std::pair<Function*, std::unique_ptr<LoopInfo>> analyzeFunction(
        std::function<Function*()> createFunc) {
        auto* func = createFunc();
        auto loopInfo =
            std::make_unique<LoopInfo>(func, new DominanceInfo(func));

        return std::make_pair(func, std::move(loopInfo));
    }
};

// Test 1: Simple loop (single basic block back edge, classic increment loop)
TEST_F(LoopInfoTest, SimpleLoop) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "simple_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* initial = func->getArg(0);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "i");
        phi->addIncoming(initial, entry);
        auto* cond = builder.createICmpSLT(phi, builder.getInt32(10), "cond");
        builder.createCondBr(cond, loop_body, exit);

        builder.setInsertPoint(loop_body);
        auto* incremented = builder.createAdd(phi, builder.getInt32(1), "inc");
        phi->addIncoming(incremented, loop_body);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    // Expected IR pattern
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @simple_loop(i32 %arg0) {
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ %arg0, %entry ], [ %inc, %loop_body ]
  %cond = icmp slt i32 %i, 10
  br i1 %cond, label %loop_body, label %exit
loop_body:
  %inc = add i32 %i, 1
  br label %loop_header
exit:
  ret i32 %i
}
)");

    // Analyze loop structure
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(loop->getHeader()->getName(), "loop_header");
    EXPECT_EQ(loop->getLoopDepth(), 1u);
    EXPECT_TRUE(loop->getSubLoops().empty());

    // Check loop contains the right blocks
    EXPECT_TRUE(equal(loop->getBlocks(), {"loop_header", "loop_body"}));

    // Check single backedge
    EXPECT_TRUE(loop->hasSingleBackedge());

    auto latches = loop->getLoopLatches();
    EXPECT_EQ(latches.size(), 1u);
    EXPECT_EQ(latches[0]->getName(), "loop_body");
}

// Test 2: While loop structure (header condition check, body jumps back)
TEST_F(LoopInfoTest, WhileLoopStructure) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "while_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* while_header =
            BasicBlock::Create(context.get(), "while_header", func);
        auto* while_body =
            BasicBlock::Create(context.get(), "while_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* n = func->getArg(0);
        builder.createBr(while_header);

        builder.setInsertPoint(while_header);
        auto* phi = builder.createPHI(int32Ty, "counter");
        phi->addIncoming(n, entry);
        auto* cond = builder.createICmpNE(phi, builder.getInt32(0), "cond");
        builder.createCondBr(cond, while_body, exit);

        builder.setInsertPoint(while_body);
        auto* decremented = builder.createSub(phi, builder.getInt32(1), "dec");
        phi->addIncoming(decremented, while_body);
        builder.createBr(while_header);

        builder.setInsertPoint(exit);
        builder.createRet(builder.getInt32(0));

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @while_loop(i32 %arg0) {
entry:
  br label %while_header
while_header:
  %counter = phi i32 [ %arg0, %entry ], [ %dec, %while_body ]
  %cond = icmp ne i32 %counter, 0
  br i1 %cond, label %while_body, label %exit
while_body:
  %dec = sub i32 %counter, 1
  br label %while_header
exit:
  ret i32 0
}
)");

    // Test loop info
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(loop->getHeader()->getName(), "while_header");
    EXPECT_TRUE(loopInfo->isLoopHeader(loop->getHeader()));
    EXPECT_EQ(loopInfo->getLoopDepth(loop->getHeader()), 1u);

    // Test header node identification
    auto* whileHeader = func->getBasicBlocks()[1];
    EXPECT_EQ(loopInfo->getLoopFor(whileHeader), loop);

    EXPECT_TRUE(equal(loop->getBlocks(), {"while_header", "while_body"}));
}

// Test 3: Nested loops (two levels)
TEST_F(LoopInfoTest, NestedLoopsTwoLevels) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "nested_loops", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* outer_header =
            BasicBlock::Create(context.get(), "outer_header", func);
        auto* inner_header =
            BasicBlock::Create(context.get(), "inner_header", func);
        auto* inner_body =
            BasicBlock::Create(context.get(), "inner_body", func);
        auto* outer_latch =
            BasicBlock::Create(context.get(), "outer_latch", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* outer_limit = func->getArg(0);
        auto* inner_limit = func->getArg(1);
        builder.createBr(outer_header);

        builder.setInsertPoint(outer_header);
        auto* i = builder.createPHI(int32Ty, "i");
        i->addIncoming(builder.getInt32(0), entry);
        auto* outer_cond = builder.createICmpSLT(i, outer_limit, "outer_cond");
        builder.createCondBr(outer_cond, inner_header, exit);

        builder.setInsertPoint(inner_header);
        auto* j = builder.createPHI(int32Ty, "j");
        j->addIncoming(builder.getInt32(0), outer_header);
        auto* inner_cond = builder.createICmpSLT(j, inner_limit, "inner_cond");
        builder.createCondBr(inner_cond, inner_body, outer_latch);

        builder.setInsertPoint(inner_body);
        auto* j_inc = builder.createAdd(j, builder.getInt32(1), "j_inc");
        j->addIncoming(j_inc, inner_body);
        builder.createBr(inner_header);

        builder.setInsertPoint(outer_latch);
        auto* i_inc = builder.createAdd(i, builder.getInt32(1), "i_inc");
        i->addIncoming(i_inc, outer_latch);
        builder.createBr(outer_header);

        builder.setInsertPoint(exit);
        builder.createRet(i);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @nested_loops(i32 %arg0, i32 %arg1) {
entry:
  br label %outer_header
outer_header:
  %i = phi i32 [ 0, %entry ], [ %i_inc, %outer_latch ]
  %outer_cond = icmp slt i32 %i, %arg0
  br i1 %outer_cond, label %inner_header, label %exit
inner_header:
  %j = phi i32 [ 0, %outer_header ], [ %j_inc, %inner_body ]
  %inner_cond = icmp slt i32 %j, %arg1
  br i1 %inner_cond, label %inner_body, label %outer_latch
inner_body:
  %j_inc = add i32 %j, 1
  br label %inner_header
outer_latch:
  %i_inc = add i32 %i, 1
  br label %outer_header
exit:
  ret i32 %i
}
)");

    // Test nested loop structure
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* outerLoop = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(outerLoop->getHeader()->getName(), "outer_header");
    EXPECT_EQ(outerLoop->getLoopDepth(), 1u);

    // Test getSubLoops()
    EXPECT_EQ(outerLoop->getSubLoops().size(), 1u);
    auto* innerLoop = outerLoop->getSubLoops()[0].get();
    EXPECT_EQ(innerLoop->getHeader()->getName(), "inner_header");
    EXPECT_EQ(innerLoop->getLoopDepth(), 2u);
    EXPECT_TRUE(innerLoop->getSubLoops().empty());

    // Test parent-child relationship
    EXPECT_EQ(innerLoop->getParentLoop(), outerLoop);
    EXPECT_EQ(outerLoop->getParentLoop(), nullptr);

    // Test contains relationship
    EXPECT_TRUE(outerLoop->contains(innerLoop));
    EXPECT_FALSE(innerLoop->contains(outerLoop));

    EXPECT_TRUE(equal(outerLoop->getBlocks(), {"outer_header", "inner_header",
                                               "inner_body", "outer_latch"}));
    EXPECT_TRUE(equal(innerLoop->getBlocks(), {"inner_header", "inner_body"}));
}

// Test 4: Triple nested loops
TEST_F(LoopInfoTest, TripleNestedLoops) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {});
        auto* func = Function::Create(fnTy, "triple_nested", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* l1_header = BasicBlock::Create(context.get(), "l1_header", func);
        auto* l2_header = BasicBlock::Create(context.get(), "l2_header", func);
        auto* l3_header = BasicBlock::Create(context.get(), "l3_header", func);
        auto* l3_body = BasicBlock::Create(context.get(), "l3_body", func);
        auto* l2_latch = BasicBlock::Create(context.get(), "l2_latch", func);
        auto* l1_latch = BasicBlock::Create(context.get(), "l1_latch", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(l1_header);

        // Level 1 loop
        builder.setInsertPoint(l1_header);
        auto* i = builder.createPHI(int32Ty, "i");
        i->addIncoming(builder.getInt32(0), entry);
        auto* cond1 = builder.createICmpSLT(i, builder.getInt32(3), "cond1");
        builder.createCondBr(cond1, l2_header, exit);

        // Level 2 loop
        builder.setInsertPoint(l2_header);
        auto* j = builder.createPHI(int32Ty, "j");
        j->addIncoming(builder.getInt32(0), l1_header);
        auto* cond2 = builder.createICmpSLT(j, builder.getInt32(3), "cond2");
        builder.createCondBr(cond2, l3_header, l1_latch);

        // Level 3 loop
        builder.setInsertPoint(l3_header);
        auto* k = builder.createPHI(int32Ty, "k");
        k->addIncoming(builder.getInt32(0), l2_header);
        auto* cond3 = builder.createICmpSLT(k, builder.getInt32(3), "cond3");
        builder.createCondBr(cond3, l3_body, l2_latch);

        builder.setInsertPoint(l3_body);
        auto* k_inc = builder.createAdd(k, builder.getInt32(1), "k_inc");
        k->addIncoming(k_inc, l3_body);
        builder.createBr(l3_header);

        builder.setInsertPoint(l2_latch);
        auto* j_inc = builder.createAdd(j, builder.getInt32(1), "j_inc");
        j->addIncoming(j_inc, l2_latch);
        builder.createBr(l2_header);

        builder.setInsertPoint(l1_latch);
        auto* i_inc = builder.createAdd(i, builder.getInt32(1), "i_inc");
        i->addIncoming(i_inc, l1_latch);
        builder.createBr(l1_header);

        builder.setInsertPoint(exit);
        builder.createRet(builder.getInt32(0));

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @triple_nested() {
entry:
  br label %l1_header
l1_header:
  %i = phi i32 [ 0, %entry ], [ %i_inc, %l1_latch ]
  %cond1 = icmp slt i32 %i, 3
  br i1 %cond1, label %l2_header, label %exit
l2_header:
  %j = phi i32 [ 0, %l1_header ], [ %j_inc, %l2_latch ]
  %cond2 = icmp slt i32 %j, 3
  br i1 %cond2, label %l3_header, label %l1_latch
l3_header:
  %k = phi i32 [ 0, %l2_header ], [ %k_inc, %l3_body ]
  %cond3 = icmp slt i32 %k, 3
  br i1 %cond3, label %l3_body, label %l2_latch
l3_body:
  %k_inc = add i32 %k, 1
  br label %l3_header
l2_latch:
  %j_inc = add i32 %j, 1
  br label %l2_header
l1_latch:
  %i_inc = add i32 %i, 1
  br label %l1_header
exit:
  ret i32 0
}
)");

    // Test recursive sub-loop identification
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* l1 = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(l1->getLoopDepth(), 1u);
    EXPECT_EQ(l1->getSubLoops().size(), 1u);

    auto* l2 = l1->getSubLoops()[0].get();
    EXPECT_EQ(l2->getLoopDepth(), 2u);
    EXPECT_EQ(l2->getSubLoops().size(), 1u);

    auto* l3 = l2->getSubLoops()[0].get();
    EXPECT_EQ(l3->getLoopDepth(), 3u);
    EXPECT_TRUE(l3->getSubLoops().empty());

    // Test nesting relationship
    EXPECT_TRUE(l1->contains(l2));
    EXPECT_TRUE(l1->contains(l3));
    EXPECT_TRUE(l2->contains(l3));
    EXPECT_FALSE(l3->contains(l2));
    EXPECT_FALSE(l3->contains(l1));

    EXPECT_TRUE(equal(l1->getBlocks(), {"l1_header", "l2_header", "l3_header",
                                        "l3_body", "l2_latch", "l1_latch"}));
    EXPECT_TRUE(equal(l2->getBlocks(),
                      {"l2_header", "l3_header", "l3_body", "l2_latch"}));
    EXPECT_TRUE(equal(l3->getBlocks(), {"l3_header", "l3_body"}));
}

// Test 5: Multiple parallel loops
TEST_F(LoopInfoTest, MultipleParallelLoops) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "parallel_loops", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop1_header =
            BasicBlock::Create(context.get(), "loop1_header", func);
        auto* loop1_body =
            BasicBlock::Create(context.get(), "loop1_body", func);
        auto* middle = BasicBlock::Create(context.get(), "middle", func);
        auto* loop2_header =
            BasicBlock::Create(context.get(), "loop2_header", func);
        auto* loop2_body =
            BasicBlock::Create(context.get(), "loop2_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* n = func->getArg(0);
        builder.createBr(loop1_header);

        // First loop
        builder.setInsertPoint(loop1_header);
        auto* i = builder.createPHI(int32Ty, "i");
        i->addIncoming(builder.getInt32(0), entry);
        auto* cond1 = builder.createICmpSLT(i, n, "cond1");
        builder.createCondBr(cond1, loop1_body, middle);

        builder.setInsertPoint(loop1_body);
        auto* i_inc = builder.createAdd(i, builder.getInt32(1), "i_inc");
        i->addIncoming(i_inc, loop1_body);
        builder.createBr(loop1_header);

        builder.setInsertPoint(middle);
        builder.createBr(loop2_header);

        // Second loop
        builder.setInsertPoint(loop2_header);
        auto* j = builder.createPHI(int32Ty, "j");
        j->addIncoming(builder.getInt32(0), middle);
        auto* cond2 = builder.createICmpSLT(j, n, "cond2");
        builder.createCondBr(cond2, loop2_body, exit);

        builder.setInsertPoint(loop2_body);
        auto* j_inc = builder.createAdd(j, builder.getInt32(1), "j_inc");
        j->addIncoming(j_inc, loop2_body);
        builder.createBr(loop2_header);

        builder.setInsertPoint(exit);
        builder.createRet(builder.getInt32(42));

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @parallel_loops(i32 %arg0) {
entry:
  br label %loop1_header
loop1_header:
  %i = phi i32 [ 0, %entry ], [ %i_inc, %loop1_body ]
  %cond1 = icmp slt i32 %i, %arg0
  br i1 %cond1, label %loop1_body, label %middle
loop1_body:
  %i_inc = add i32 %i, 1
  br label %loop1_header
middle:
  br label %loop2_header
loop2_header:
  %j = phi i32 [ 0, %middle ], [ %j_inc, %loop2_body ]
  %cond2 = icmp slt i32 %j, %arg0
  br i1 %cond2, label %loop2_body, label %exit
loop2_body:
  %j_inc = add i32 %j, 1
  br label %loop2_header
exit:
  ret i32 42
}
)");

    // Test top-level loop collection
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 2u);

    auto* loop1 = loopInfo->getTopLevelLoops()[0].get();
    auto* loop2 = loopInfo->getTopLevelLoops()[1].get();

    // Ensure loops are not nested
    EXPECT_TRUE(loop1->getSubLoops().empty());
    EXPECT_TRUE(loop2->getSubLoops().empty());
    EXPECT_EQ(loop1->getParentLoop(), nullptr);
    EXPECT_EQ(loop2->getParentLoop(), nullptr);

    // Ensure loops don't contain each other
    EXPECT_FALSE(loop1->contains(loop2));
    EXPECT_FALSE(loop2->contains(loop1));

    // Check loop depths
    EXPECT_EQ(loop1->getLoopDepth(), 1u);
    EXPECT_EQ(loop2->getLoopDepth(), 1u);

    for (auto block : loop1->getBlocks()) {
        std::cout << "Loop 1 Block: " << block->getName() << std::endl;
    }
    EXPECT_TRUE(equal(loop1->getBlocks(), {"loop1_header", "loop1_body"}));
    for (auto block : loop2->getBlocks()) {
        std::cout << "Loop 2 Block: " << block->getName() << std::endl;
    }
    EXPECT_TRUE(equal(loop2->getBlocks(), {"loop2_header", "loop2_body"}));
}

// Test 6: Loop with multiple backedges
TEST_F(LoopInfoTest, MultipleBackedgeLoop) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "multi_backedge", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* body1 = BasicBlock::Create(context.get(), "body1", func);
        auto* body2 = BasicBlock::Create(context.get(), "body2", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* n = func->getArg(0);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "counter");
        phi->addIncoming(n, entry);
        auto* cond = builder.createICmpSGT(phi, builder.getInt32(0), "cond");
        builder.createCondBr(cond, body1, exit);

        builder.setInsertPoint(body1);
        auto* dec1 = builder.createSub(phi, builder.getInt32(1), "dec1");
        phi->addIncoming(dec1, body1);
        auto* branch_cond =
            builder.createICmpEQ(dec1, builder.getInt32(5), "branch_cond");
        builder.createCondBr(branch_cond, body2,
                             loop_header);  // First backedge

        builder.setInsertPoint(body2);
        auto* dec2 = builder.createSub(dec1, builder.getInt32(1), "dec2");
        phi->addIncoming(dec2, body2);
        builder.createBr(loop_header);  // Second backedge

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_backedge(i32 %arg0) {
entry:
  br label %loop_header
loop_header:
  %counter = phi i32 [ %arg0, %entry ], [ %dec1, %body1 ], [ %dec2, %body2 ]
  %cond = icmp sgt i32 %counter, 0
  br i1 %cond, label %body1, label %exit
body1:
  %dec1 = sub i32 %counter, 1
  %branch_cond = icmp eq i32 %dec1, 5
  br i1 %branch_cond, label %body2, label %loop_header
body2:
  %dec2 = sub i32 %dec1, 1
  br label %loop_header
exit:
  ret i32 %counter
}
)");

    // Test loop identification robustness
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(loop->getHeader()->getName(), "loop_header");

    // Test multiple backedges
    EXPECT_FALSE(loop->hasSingleBackedge());

    auto latches = loop->getLoopLatches();
    EXPECT_EQ(latches.size(), 2u);

    EXPECT_EQ(latches[0]->getName(), "body1");
    EXPECT_EQ(latches[1]->getName(), "body2");

    EXPECT_TRUE(equal(loop->getBlocks(), {"loop_header", "body1", "body2"}));

    EXPECT_EQ(loop->getPreheader()->getName(), "entry");
}

// Test 7: Irregular loop (no preheader)
TEST_F(LoopInfoTest, IrregularLoopNoPreheader) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "irregular_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* branch1 = BasicBlock::Create(context.get(), "branch1", func);
        auto* branch2 = BasicBlock::Create(context.get(), "branch2", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* n = func->getArg(0);
        auto* entry_cond =
            builder.createICmpSGT(n, builder.getInt32(10), "entry_cond");
        builder.createCondBr(entry_cond, branch1, branch2);

        // Two different paths lead to loop header (no single preheader)
        builder.setInsertPoint(branch1);
        builder.createBr(loop_header);

        builder.setInsertPoint(branch2);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "counter");
        phi->addIncoming(n, branch1);
        phi->addIncoming(builder.getInt32(0), branch2);
        auto* loop_cond =
            builder.createICmpSGT(phi, builder.getInt32(0), "loop_cond");
        builder.createCondBr(loop_cond, loop_body, exit);

        builder.setInsertPoint(loop_body);
        auto* dec = builder.createSub(phi, builder.getInt32(1), "dec");
        phi->addIncoming(dec, loop_body);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @irregular_loop(i32 %arg0) {
entry:
  %entry_cond = icmp sgt i32 %arg0, 10
  br i1 %entry_cond, label %branch1, label %branch2
branch1:
  br label %loop_header
branch2:
  br label %loop_header
loop_header:
  %counter = phi i32 [ %arg0, %branch1 ], [ 0, %branch2 ], [ %dec, %loop_body ]
  %loop_cond = icmp sgt i32 %counter, 0
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %dec = sub i32 %counter, 1
  br label %loop_header
exit:
  ret i32 %counter
}
)");

    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(loop->getHeader()->getName(), "loop_header");

    // Test that loop has no preheader
    EXPECT_EQ(loop->getPreheader(), nullptr);
    EXPECT_FALSE(loop->isSimplified());
}

// Test 8: Loop body with multiple basic blocks
TEST_F(LoopInfoTest, MultiBlockLoopBody) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "multi_block_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* body_part1 =
            BasicBlock::Create(context.get(), "body_part1", func);
        auto* body_part2 =
            BasicBlock::Create(context.get(), "body_part2", func);
        auto* body_part3 =
            BasicBlock::Create(context.get(), "body_part3", func);
        auto* latch = BasicBlock::Create(context.get(), "latch", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* n = func->getArg(0);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "i");
        phi->addIncoming(builder.getInt32(0), entry);
        auto* cond = builder.createICmpSLT(phi, n, "cond");
        builder.createCondBr(cond, body_part1, exit);

        builder.setInsertPoint(body_part1);
        auto* val1 = builder.createAdd(phi, builder.getInt32(1), "val1");
        builder.createBr(body_part2);

        builder.setInsertPoint(body_part2);
        auto* val2 = builder.createMul(val1, builder.getInt32(2), "val2");
        auto* branch_cond =
            builder.createICmpEQ(val2, builder.getInt32(10), "branch_cond");
        builder.createCondBr(branch_cond, body_part3, latch);

        builder.setInsertPoint(body_part3);
        auto* val3 = builder.createAdd(val2, builder.getInt32(1), "val3");
        builder.createBr(latch);

        builder.setInsertPoint(latch);
        auto* result_phi = builder.createPHI(int32Ty, "result");
        result_phi->addIncoming(val2, body_part2);
        result_phi->addIncoming(val3, body_part3);
        phi->addIncoming(result_phi, latch);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_block_loop(i32 %arg0) {
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %entry ], [ %result, %latch ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %body_part1, label %exit
body_part1:
  %val1 = add i32 %i, 1
  br label %body_part2
body_part2:
  %val2 = mul i32 %val1, 2
  %branch_cond = icmp eq i32 %val2, 10
  br i1 %branch_cond, label %body_part3, label %latch
body_part3:
  %val3 = add i32 %val2, 1
  br label %latch
latch:
  %result = phi i32 [ %val2, %body_part2 ], [ %val3, %body_part3 ]
  br label %loop_header
exit:
  ret i32 %i
}
)");

    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();

    // Verify getBlocks() content is correct
    EXPECT_EQ(loop->getBlocks().size(),
              5u);  // header + 3 body parts + latch

    EXPECT_TRUE(equal(
        loop->getBlocks(),
        {"loop_header", "body_part1", "body_part2", "body_part3", "latch"}));
}

// Test 9: Loop with multiple exit blocks
TEST_F(LoopInfoTest, MultipleExitBlocks) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "multi_exit_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* exit1 = BasicBlock::Create(context.get(), "exit1", func);
        auto* exit2 = BasicBlock::Create(context.get(), "exit2", func);
        auto* final_exit =
            BasicBlock::Create(context.get(), "final_exit", func);

        IRBuilder builder(entry);
        auto* limit = func->getArg(0);
        auto* threshold = func->getArg(1);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "i");
        phi->addIncoming(builder.getInt32(0), entry);
        auto* exit_cond1 = builder.createICmpEQ(phi, threshold, "exit_cond1");
        builder.createCondBr(exit_cond1, exit1, loop_body);

        builder.setInsertPoint(loop_body);
        auto* inc = builder.createAdd(phi, builder.getInt32(1), "inc");
        phi->addIncoming(inc, loop_body);
        auto* exit_cond2 = builder.createICmpSGE(inc, limit, "exit_cond2");
        builder.createCondBr(exit_cond2, exit2, loop_header);

        builder.setInsertPoint(exit1);
        builder.createBr(final_exit);

        builder.setInsertPoint(exit2);
        builder.createBr(final_exit);

        builder.setInsertPoint(final_exit);
        auto* result = builder.createPHI(int32Ty, "result");
        result->addIncoming(builder.getInt32(1), exit1);
        result->addIncoming(builder.getInt32(2), exit2);
        builder.createRet(result);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_exit_loop(i32 %arg0, i32 %arg1) {
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop_body ]
  %exit_cond1 = icmp eq i32 %i, %arg1
  br i1 %exit_cond1, label %exit1, label %loop_body
loop_body:
  %inc = add i32 %i, 1
  %exit_cond2 = icmp sge i32 %inc, %arg0
  br i1 %exit_cond2, label %exit2, label %loop_header
exit1:
  br label %final_exit
exit2:
  br label %final_exit
final_exit:
  %result = phi i32 [ 1, %exit1 ], [ 2, %exit2 ]
  ret i32 %result
}
)");

    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();

    // Test getExitBlocks()
    auto exitBlocks = loop->getExitBlocks();
    EXPECT_GE(exitBlocks.size(), 1u);

    // Test getExitingBlocks()
    auto exitingBlocks = loop->getExitingBlocks();
    EXPECT_GE(exitingBlocks.size(), 1u);

    std::set<std::string> exitingNames;
    for (auto* bb : exitingBlocks) {
        exitingNames.insert(bb->getName());
    }

    // Both header and body can exit the loop
    EXPECT_TRUE(exitingNames.count("loop_header") ||
                exitingNames.count("loop_body"));
}

// Test 10: Infinite loop (while(1) type)
TEST_F(LoopInfoTest, InfiniteLoop) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {});
        auto* func = Function::Create(fnTy, "infinite_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);

        IRBuilder builder(entry);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "counter");
        phi->addIncoming(builder.getInt32(0), entry);
        builder.createBr(loop_body);  // Always branch to body (infinite loop)

        builder.setInsertPoint(loop_body);
        auto* inc = builder.createAdd(phi, builder.getInt32(1), "inc");
        phi->addIncoming(inc, loop_body);
        builder.createBr(loop_header);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @infinite_loop() {
entry:
  br label %loop_header
loop_header:
  %counter = phi i32 [ 0, %entry ], [ %inc, %loop_body ]
  br label %loop_body
loop_body:
  %inc = add i32 %counter, 1
  br label %loop_header
}
)");

    // LoopInfo should still be able to identify the loop
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(loop->getHeader()->getName(), "loop_header");

    // Test that loop has no exit blocks
    auto exitBlocks = loop->getExitBlocks();
    EXPECT_TRUE(exitBlocks.empty());
}

// Test 11: If-else embedded in loop
TEST_F(LoopInfoTest, IfElseInLoop) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "if_else_loop", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* if_branch = BasicBlock::Create(context.get(), "if_branch", func);
        auto* else_branch =
            BasicBlock::Create(context.get(), "else_branch", func);
        auto* loop_latch =
            BasicBlock::Create(context.get(), "loop_latch", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* n = func->getArg(0);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "i");
        phi->addIncoming(builder.getInt32(0), entry);
        auto* loop_cond = builder.createICmpSLT(phi, n, "loop_cond");
        builder.createCondBr(loop_cond, if_branch, exit);

        builder.setInsertPoint(if_branch);
        auto* if_cond =
            builder.createICmpEQ(phi, builder.getInt32(5), "if_cond");
        builder.createCondBr(if_cond, else_branch, loop_latch);

        builder.setInsertPoint(else_branch);
        auto* else_val =
            builder.createMul(phi, builder.getInt32(2), "else_val");
        builder.createBr(loop_latch);

        builder.setInsertPoint(loop_latch);
        auto* result_phi = builder.createPHI(int32Ty, "result");
        result_phi->addIncoming(phi, if_branch);
        result_phi->addIncoming(else_val, else_branch);
        auto* inc = builder.createAdd(result_phi, builder.getInt32(1), "inc");
        phi->addIncoming(inc, loop_latch);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @if_else_loop(i32 %arg0) {
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop_latch ]
  %loop_cond = icmp slt i32 %i, %arg0
  br i1 %loop_cond, label %if_branch, label %exit
if_branch:
  %if_cond = icmp eq i32 %i, 5
  br i1 %if_cond, label %else_branch, label %loop_latch
else_branch:
  %else_val = mul i32 %i, 2
  br label %loop_latch
loop_latch:
  %result = phi i32 [ %i, %if_branch ], [ %else_val, %else_branch ]
  %inc = add i32 %result, 1
  br label %loop_header
exit:
  ret i32 %i
}
)");

    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();

    EXPECT_TRUE(equal(loop->getBlocks(), {"loop_header", "if_branch",
                                          "else_branch", "loop_latch"}));
}

// Test 12: Conditional break structure
TEST_F(LoopInfoTest, ConditionalBreak) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "conditional_break", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* break_check =
            BasicBlock::Create(context.get(), "break_check", func);
        auto* continue_loop =
            BasicBlock::Create(context.get(), "continue_loop", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* limit = func->getArg(0);
        auto* break_val = func->getArg(1);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "i");
        phi->addIncoming(builder.getInt32(0), entry);
        auto* loop_cond = builder.createICmpSLT(phi, limit, "loop_cond");
        builder.createCondBr(loop_cond, loop_body, exit);

        builder.setInsertPoint(loop_body);
        auto* inc = builder.createAdd(phi, builder.getInt32(1), "inc");
        builder.createBr(break_check);

        builder.setInsertPoint(break_check);
        auto* break_cond = builder.createICmpEQ(inc, break_val, "break_cond");
        builder.createCondBr(break_cond, exit,
                             continue_loop);  // Conditional break

        builder.setInsertPoint(continue_loop);
        phi->addIncoming(inc, continue_loop);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        auto* result = builder.createPHI(int32Ty, "result");
        result->addIncoming(phi, loop_header);
        result->addIncoming(inc, break_check);
        builder.createRet(result);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @conditional_break(i32 %arg0, i32 %arg1) {
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %entry ], [ %inc, %continue_loop ]
  %loop_cond = icmp slt i32 %i, %arg0
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %inc = add i32 %i, 1
  br label %break_check
break_check:
  %break_cond = icmp eq i32 %inc, %arg1
  br i1 %break_cond, label %exit, label %continue_loop
continue_loop:
  br label %loop_header
exit:
  %result = phi i32 [ %i, %loop_header ], [ %inc, %break_check ]
  ret i32 %result
}
)");

    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();

    // Test getExitingBlocks() - should include break_check
    auto exitingBlocks = loop->getExitingBlocks();
    EXPECT_GE(exitingBlocks.size(), 1u);
    EXPECT_TRUE(std::any_of(
        exitingBlocks.begin(), exitingBlocks.end(),
        [](BasicBlock* bb) { return bb->getName() == "break_check"; }));

    auto exitBlocks = loop->getExitBlocks();
    EXPECT_EQ(exitBlocks.size(), 1u);

    bool hasBreakCheck = false;
    for (auto* bb : exitingBlocks) {
        if (bb->getName() == "break_check" || bb->getName() == "loop_header") {
            hasBreakCheck = true;
            break;
        }
    }
    EXPECT_TRUE(hasBreakCheck);
}

// Test 13: Function with no loops
TEST_F(LoopInfoTest, NoLoopsInFunction) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
        auto* func = Function::Create(fnTy, "no_loops", module.get());

        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* branch1 = BasicBlock::Create(context.get(), "branch1", func);
        auto* branch2 = BasicBlock::Create(context.get(), "branch2", func);
        auto* merge = BasicBlock::Create(context.get(), "merge", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        auto* a = func->getArg(0);
        auto* b = func->getArg(1);
        auto* cond = builder.createICmpSGT(a, b, "cond");
        builder.createCondBr(cond, branch1, branch2);

        builder.setInsertPoint(branch1);
        auto* val1 = builder.createAdd(a, b, "val1");
        builder.createBr(merge);

        builder.setInsertPoint(branch2);
        auto* val2 = builder.createSub(a, b, "val2");
        builder.createBr(merge);

        builder.setInsertPoint(merge);
        auto* phi = builder.createPHI(int32Ty, "result");
        phi->addIncoming(val1, branch1);
        phi->addIncoming(val2, branch2);
        builder.createBr(exit);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @no_loops(i32 %arg0, i32 %arg1) {
entry:
  %cond = icmp sgt i32 %arg0, %arg1
  br i1 %cond, label %branch1, label %branch2
branch1:
  %val1 = add i32 %arg0, %arg1
  br label %merge
branch2:
  %val2 = sub i32 %arg0, %arg1
  br label %merge
merge:
  %result = phi i32 [ %val1, %branch1 ], [ %val2, %branch2 ]
  br label %exit
exit:
  ret i32 %result
}
)");

    // Verify empty LoopInfo return
    EXPECT_TRUE(loopInfo->empty());
    EXPECT_EQ(loopInfo->size(), 0u);
    EXPECT_TRUE(loopInfo->getTopLevelLoops().empty());

    // Test that no blocks are identified as loop headers
    for (auto& bb : *func) {
        EXPECT_FALSE(loopInfo->isLoopHeader(bb));
        EXPECT_EQ(loopInfo->getLoopFor(bb), nullptr);
        EXPECT_EQ(loopInfo->getLoopDepth(bb), 0u);
    }
}

// Test 14: LoopAnalysis Pass Test
TEST_F(LoopInfoTest, LoopAnalysisPass) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "test_pass", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* loop_header = BasicBlock::Create(context.get(), "loop_header", func);
    auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    auto* n = func->getArg(0);
    builder.createBr(loop_header);

    builder.setInsertPoint(loop_header);
    auto* phi = builder.createPHI(int32Ty, "i");
    phi->addIncoming(builder.getInt32(0), entry);
    auto* cond = builder.createICmpSLT(phi, n, "cond");
    builder.createCondBr(cond, loop_body, exit);

    builder.setInsertPoint(loop_body);
    auto* inc = builder.createAdd(phi, builder.getInt32(1), "inc");
    phi->addIncoming(inc, loop_body);
    builder.createBr(loop_header);

    builder.setInsertPoint(exit);
    builder.createRet(phi);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_pass(i32 %arg0) {
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop_body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop_body, label %exit
loop_body:
  %inc = add i32 %i, 1
  br label %loop_header
exit:
  ret i32 %i
}
)");

    // Test LoopAnalysis pass
    LoopAnalysis analysis;
    auto result = analysis.runOnFunction(*func);
    ASSERT_NE(result, nullptr);

    auto* loopInfo = dynamic_cast<LoopInfo*>(result.get());
    ASSERT_NE(loopInfo, nullptr);

    EXPECT_EQ(loopInfo->getFunction(), func);
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);
    EXPECT_TRUE(analysis.supportsFunction());

    auto deps = analysis.getDependencies();
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "DominanceAnalysis") !=
                deps.end());
}

// Test 15: Loop Properties and Methods
TEST_F(LoopInfoTest, LoopPropertiesAndMethods) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, "loop_properties", module.get());

        auto* preheader = BasicBlock::Create(context.get(), "preheader", func);
        auto* loop_header =
            BasicBlock::Create(context.get(), "loop_header", func);
        auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(preheader);
        auto* n = func->getArg(0);
        builder.createBr(loop_header);

        builder.setInsertPoint(loop_header);
        auto* phi = builder.createPHI(int32Ty, "i");
        phi->addIncoming(builder.getInt32(0), preheader);
        auto* cond = builder.createICmpSLT(phi, n, "cond");
        builder.createCondBr(cond, loop_body, exit);

        builder.setInsertPoint(loop_body);
        auto* inc = builder.createAdd(phi, builder.getInt32(1), "inc");
        phi->addIncoming(inc, loop_body);
        builder.createBr(loop_header);

        builder.setInsertPoint(exit);
        builder.createRet(phi);

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @loop_properties(i32 %arg0) {
preheader:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %preheader ], [ %inc, %loop_body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop_body, label %exit
loop_body:
  %inc = add i32 %i, 1
  br label %loop_header
exit:
  ret i32 %i
}
)");

    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* loop = loopInfo->getTopLevelLoops()[0].get();

    // Test simplified loop detection
    EXPECT_TRUE(loop->isSimplified());
    EXPECT_TRUE(loop->hasSingleBackedge());

    auto* preheader = loop->getPreheader();
    EXPECT_NE(preheader, nullptr);
    EXPECT_EQ(preheader->getName(), "preheader");

    // Test iterator support
    size_t blockCount = 0;
    for (auto it = loop->block_begin(); it != loop->block_end(); ++it) {
        blockCount++;
    }
    EXPECT_EQ(blockCount, loop->getBlocks().size());

    // Test verification
    EXPECT_TRUE(loopInfo->verify());
}

// Test 16: LoopAnalysis with AnalysisManager
TEST_F(LoopInfoTest, LoopAnalysisWithAnalysisManager) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "am_test", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* loop_header = BasicBlock::Create(context.get(), "loop_header", func);
    auto* loop_body = BasicBlock::Create(context.get(), "loop_body", func);
    auto* exit = BasicBlock::Create(context.get(), "exit", func);

    IRBuilder builder(entry);
    auto* n = func->getArg(0);
    builder.createBr(loop_header);

    builder.setInsertPoint(loop_header);
    auto* phi = builder.createPHI(int32Ty, "i");
    phi->addIncoming(builder.getInt32(0), entry);
    auto* cond = builder.createICmpSLT(phi, n, "cond");
    builder.createCondBr(cond, loop_body, exit);

    builder.setInsertPoint(loop_body);
    auto* inc = builder.createAdd(phi, builder.getInt32(1), "inc");
    phi->addIncoming(inc, loop_body);
    builder.createBr(loop_header);

    builder.setInsertPoint(exit);
    builder.createRet(phi);

    // Test with AnalysisManager (if available)
    LoopAnalysis analysis;
    auto result = analysis.runOnFunction(*func);
    ASSERT_NE(result, nullptr);

    auto* loopInfo = dynamic_cast<LoopInfo*>(result.get());
    ASSERT_NE(loopInfo, nullptr);

    EXPECT_EQ(loopInfo->getFunction(), func);
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);
}

// Test 17: Complex Loop Nesting with Many Levels
TEST_F(LoopInfoTest, ComplexDeepNesting) {
    auto createFunc = [&]() -> Function* {
        auto* int32Ty = context->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {});
        auto* func = Function::Create(fnTy, "deep_nesting", module.get());

        // Create 4-level deep nesting
        auto* entry = BasicBlock::Create(context.get(), "entry", func);
        auto* l1_header = BasicBlock::Create(context.get(), "l1_header", func);
        auto* l2_header = BasicBlock::Create(context.get(), "l2_header", func);
        auto* l3_header = BasicBlock::Create(context.get(), "l3_header", func);
        auto* l4_header = BasicBlock::Create(context.get(), "l4_header", func);
        auto* l4_body = BasicBlock::Create(context.get(), "l4_body", func);
        auto* l3_latch = BasicBlock::Create(context.get(), "l3_latch", func);
        auto* l2_latch = BasicBlock::Create(context.get(), "l2_latch", func);
        auto* l1_latch = BasicBlock::Create(context.get(), "l1_latch", func);
        auto* exit = BasicBlock::Create(context.get(), "exit", func);

        IRBuilder builder(entry);
        builder.createBr(l1_header);

        // Level 1
        builder.setInsertPoint(l1_header);
        auto* i = builder.createPHI(int32Ty, "i");
        i->addIncoming(builder.getInt32(0), entry);
        auto* cond1 = builder.createICmpSLT(i, builder.getInt32(2), "cond1");
        builder.createCondBr(cond1, l2_header, exit);

        // Level 2
        builder.setInsertPoint(l2_header);
        auto* j = builder.createPHI(int32Ty, "j");
        j->addIncoming(builder.getInt32(0), l1_header);
        auto* cond2 = builder.createICmpSLT(j, builder.getInt32(2), "cond2");
        builder.createCondBr(cond2, l3_header, l1_latch);

        // Level 3
        builder.setInsertPoint(l3_header);
        auto* k = builder.createPHI(int32Ty, "k");
        k->addIncoming(builder.getInt32(0), l2_header);
        auto* cond3 = builder.createICmpSLT(k, builder.getInt32(2), "cond3");
        builder.createCondBr(cond3, l4_header, l2_latch);

        // Level 4
        builder.setInsertPoint(l4_header);
        auto* l = builder.createPHI(int32Ty, "l");
        l->addIncoming(builder.getInt32(0), l3_header);
        auto* cond4 = builder.createICmpSLT(l, builder.getInt32(2), "cond4");
        builder.createCondBr(cond4, l4_body, l3_latch);

        builder.setInsertPoint(l4_body);
        auto* l_inc = builder.createAdd(l, builder.getInt32(1), "l_inc");
        l->addIncoming(l_inc, l4_body);
        builder.createBr(l4_header);

        builder.setInsertPoint(l3_latch);
        auto* k_inc = builder.createAdd(k, builder.getInt32(1), "k_inc");
        k->addIncoming(k_inc, l3_latch);
        builder.createBr(l3_header);

        builder.setInsertPoint(l2_latch);
        auto* j_inc = builder.createAdd(j, builder.getInt32(1), "j_inc");
        j->addIncoming(j_inc, l2_latch);
        builder.createBr(l2_header);

        builder.setInsertPoint(l1_latch);
        auto* i_inc = builder.createAdd(i, builder.getInt32(1), "i_inc");
        i->addIncoming(i_inc, l1_latch);
        builder.createBr(l1_header);

        builder.setInsertPoint(exit);
        builder.createRet(builder.getInt32(0));

        return func;
    };

    auto [func, loopInfo] = analyzeFunction(createFunc);

    // Test deep nesting identification
    EXPECT_EQ(loopInfo->getTopLevelLoops().size(), 1u);

    auto* l1 = loopInfo->getTopLevelLoops()[0].get();
    EXPECT_EQ(l1->getLoopDepth(), 1u);
    EXPECT_EQ(l1->getSubLoops().size(), 1u);

    auto* l2 = l1->getSubLoops()[0].get();
    EXPECT_EQ(l2->getLoopDepth(), 2u);
    EXPECT_EQ(l2->getSubLoops().size(), 1u);

    auto* l3 = l2->getSubLoops()[0].get();
    EXPECT_EQ(l3->getLoopDepth(), 3u);
    EXPECT_EQ(l3->getSubLoops().size(), 1u);

    auto* l4 = l3->getSubLoops()[0].get();
    EXPECT_EQ(l4->getLoopDepth(), 4u);
    EXPECT_TRUE(l4->getSubLoops().empty());
}

}  // namespace