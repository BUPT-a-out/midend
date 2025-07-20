#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/CallGraph.h"

using namespace midend;

namespace {

// Helper class to easily build call graphs for testing
class CallGraphTestBuilder {
   private:
    std::unique_ptr<Context> context_;
    std::unique_ptr<Module> module_;
    std::vector<Function*> functions_;
    IRBuilder builder_;

   public:
    CallGraphTestBuilder()
        : context_(std::make_unique<Context>()),
          module_(std::make_unique<Module>("test_module", context_.get())),
          builder_(context_.get()) {}

    // Define N functions with simple signatures
    void defineFunctions(size_t count) {
        auto* int32Ty = context_->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});

        functions_.clear();
        for (size_t i = 0; i < count; ++i) {
            std::string name = "func" + std::to_string(i);
            auto* func = Function::Create(fnTy, name, module_.get());

            // Create a simple function body that returns the argument
            auto* bb = BasicBlock::Create(context_.get(), "entry", func);
            builder_.setInsertPoint(bb);
            builder_.createRet(func->getArg(0));

            functions_.push_back(func);
        }
    }

    // Add a call from functions_[from] to functions_[to]
    void addCall(size_t from, size_t to) {
        if (from >= functions_.size() || to >= functions_.size()) {
            throw std::out_of_range("Function index out of range");
        }

        Function* caller = functions_[from];
        Function* callee = functions_[to];

        // Find the return instruction and insert call before it
        BasicBlock* bb = &caller->front();

        // Find the return instruction (should be the last instruction)
        auto it = bb->end();
        if (it != bb->begin()) {
            --it;
            if ((*it)->getOpcode() == Opcode::Ret) {
                // Insert call before return
                builder_.setInsertPoint(bb, it);
                builder_.createCall(callee, {caller->getArg(0)});
            } else {
                // No return instruction, add call at the end
                builder_.setInsertPoint(bb);
                builder_.createCall(callee, {caller->getArg(0)});
            }
        } else {
            // Empty block, add call
            builder_.setInsertPoint(bb);
            builder_.createCall(callee, {caller->getArg(0)});
        }
    }

    // Add a global variable and make a function have side effects by storing to
    // it
    void addSideEffect(size_t funcIndex) {
        if (funcIndex >= functions_.size()) {
            throw std::out_of_range("Function index out of range");
        }

        Function* func = functions_[funcIndex];
        auto* int32Ty = context_->getInt32Type();

        // Create a global variable
        std::string globalName = "global_" + std::to_string(funcIndex);
        auto* globalVar = GlobalVariable::Create(
            int32Ty, false, GlobalVariable::ExternalLinkage, nullptr,
            globalName, module_.get());

        // Clear the function and rebuild it with side effects
        BasicBlock* bb = &func->front();
        while (!bb->empty()) {
            bb->back().eraseFromParent();
        }

        builder_.setInsertPoint(bb);
        builder_.createStore(func->getArg(0), globalVar);
        builder_.createRet(func->getArg(0));
    }

    // Create an external function declaration (no basic blocks = declaration)
    Function* addExternalFunction(const std::string& name) {
        auto* int32Ty = context_->getInt32Type();
        auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
        auto* func = Function::Create(fnTy, name, module_.get());
        // Don't add any basic blocks - this makes it a declaration
        return func;
    }

    // Add a call to an external function
    void addExternalCall(size_t from, Function* external) {
        if (from >= functions_.size()) {
            throw std::out_of_range("Function index out of range");
        }

        Function* caller = functions_[from];
        BasicBlock* bb = &caller->front();
        while (!bb->empty()) {
            bb->back().eraseFromParent();
        }

        builder_.setInsertPoint(bb);
        auto* callResult = builder_.createCall(external, {caller->getArg(0)});
        builder_.createRet(callResult);
    }

    Function* getFunction(size_t index) const {
        if (index >= functions_.size()) {
            throw std::out_of_range("Function index out of range");
        }
        return functions_[index];
    }

    Module* getModule() const { return module_.get(); }
    Context* getContext() const { return context_.get(); }
    const std::vector<Function*>& getFunctions() const { return functions_; }
};

class CallGraphTest : public ::testing::Test {
   protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test empty module
TEST_F(CallGraphTest, EmptyModule) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(0);

    CallGraph cg(builder.getModule());
    EXPECT_TRUE(cg.getSCCs().empty());
}

// Test single function with no calls
TEST_F(CallGraphTest, SingleFunction) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(1);

    CallGraph cg(builder.getModule());
    Function* f0 = builder.getFunction(0);

    EXPECT_FALSE(cg.isInSCC(f0));
    EXPECT_FALSE(cg.hasSideEffects(f0));

    auto iter = cg.getDownstreamIterator(f0);
    std::vector<Function*> downstream(iter.begin(), iter.end());
    EXPECT_EQ(downstream.size(), 0);  // No callees for single function
}

