#include <gtest/gtest.h>

#include "IR/Function.h"
#include "IR/Module.h"
#include "Pass/Pass.h"

using namespace midend;

// Test pass for registry testing
class TestPass : public ModulePass {
   public:
    TestPass() : ModulePass("TestPass", "A test pass for registry testing") {}

    bool runOnModule(Module&, AnalysisManager&) override {
        return false;  // No modification
    }
};

class AnotherTestPass : public ModulePass {
   public:
    AnotherTestPass() : ModulePass("AnotherTestPass", "Another test pass") {}

    bool runOnModule(Module&, AnalysisManager&) override {
        return true;  // Modification
    }
};

class PassRegistryTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Note: PassRegistry is a singleton, so we need to be careful with
        // state For testing, we'll work with a fresh registry state
    }

    void TearDown() override {
        // Clean up any registered passes if needed
    }
};

TEST_F(PassRegistryTest, SingletonAccess) {
    PassRegistry& registry1 = PassRegistry::getInstance();
    PassRegistry& registry2 = PassRegistry::getInstance();

    // Should be the same instance
    EXPECT_EQ(&registry1, &registry2);
}

TEST_F(PassRegistryTest, PassRegistration) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register a pass using template method
    registry.registerPass<TestPass>("test_pass");

    // Check if pass is registered
    EXPECT_TRUE(registry.hasPass("test_pass"));
    EXPECT_FALSE(registry.hasPass("nonexistent_pass"));
}

TEST_F(PassRegistryTest, PassCreation) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register a pass
    registry.registerPass<TestPass>("test_pass");

    // Create the pass
    auto pass = registry.createPass("test_pass");
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->getName(), "TestPass");

    // Try to create non-existent pass
    auto nullPass = registry.createPass("nonexistent");
    EXPECT_EQ(nullPass, nullptr);
}

TEST_F(PassRegistryTest, ManualPassRegistration) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register pass using function constructor
    registry.registerPass("manual_test",
                          []() { return std::make_unique<AnotherTestPass>(); });

    EXPECT_TRUE(registry.hasPass("manual_test"));

    auto pass = registry.createPass("manual_test");
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->getName(), "AnotherTestPass");
}

TEST_F(PassRegistryTest, MultiplePassRegistration) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register multiple passes
    registry.registerPass<TestPass>("pass1");
    registry.registerPass<AnotherTestPass>("pass2");

    EXPECT_TRUE(registry.hasPass("pass1"));
    EXPECT_TRUE(registry.hasPass("pass2"));

    auto pass1 = registry.createPass("pass1");
    auto pass2 = registry.createPass("pass2");

    EXPECT_NE(pass1, nullptr);
    EXPECT_NE(pass2, nullptr);
    EXPECT_EQ(pass1->getName(), "TestPass");
    EXPECT_EQ(pass2->getName(), "AnotherTestPass");
}

TEST_F(PassRegistryTest, GetRegisteredPasses) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Clear any existing passes by getting current state
    auto initialPasses = registry.getRegisteredPasses();
    size_t initialCount = initialPasses.size();

    // Register some passes
    registry.registerPass<TestPass>("list_test1");
    registry.registerPass<AnotherTestPass>("list_test2");

    auto passes = registry.getRegisteredPasses();
    EXPECT_EQ(passes.size(), initialCount + 2);

    // Check that our passes are in the list
    bool foundTest1 = false, foundTest2 = false;
    for (const auto& name : passes) {
        if (name == "list_test1") foundTest1 = true;
        if (name == "list_test2") foundTest2 = true;
    }

    EXPECT_TRUE(foundTest1);
    EXPECT_TRUE(foundTest2);
}

TEST_F(PassRegistryTest, PassReplacement) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register a pass
    registry.registerPass<TestPass>("replaceable");
    auto pass1 = registry.createPass("replaceable");
    EXPECT_EQ(pass1->getName(), "TestPass");

    // Register another pass with the same name (should replace)
    registry.registerPass<AnotherTestPass>("replaceable");
    auto pass2 = registry.createPass("replaceable");
    EXPECT_EQ(pass2->getName(), "AnotherTestPass");
}