// Test simple call chain
TEST_F(CallGraphTest, SimpleCallChain) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(2);
    builder.addCall(0, 1);  // func0 calls func1

    CallGraph cg(builder.getModule());
    Function* f0 = builder.getFunction(0);
    Function* f1 = builder.getFunction(1);

    auto* n0 = cg.getNode(f0);
    auto* n1 = cg.getNode(f1);

    ASSERT_NE(n0, nullptr);
    ASSERT_NE(n1, nullptr);

    // Check edges
    EXPECT_TRUE(n0->getCallees().count(n1) > 0);
    EXPECT_TRUE(n1->getCallers().count(n0) > 0);

    // Check SCC - neither should be in an SCC
    EXPECT_FALSE(cg.isInSCC(f0));
    EXPECT_FALSE(cg.isInSCC(f1));

    // Check downstream iterator - should only return callees
    auto iter = cg.getDownstreamIterator(f0);
    std::vector<Function*> downstream(iter.begin(), iter.end());
    ASSERT_EQ(downstream.size(), 1);
    EXPECT_EQ(downstream[0], f1);  // Only the callee
}

// Test self-recursive function
TEST_F(CallGraphTest, SelfRecursiveFunction) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(1);
    builder.addCall(0, 0);  // func0 calls itself

    CallGraph cg(builder.getModule());
    Function* f0 = builder.getFunction(0);

    // Check that the function is in an SCC
    EXPECT_TRUE(cg.isInSCC(f0));

    // Check that the SCC contains only this function
    auto* scc = cg.getSCC(f0);
    ASSERT_NE(scc, nullptr);
    EXPECT_EQ(scc->size(), 1);
    EXPECT_TRUE(scc->count(f0) > 0);
}

// Test mutual recursion
TEST_F(CallGraphTest, MutualRecursion) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(2);
    builder.addCall(0, 1);  // func0 calls func1
    builder.addCall(1, 0);  // func1 calls func0

    CallGraph cg(builder.getModule());
    Function* f0 = builder.getFunction(0);
    Function* f1 = builder.getFunction(1);

    // Both functions should be in the same SCC
    EXPECT_TRUE(cg.isInSCC(f0));
    EXPECT_TRUE(cg.isInSCC(f1));

    auto* scc0 = cg.getSCC(f0);
    auto* scc1 = cg.getSCC(f1);
    EXPECT_EQ(scc0, scc1);

    ASSERT_NE(scc0, nullptr);
    EXPECT_EQ(scc0->size(), 2);
    EXPECT_TRUE(scc0->count(f0) > 0);
    EXPECT_TRUE(scc0->count(f1) > 0);
}

// Test disconnected components
TEST_F(CallGraphTest, DisconnectedComponents) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(4);
    builder.addCall(0, 1);  // Component 1: func0 -> func1
    builder.addCall(2, 3);  // Component 2: func2 -> func3

    CallGraph cg(builder.getModule());

    // Check that each component is separate
    auto iter0 = cg.getDownstreamIterator(builder.getFunction(0));
    std::vector<Function*> downstream0(iter0.begin(), iter0.end());
    EXPECT_EQ(downstream0.size(), 1);  // func0 -> func1

    auto iter2 = cg.getDownstreamIterator(builder.getFunction(2));
    std::vector<Function*> downstream2(iter2.begin(), iter2.end());
    EXPECT_EQ(downstream2.size(), 1);  // func2 -> func3
}

// Test large SCC (5 functions in a cycle)
TEST_F(CallGraphTest, LargeSCC) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(5);
    // Create a cycle: 0 -> 1 -> 2 -> 3 -> 4 -> 0
    builder.addCall(0, 1);
    builder.addCall(1, 2);
    builder.addCall(2, 3);
    builder.addCall(3, 4);
    builder.addCall(4, 0);

    CallGraph cg(builder.getModule());

    // All functions should be in the same SCC
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }

    // Check they're all in the same SCC
    auto* scc = cg.getSCC(builder.getFunction(0));
    ASSERT_NE(scc, nullptr);
    EXPECT_EQ(scc->size(), 5);

    for (size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(scc->count(builder.getFunction(i)) > 0);
        EXPECT_EQ(cg.getSCC(builder.getFunction(i)), scc);
    }
}

// Test multiple SCCs in one graph
TEST_F(CallGraphTest, MultipleSCCs) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(6);

    // First SCC: 0 <-> 1
    builder.addCall(0, 1);
    builder.addCall(1, 0);

    // Second SCC: 2 <-> 3
    builder.addCall(2, 3);
    builder.addCall(3, 2);

    // Connect SCCs: 1 -> 2
    builder.addCall(1, 2);

    // Isolated functions: 4, 5

    CallGraph cg(builder.getModule());

    // Check first SCC
    auto* scc1 = cg.getSCC(builder.getFunction(0));
    EXPECT_EQ(scc1->size(), 2);
    EXPECT_TRUE(scc1->count(builder.getFunction(0)) > 0);
    EXPECT_TRUE(scc1->count(builder.getFunction(1)) > 0);

    // Check second SCC
    auto* scc2 = cg.getSCC(builder.getFunction(2));
    EXPECT_EQ(scc2->size(), 2);
    EXPECT_TRUE(scc2->count(builder.getFunction(2)) > 0);
    EXPECT_TRUE(scc2->count(builder.getFunction(3)) > 0);

    // SCCs should be different
    EXPECT_NE(scc1, scc2);

    // Isolated functions should not be in SCCs
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(4)));
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(5)));
}

// Test diamond pattern with SCC
TEST_F(CallGraphTest, DiamondWithSCC) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(4);

    // Diamond: 0 -> 1, 0 -> 2, 1 -> 3, 2 -> 3
    builder.addCall(0, 1);
    builder.addCall(0, 2);
    builder.addCall(1, 3);
    builder.addCall(2, 3);

    // Add recursion to make 1 and 2 part of an SCC
    builder.addCall(1, 2);
    builder.addCall(2, 1);

    CallGraph cg(builder.getModule());

    // Functions 1 and 2 should be in the same SCC
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(1)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(2)));
    EXPECT_EQ(cg.getSCC(builder.getFunction(1)),
              cg.getSCC(builder.getFunction(2)));

    // Functions 0 and 3 should not be in SCCs
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(0)));
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(3)));
}

// Test external function calls
TEST_F(CallGraphTest, ExternalFunctionCalls) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(2);
    Function* external = builder.addExternalFunction("external_func");

    builder.addCall(0, 1);
    builder.addExternalCall(1, external);

    CallGraph cg(builder.getModule());

    // External function should have side effects
    EXPECT_TRUE(cg.hasSideEffects(external));

    // Function calling external function should have side effects
    EXPECT_TRUE(cg.hasSideEffects(builder.getFunction(1)));

    // Function calling function with side effects should also have side effects
    EXPECT_TRUE(cg.hasSideEffects(builder.getFunction(0)));
}

// Test side effect propagation
TEST_F(CallGraphTest, SideEffectPropagation) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(4);

    // Chain: 0 -> 1 -> 2 -> 3
    builder.addCall(0, 1);
    builder.addCall(1, 2);
    builder.addCall(2, 3);

    // Only func3 has direct side effects
    builder.addSideEffect(3);

    CallGraph cg(builder.getModule());

    // All functions should have side effects due to propagation
    EXPECT_TRUE(cg.hasSideEffects(builder.getFunction(0)));
    EXPECT_TRUE(cg.hasSideEffects(builder.getFunction(1)));
    EXPECT_TRUE(cg.hasSideEffects(builder.getFunction(2)));
    EXPECT_TRUE(cg.hasSideEffects(builder.getFunction(3)));
}

// Test pure functions in SCC
TEST_F(CallGraphTest, PureSCC) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(3);

    // Create a cycle of pure functions: 0 -> 1 -> 2 -> 0
    builder.addCall(0, 1);
    builder.addCall(1, 2);
    builder.addCall(2, 0);

    CallGraph cg(builder.getModule());

    // All functions should be in the same SCC
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }

    // But none should have side effects
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_FALSE(cg.hasSideEffects(builder.getFunction(i)));
    }
}

// Test indirect function calls (function pointers)
TEST_F(CallGraphTest, IndirectCalls) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(2);

    Function* caller = builder.getFunction(0);
    Function* target = builder.getFunction(1);

    // Create an indirect call by using a null function pointer
    // (this simulates what happens when we can't statically determine the
    // target)
    BasicBlock* bb = &caller->front();
    while (!bb->empty()) {
        bb->back().eraseFromParent();
    }

    IRBuilder localBuilder(bb);
    // Create a call with null callee to simulate indirect call
    auto* int32Ty = builder.getContext()->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* indirectCall =
        CallInst::Create(fnTy, nullptr, {caller->getArg(0)}, "indirect");
    bb->push_back(indirectCall);
    localBuilder.setInsertPoint(bb);
    localBuilder.createRet(caller->getArg(0));

    CallGraph cg(builder.getModule());

    // Indirect calls should be considered to have side effects
    EXPECT_TRUE(cg.hasSideEffects(caller));

    // The call graph should not have an edge to the target function
    auto* callerNode = cg.getNode(caller);
    auto* targetNode = cg.getNode(target);
    EXPECT_TRUE(callerNode->getCallees().count(targetNode) == 0);
}

// Stress test with many functions
TEST_F(CallGraphTest, StressTest) {
    CallGraphTestBuilder builder;
    const size_t numFuncs = 100;
    builder.defineFunctions(numFuncs);

    // Create a linear chain: 0 -> 1 -> 2 -> ... -> 99
    for (size_t i = 0; i < numFuncs - 1; ++i) {
        builder.addCall(i, i + 1);
    }

    CallGraph cg(builder.getModule());

    // No function should be in an SCC
    for (size_t i = 0; i < numFuncs; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }

    // All downstream functions should be reachable from func0
    auto iter = cg.getDownstreamIterator(builder.getFunction(0));
    std::vector<Function*> downstream;
    for (Function* F : iter) {
        downstream.push_back(F);
    }
    EXPECT_EQ(downstream.size(), numFuncs - 1);  // All except func0 itself
}

// Test complex nested SCCs with 15 nodes
TEST_F(CallGraphTest, ComplexNestedSCCs) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(15);

    // Create three nested SCCs:
    // SCC1: 0 <-> 1 <-> 2 (3 nodes)
    builder.addCall(0, 1);
    builder.addCall(1, 2);
    builder.addCall(2, 0);

    // SCC2: 3 <-> 4 <-> 5 <-> 6 (4 nodes)
    builder.addCall(3, 4);
    builder.addCall(4, 5);
    builder.addCall(5, 6);
    builder.addCall(6, 3);

    // SCC3: 7 <-> 8 <-> 9 <-> 10 <-> 11 (5 nodes)
    builder.addCall(7, 8);
    builder.addCall(8, 9);
    builder.addCall(9, 10);
    builder.addCall(10, 11);
    builder.addCall(11, 7);

    // Connect SCCs in a chain: SCC1 -> SCC2 -> SCC3
    builder.addCall(2, 3);
    builder.addCall(6, 7);

    // Add isolated nodes that connect to SCCs
    builder.addCall(12, 0);  // 12 -> SCC1
    builder.addCall(13, 3);  // 13 -> SCC2
    builder.addCall(14, 7);  // 14 -> SCC3

    CallGraph cg(builder.getModule());

    // Verify SCC1 (nodes 0, 1, 2)
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* scc1 = cg.getSCC(builder.getFunction(0));
    EXPECT_EQ(scc1->size(), 3);

    // Verify SCC2 (nodes 3, 4, 5, 6)
    for (int i = 3; i < 7; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* scc2 = cg.getSCC(builder.getFunction(3));
    EXPECT_EQ(scc2->size(), 4);

    // Verify SCC3 (nodes 7, 8, 9, 10, 11)
    for (int i = 7; i < 12; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* scc3 = cg.getSCC(builder.getFunction(7));
    EXPECT_EQ(scc3->size(), 5);

    // Verify isolated nodes are not in SCCs
    for (int i = 12; i < 15; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }

    // Verify all SCCs are different
    EXPECT_NE(scc1, scc2);
    EXPECT_NE(scc2, scc3);
    EXPECT_NE(scc1, scc3);
}

// Test complex SCC with multiple entry/exit points (12 nodes)
TEST_F(CallGraphTest, ComplexSCCWithMultipleEntryExit) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(12);

    // Create a complex SCC with 8 nodes (0-7)
    // Core cycle: 0 -> 1 -> 2 -> 3 -> 0
    builder.addCall(0, 1);
    builder.addCall(1, 2);
    builder.addCall(2, 3);
    builder.addCall(3, 0);

    // Additional nodes in SCC with multiple paths
    builder.addCall(1, 4);
    builder.addCall(4, 5);
    builder.addCall(5, 2);  // Back to core cycle

    builder.addCall(3, 6);
    builder.addCall(6, 7);
    builder.addCall(7, 0);  // Back to core cycle

    // Cross connections within SCC
    builder.addCall(4, 6);
    builder.addCall(5, 7);

    // External entry points
    builder.addCall(8, 0);   // Entry to node 0
    builder.addCall(9, 1);   // Entry to node 1
    builder.addCall(10, 4);  // Entry to node 4

    // External exit points
    builder.addCall(2, 11);  // Exit from node 2
    builder.addCall(5, 11);  // Exit from node 5
    builder.addCall(7, 11);  // Exit from node 7

    CallGraph cg(builder.getModule());

    // Verify the main SCC contains exactly nodes 0-7
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* mainSCC = cg.getSCC(builder.getFunction(0));
    EXPECT_EQ(mainSCC->size(), 8);

    // Verify all nodes 0-7 are in the same SCC
    for (int i = 1; i < 8; ++i) {
        EXPECT_EQ(cg.getSCC(builder.getFunction(i)), mainSCC);
    }

    // Verify external nodes are not in the SCC
    for (int i = 8; i < 12; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }
}

// Test interleaved SCCs with complex connections (20 nodes)
TEST_F(CallGraphTest, InterleavedSCCs) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(20);

    // Create 4 separate SCCs with various sizes

    // SCC1: Simple cycle (0, 1)
    builder.addCall(0, 1);
    builder.addCall(1, 0);

    // SCC2: Triangle (2, 3, 4)
    builder.addCall(2, 3);
    builder.addCall(3, 4);
    builder.addCall(4, 2);

    // SCC3: Complex cycle (5, 6, 7, 8, 9)
    builder.addCall(5, 6);
    builder.addCall(6, 7);
    builder.addCall(7, 8);
    builder.addCall(8, 9);
    builder.addCall(9, 5);
    // Add internal connections
    builder.addCall(5, 8);
    builder.addCall(7, 9);

    // SCC4: Large cycle (10-15)
    for (int i = 10; i < 15; ++i) {
        builder.addCall(i, i + 1);
    }
    builder.addCall(15, 10);

    // Connect SCCs in acyclic ways (DAG between SCCs)
    builder.addCall(1, 2);   // SCC1 -> SCC2
    builder.addCall(4, 5);   // SCC2 -> SCC3
    builder.addCall(9, 10);  // SCC3 -> SCC4
    // Note: No cycle back from SCC4 to SCC1

    // Add bridge nodes that connect to SCCs but don't create cycles
    builder.addCall(16, 0);  // Bridge to SCC1
    builder.addCall(16, 2);  // Bridge to SCC2
    builder.addCall(16, 5);  // Bridge to SCC3

    builder.addCall(17, 10);  // Bridge to SCC4
    builder.addCall(4, 17);   // SCC2 to bridge (one-way)
    builder.addCall(17, 18);  // Bridge to isolated

    builder.addCall(18, 19);  // Isolated chain

    CallGraph cg(builder.getModule());

    // Verify SCC1 (nodes 0, 1)
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(0)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(1)));
    auto* scc1 = cg.getSCC(builder.getFunction(0));
    EXPECT_EQ(scc1->size(), 2);

    // Verify SCC2 (nodes 2, 3, 4)
    for (int i = 2; i <= 4; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* scc2 = cg.getSCC(builder.getFunction(2));
    EXPECT_EQ(scc2->size(), 3);

    // Verify SCC3 (nodes 5, 6, 7, 8, 9)
    for (int i = 5; i <= 9; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* scc3 = cg.getSCC(builder.getFunction(5));
    EXPECT_EQ(scc3->size(), 5);

    // Verify SCC4 (nodes 10-15)
    for (int i = 10; i <= 15; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    auto* scc4 = cg.getSCC(builder.getFunction(10));
    EXPECT_EQ(scc4->size(), 6);

    // Verify all SCCs are different
    EXPECT_NE(scc1, scc2);
    EXPECT_NE(scc2, scc3);
    EXPECT_NE(scc3, scc4);
    EXPECT_NE(scc1, scc3);
    EXPECT_NE(scc1, scc4);
    EXPECT_NE(scc2, scc4);

    // Verify bridge and isolated nodes are not in SCCs
    for (int i = 16; i < 20; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }
}

// Test complete graph (all nodes connected to all nodes) - 10 nodes
TEST_F(CallGraphTest, CompleteGraph) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(10);

    // Create a complete directed graph - every node calls every other node
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            if (i != j) {
                builder.addCall(i, j);
            }
        }
    }

    CallGraph cg(builder.getModule());

    // All nodes should be in the same SCC
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }

    // Verify it's a single SCC with all 10 nodes
    auto* scc = cg.getSCC(builder.getFunction(0));
    EXPECT_EQ(scc->size(), 10);

    // Verify all nodes are in the same SCC
    for (int i = 1; i < 10; ++i) {
        EXPECT_EQ(cg.getSCC(builder.getFunction(i)), scc);
    }
}

// Test tournament DAG (directed acyclic graph) - 10 nodes
TEST_F(CallGraphTest, TournamentDAG) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(10);

    // Create a tournament DAG where i calls j if i < j
    for (int i = 0; i < 10; ++i) {
        for (int j = i + 1; j < 10; ++j) {
            builder.addCall(i, j);
        }
    }

    CallGraph cg(builder.getModule());

    // No node should be in an SCC (it's acyclic)
    for (int i = 0; i < 10; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }

    // Verify downstream iteration from node 0 reaches all other nodes
    auto iter = cg.getDownstreamIterator(builder.getFunction(0));
    std::unordered_set<Function*> reachable;
    for (Function* f : iter) {
        reachable.insert(f);
    }
    EXPECT_EQ(reachable.size(), 9);  // All nodes except 0

    // Verify node 9 has no downstream nodes
    auto iter9 = cg.getDownstreamIterator(builder.getFunction(9));
    std::vector<Function*> downstream9(iter9.begin(), iter9.end());
    EXPECT_EQ(downstream9.size(), 0);
}

// Test graph with mix of large and small components (25 nodes)
TEST_F(CallGraphTest, MixedLargeAndSmallComponents) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(25);

    // Large SCC (nodes 0-9): bidirectional ring with cross connections
    for (int i = 0; i < 9; ++i) {
        builder.addCall(i, i + 1);
        builder.addCall(i + 1, i);
    }
    builder.addCall(9, 0);
    builder.addCall(0, 9);
    // Add some cross connections
    builder.addCall(0, 5);
    builder.addCall(3, 7);
    builder.addCall(2, 8);

    // Small SCC1 (nodes 10-11): simple mutual recursion
    builder.addCall(10, 11);
    builder.addCall(11, 10);

    // Small SCC2 (nodes 12-13): another mutual recursion
    builder.addCall(12, 13);
    builder.addCall(13, 12);

    // Self-recursive nodes
    builder.addCall(14, 14);
    builder.addCall(15, 15);

    // Chain of non-SCC nodes
    for (int i = 16; i < 20; ++i) {
        builder.addCall(i, i + 1);
    }

    // Nodes that connect components
    builder.addCall(20, 0);   // To large SCC
    builder.addCall(20, 10);  // To small SCC1
    builder.addCall(20, 12);  // To small SCC2
    builder.addCall(21, 14);  // To self-recursive
    builder.addCall(21, 16);  // To chain

    // Isolated nodes (22, 23, 24)

    CallGraph cg(builder.getModule());

    // Verify large SCC
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(cg.isInSCC(builder.getFunction(i)));
    }
    EXPECT_EQ(cg.getSCC(builder.getFunction(0))->size(), 10);

    // Verify small SCCs
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(10)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(11)));
    EXPECT_EQ(cg.getSCC(builder.getFunction(10))->size(), 2);

    EXPECT_TRUE(cg.isInSCC(builder.getFunction(12)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(13)));
    EXPECT_EQ(cg.getSCC(builder.getFunction(12))->size(), 2);

    // Verify self-recursive nodes
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(14)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(15)));
    EXPECT_EQ(cg.getSCC(builder.getFunction(14))->size(), 1);
    EXPECT_EQ(cg.getSCC(builder.getFunction(15))->size(), 1);

    // Verify chain nodes are not in SCCs
    for (int i = 16; i < 20; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }

    // Verify connector and isolated nodes
    for (int i = 20; i < 25; ++i) {
        EXPECT_FALSE(cg.isInSCC(builder.getFunction(i)));
    }
}

// Test tree structure with back edges creating SCCs (15 nodes)
TEST_F(CallGraphTest, TreeWithBackEdges) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(15);

    // Build a tree structure
    // Root: 0
    // Level 1: 1, 2, 3
    builder.addCall(0, 1);
    builder.addCall(0, 2);
    builder.addCall(0, 3);

    // Level 2: 4, 5 (under 1), 6, 7 (under 2), 8 (under 3)
    builder.addCall(1, 4);
    builder.addCall(1, 5);
    builder.addCall(2, 6);
    builder.addCall(2, 7);
    builder.addCall(3, 8);

    // Level 3: 9, 10 (under 4), 11 (under 6), 12, 13, 14 (under 8)
    builder.addCall(4, 9);
    builder.addCall(4, 10);
    builder.addCall(6, 11);
    builder.addCall(8, 12);
    builder.addCall(8, 13);
    builder.addCall(8, 14);

    // Add back edges to create SCCs
    builder.addCall(5, 1);   // Creates SCC: 1, 5
    builder.addCall(10, 4);  // Creates SCC: 4, 10
    builder.addCall(11, 2);  // Creates larger SCC: 2, 6, 11
    builder.addCall(13, 3);  // Creates larger SCC: 3, 8, 13
    builder.addCall(14, 0);  // Connects to root, creating large SCC

    CallGraph cg(builder.getModule());

    // Due to the back edge from 14 to 0, and the path 0->3->8->14,
    // nodes 0, 3, 8, 13, 14 should form an SCC
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(0)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(3)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(8)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(13)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(14)));

    // Verify SCC with nodes 1, 5
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(1)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(5)));
    auto* scc15 = cg.getSCC(builder.getFunction(1));
    EXPECT_EQ(scc15->size(), 2);

    // Verify SCC with nodes 2, 6, 11
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(2)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(6)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(11)));
    auto* scc2611 = cg.getSCC(builder.getFunction(2));
    EXPECT_EQ(scc2611->size(), 3);

    // Verify SCC with nodes 4, 10
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(4)));
    EXPECT_TRUE(cg.isInSCC(builder.getFunction(10)));
    auto* scc410 = cg.getSCC(builder.getFunction(4));
    EXPECT_EQ(scc410->size(), 2);

    // Nodes without back edges should not be in SCCs
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(7)));
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(9)));
    EXPECT_FALSE(cg.isInSCC(builder.getFunction(12)));
}

// Test SuperGraph functionality
TEST_F(CallGraphTest, SuperGraphBasic) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(4);

    // Create two SCCs: 0 <-> 1 and 2 <-> 3
    builder.addCall(0, 1);
    builder.addCall(1, 0);
    builder.addCall(2, 3);
    builder.addCall(3, 2);

    // Connect SCCs: SCC1 -> SCC2
    builder.addCall(1, 2);

    CallGraph cg(builder.getModule());
    const SuperGraph& sg = cg.getSuperGraph();

    // Should have 2 super nodes
    EXPECT_EQ(sg.size(), 2);

    // Get super nodes
    SuperNode* sn0 = cg.getSuperNode(builder.getFunction(0));
    SuperNode* sn1 = cg.getSuperNode(builder.getFunction(1));
    SuperNode* sn2 = cg.getSuperNode(builder.getFunction(2));
    SuperNode* sn3 = cg.getSuperNode(builder.getFunction(3));

    // Functions 0 and 1 should be in the same super node
    EXPECT_EQ(sn0, sn1);
    EXPECT_EQ(sn2, sn3);
    EXPECT_NE(sn0, sn2);

    // Check super node contents
    EXPECT_EQ(sn0->size(), 2);
    EXPECT_EQ(sn2->size(), 2);
    EXPECT_TRUE(sn0->containsFunction(builder.getFunction(0)));
    EXPECT_TRUE(sn0->containsFunction(builder.getFunction(1)));
    EXPECT_TRUE(sn2->containsFunction(builder.getFunction(2)));
    EXPECT_TRUE(sn2->containsFunction(builder.getFunction(3)));

    // Check SCC property
    EXPECT_TRUE(sn0->isSCC());
    EXPECT_TRUE(sn2->isSCC());

    // Check connectivity
    EXPECT_TRUE(sn0->getSuccessors().count(sn2) > 0);
    EXPECT_TRUE(sn2->getPredecessors().count(sn0) > 0);
    EXPECT_EQ(sn0->getSuccessors().size(), 1);
    EXPECT_EQ(sn2->getPredecessors().size(), 1);
    EXPECT_EQ(sn0->getPredecessors().size(), 0);
    EXPECT_EQ(sn2->getSuccessors().size(), 0);
}

// Test SuperGraph with self-recursive function
TEST_F(CallGraphTest, SuperGraphSelfRecursive) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(3);

    // Function 0 is self-recursive
    builder.addCall(0, 0);

    // Functions 1 and 2 are connected but not recursive
    builder.addCall(1, 2);

    CallGraph cg(builder.getModule());
    const SuperGraph& sg = cg.getSuperGraph();

    // Should have 3 super nodes (self-recursive counts as SCC)
    EXPECT_EQ(sg.size(), 3);

    SuperNode* sn0 = cg.getSuperNode(builder.getFunction(0));
    SuperNode* sn1 = cg.getSuperNode(builder.getFunction(1));
    SuperNode* sn2 = cg.getSuperNode(builder.getFunction(2));

    // All should be in different super nodes
    EXPECT_NE(sn0, sn1);
    EXPECT_NE(sn1, sn2);
    EXPECT_NE(sn0, sn2);

    // Check sizes
    EXPECT_EQ(sn0->size(), 1);
    EXPECT_EQ(sn1->size(), 1);
    EXPECT_EQ(sn2->size(), 1);

    // Check SCC property
    EXPECT_TRUE(sn0->isSCC());   // Self-recursive
    EXPECT_FALSE(sn1->isSCC());  // Not recursive
    EXPECT_FALSE(sn2->isSCC());  // Not recursive

    // Check trivial property
    EXPECT_TRUE(sn0->isTrivial());
    EXPECT_TRUE(sn1->isTrivial());
    EXPECT_TRUE(sn2->isTrivial());
}

// Test SuperGraph reverse topological ordering
TEST_F(CallGraphTest, SuperGraphReverseTopologicalOrder) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(6);

    // Create SCCs: {0,1}, {2,3}, {4,5}
    builder.addCall(0, 1);
    builder.addCall(1, 0);
    builder.addCall(2, 3);
    builder.addCall(3, 2);
    builder.addCall(4, 5);
    builder.addCall(5, 4);

    // Connect in order: {0,1} -> {2,3} -> {4,5}
    builder.addCall(1, 2);
    builder.addCall(3, 4);

    CallGraph cg(builder.getModule());
    const SuperGraph& sg = cg.getSuperGraph();

    EXPECT_EQ(sg.size(), 3);

    SuperNode* sn01 = cg.getSuperNode(builder.getFunction(0));
    SuperNode* sn23 = cg.getSuperNode(builder.getFunction(2));
    SuperNode* sn45 = cg.getSuperNode(builder.getFunction(4));

    // Test iteration order (reverse topological)
    std::vector<SuperNode*> iterationOrder(sg.begin(), sg.end());
    EXPECT_EQ(iterationOrder.size(), 3);

    // In reverse topological order: leaves first (sn45), then sn23, then roots
    // (sn01)
    EXPECT_EQ(iterationOrder[0], sn45);  // Leaf (no successors)
    EXPECT_EQ(iterationOrder[1], sn23);  // Middle
    EXPECT_EQ(iterationOrder[2], sn01);  // Root (no predecessors)
}

// Test SuperGraph iterator
TEST_F(CallGraphTest, SuperGraphIterator) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(4);

    // Create two SCCs with connection
    builder.addCall(0, 1);
    builder.addCall(1, 0);
    builder.addCall(2, 3);
    builder.addCall(3, 2);
    builder.addCall(1, 2);

    CallGraph cg(builder.getModule());
    const SuperGraph& sg = cg.getSuperGraph();

    // Test basic iteration (should be in reverse topological order - bottom-up)
    std::vector<SuperNode*> nodes(sg.begin(), sg.end());
    EXPECT_EQ(nodes.size(), 2);

    // In reverse topological order, the sink (no successors) should come first
    EXPECT_TRUE(nodes[0]->getSuccessors().empty());   // Should be the sink
    EXPECT_FALSE(nodes[1]->getSuccessors().empty());  // Should be the source

    // The iteration order is reverse topological (bottom-up)
    // So first node should have no successors (leaf)
    // and last node should have no predecessors (root)
}

// Test SuperGraph complex case
TEST_F(CallGraphTest, SuperGraphComplex) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(10);

    // Create complex structure:
    // SCC1: {0, 1, 2} - 3-node cycle
    builder.addCall(0, 1);
    builder.addCall(1, 2);
    builder.addCall(2, 0);

    // SCC2: {3} - self-recursive
    builder.addCall(3, 3);

    // SCC3: {4, 5} - mutual recursion
    builder.addCall(4, 5);
    builder.addCall(5, 4);

    // Non-SCC nodes: 6, 7, 8, 9 in a chain
    builder.addCall(6, 7);
    builder.addCall(7, 8);
    builder.addCall(8, 9);

    // Connect: SCC1 -> SCC2 -> SCC3 -> chain
    builder.addCall(2, 3);
    builder.addCall(3, 4);
    builder.addCall(5, 6);

    CallGraph cg(builder.getModule());
    const SuperGraph& sg = cg.getSuperGraph();

    // Should have 7 super nodes: 3 SCCs + 4 individual nodes
    EXPECT_EQ(sg.size(), 7);

    SuperNode* sn012 = cg.getSuperNode(builder.getFunction(0));
    SuperNode* sn3 = cg.getSuperNode(builder.getFunction(3));
    SuperNode* sn45 = cg.getSuperNode(builder.getFunction(4));
    SuperNode* sn6 = cg.getSuperNode(builder.getFunction(6));
    SuperNode* sn7 = cg.getSuperNode(builder.getFunction(7));
    SuperNode* sn8 = cg.getSuperNode(builder.getFunction(8));
    SuperNode* sn9 = cg.getSuperNode(builder.getFunction(9));

    // Check SCC properties
    EXPECT_TRUE(sn012->isSCC());  // 3-node SCC
    EXPECT_TRUE(sn3->isSCC());    // Self-recursive
    EXPECT_TRUE(sn45->isSCC());   // 2-node SCC
    EXPECT_FALSE(sn6->isSCC());   // Not recursive
    EXPECT_FALSE(sn7->isSCC());   // Not recursive
    EXPECT_FALSE(sn8->isSCC());   // Not recursive
    EXPECT_FALSE(sn9->isSCC());   // Not recursive

    // Check sizes
    EXPECT_EQ(sn012->size(), 3);
    EXPECT_EQ(sn3->size(), 1);
    EXPECT_EQ(sn45->size(), 2);
    EXPECT_EQ(sn6->size(), 1);
    EXPECT_EQ(sn7->size(), 1);
    EXPECT_EQ(sn8->size(), 1);
    EXPECT_EQ(sn9->size(), 1);

    // Check reverse topological order makes sense (leaves first)
    std::vector<SuperNode*> iterationOrder(sg.begin(), sg.end());
    EXPECT_EQ(iterationOrder.size(), 7);

    // In reverse topological order, sn9 should come before sn6, etc. (leaves
    // first)
    auto pos012 =
        std::find(iterationOrder.begin(), iterationOrder.end(), sn012);
    auto pos3 = std::find(iterationOrder.begin(), iterationOrder.end(), sn3);
    auto pos45 = std::find(iterationOrder.begin(), iterationOrder.end(), sn45);
    auto pos6 = std::find(iterationOrder.begin(), iterationOrder.end(), sn6);
    auto pos9 = std::find(iterationOrder.begin(), iterationOrder.end(), sn9);

    EXPECT_GT(pos012, pos3);  // Root comes after its dependencies
    EXPECT_GT(pos3, pos45);   // Dependencies come before dependents
    EXPECT_GT(pos45, pos6);
    EXPECT_GT(pos6, pos9);  // Leaves come first
}

// Test reverse topological order iteration
TEST_F(CallGraphTest, SuperGraphReverseTopologicalIteration) {
    CallGraphTestBuilder builder;
    builder.defineFunctions(6);

    // Create a chain: SCC1 -> SCC2 -> SCC3
    // SCC1: {0,1}
    builder.addCall(0, 1);
    builder.addCall(1, 0);

    // SCC2: {2,3}
    builder.addCall(2, 3);
    builder.addCall(3, 2);

    // SCC3: {4,5}
    builder.addCall(4, 5);
    builder.addCall(5, 4);

    // Connect: SCC1 -> SCC2 -> SCC3
    builder.addCall(1, 2);
    builder.addCall(3, 4);

    CallGraph cg(builder.getModule());
    const SuperGraph& sg = cg.getSuperGraph();

    SuperNode* sn01 = cg.getSuperNode(builder.getFunction(0));
    SuperNode* sn23 = cg.getSuperNode(builder.getFunction(2));
    SuperNode* sn45 = cg.getSuperNode(builder.getFunction(4));

    // Test forward iteration (should be reverse topological = bottom-up)
    std::vector<SuperNode*> forwardOrder(sg.begin(), sg.end());
    EXPECT_EQ(forwardOrder.size(), 3);

    // In reverse topological order: leaves first (sn45), then sn23, then roots
    // (sn01)
    EXPECT_EQ(forwardOrder[0], sn45);  // Leaf (no successors)
    EXPECT_EQ(forwardOrder[1], sn23);  // Middle
    EXPECT_EQ(forwardOrder[2], sn01);  // Root (no predecessors)

    // Verify the properties
    EXPECT_TRUE(sn45->getSuccessors().empty());     // Leaf
    EXPECT_TRUE(sn01->getPredecessors().empty());   // Root
    EXPECT_FALSE(sn23->getSuccessors().empty());    // Has successors
    EXPECT_FALSE(sn23->getPredecessors().empty());  // Has predecessors
}

}  // namespace