#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "IR/Value.h"

using namespace midend;

namespace {

class ValueTest : public ::testing::Test {
   protected:
    void SetUp() override { context = std::make_unique<Context>(); }

    std::unique_ptr<Context> context;
};

TEST_F(ValueTest, BasicValueProperties) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 42);

    EXPECT_EQ(val->getType(), int32Ty);
    EXPECT_EQ(val->getContext(), context.get());
    EXPECT_FALSE(val->hasName());

    val->setName("test_value");
    EXPECT_TRUE(val->hasName());
    EXPECT_EQ(val->getName(), "test_value");
}

TEST_F(ValueTest, UseDefChain) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    EXPECT_FALSE(val1->hasUses());
    EXPECT_EQ(val1->getNumUses(), 0u);

    // Create a simple User for testing
    class TestUser : public User {
       public:
        TestUser(Type* ty) : User(ty, ValueKind::User, 0, "test_user") {}
    };

    auto* user = new TestUser(int32Ty);
    user->addOperand(val1);
    user->addOperand(val2);

    EXPECT_TRUE(val1->hasUses());
    EXPECT_EQ(val1->getNumUses(), 1u);
    EXPECT_TRUE(val2->hasUses());
    EXPECT_EQ(val2->getNumUses(), 1u);

    EXPECT_EQ(user->getNumOperands(), 2u);
    EXPECT_EQ(user->getOperand(0), val1);
    EXPECT_EQ(user->getOperand(1), val2);

    delete user;
}

TEST_F(ValueTest, ReplaceAllUsesWith) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);
    auto* replacement = ConstantInt::get(int32Ty, 30);

    class TestUser : public User {
       public:
        TestUser(Type* ty) : User(ty, ValueKind::User, 0, "test_user") {}
    };

    auto* user1 = new TestUser(int32Ty);
    auto* user2 = new TestUser(int32Ty);

    user1->addOperand(val1);
    user1->addOperand(val2);
    user2->addOperand(val1);
    user2->addOperand(val2);

    EXPECT_EQ(val1->getNumUses(), 2u);

    val1->replaceAllUsesWith(replacement);

    EXPECT_EQ(val1->getNumUses(), 0u);
    EXPECT_EQ(replacement->getNumUses(), 2u);
    EXPECT_EQ(user1->getOperand(0), replacement);
    EXPECT_EQ(user2->getOperand(0), replacement);

    delete user1;
    delete user2;
}

TEST_F(ValueTest, ConstantCreation) {
    // Integer constants
    auto* int32Ty = context->getInt32Type();
    auto* constInt = ConstantInt::get(int32Ty, 42);
    EXPECT_EQ(constInt->getValue(), 42u);
    EXPECT_FALSE(constInt->isZero());
    EXPECT_FALSE(constInt->isOne());

    auto* zero = ConstantInt::get(int32Ty, 0);
    EXPECT_TRUE(zero->isZero());

    auto* one = ConstantInt::get(int32Ty, 1);
    EXPECT_TRUE(one->isOne());

    // Boolean constants
    auto* trueVal = ConstantInt::getTrue(context.get());
    auto* falseVal = ConstantInt::getFalse(context.get());
    EXPECT_TRUE(trueVal->isTrue());
    EXPECT_FALSE(falseVal->isTrue());

    // Float constants
    auto* floatTy = context->getFloatType();
    auto* constFP = ConstantFP::get(floatTy, 3.14);
    EXPECT_FLOAT_EQ(constFP->getValue(), 3.14f);
    EXPECT_FALSE(constFP->isZero());

    auto* zeroFP = ConstantFP::get(floatTy, 0.0);
    EXPECT_TRUE(zeroFP->isZero());
}

TEST_F(ValueTest, ValueKindAndCasting) {
    auto* int32Ty = context->getInt32Type();
    auto* constInt = ConstantInt::get(int32Ty, 42);

    Value* val = constInt;
    EXPECT_TRUE(isa<Constant>(*val));
    EXPECT_TRUE(isa<User>(*val));
    EXPECT_TRUE(isa<Value>(*val));

    auto* constant = dyn_cast<Constant>(val);
    EXPECT_NE(constant, nullptr);

    auto* user = dyn_cast<User>(val);
    EXPECT_NE(user, nullptr);
}

TEST_F(ValueTest, ComplexUseDefChains) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    class TestUser : public User {
       public:
        TestUser(Type* ty, unsigned numOps)
            : User(ty, ValueKind::User, numOps, "test_user") {}
    };

    // Create a chain: user1 -> val1, user2 -> user1, user3 -> user2
    auto* user1 = new TestUser(int32Ty, 1);
    auto* user2 = new TestUser(int32Ty, 1);
    auto* user3 = new TestUser(int32Ty, 2);

    user1->setOperand(0, val1);
    user2->setOperand(0, user1);
    user3->setOperand(0, user2);
    user3->setOperand(1, val2);

    // Check use counts
    EXPECT_EQ(val1->getNumUses(), 1u);
    EXPECT_EQ(val2->getNumUses(), 1u);
    EXPECT_EQ(user1->getNumUses(), 1u);
    EXPECT_EQ(user2->getNumUses(), 1u);
    EXPECT_EQ(user3->getNumUses(), 0u);

    // Check operands
    EXPECT_EQ(user1->getOperand(0), val1);
    EXPECT_EQ(user2->getOperand(0), user1);
    EXPECT_EQ(user3->getOperand(0), user2);
    EXPECT_EQ(user3->getOperand(1), val2);

    // Test iterating through uses
    std::vector<User*> val1Users;
    for (auto it = val1->use_begin(); it != val1->use_end(); ++it) {
        val1Users.push_back((*it)->getUser());
    }
    EXPECT_EQ(val1Users.size(), 1u);
    EXPECT_EQ(val1Users[0], user1);

    delete user3;
    delete user2;
    delete user1;
}

TEST_F(ValueTest, MultipleUsersOfSameValue) {
    auto* int32Ty = context->getInt32Type();
    auto* sharedVal = ConstantInt::get(int32Ty, 12345);

    class TestUser : public User {
       public:
        TestUser(Type* ty, unsigned numOps)
            : User(ty, ValueKind::User, numOps, "test_user") {}
    };

    std::vector<TestUser*> users;
    const int numUsers = 5;

    // Create multiple users of the same value
    for (int i = 0; i < numUsers; ++i) {
        auto* user = new TestUser(int32Ty, 2);
        user->setOperand(0, sharedVal);
        user->setOperand(1, ConstantInt::get(int32Ty, 1000 + i));
        users.push_back(user);
    }

    EXPECT_EQ(sharedVal->getNumUses(), numUsers);
    EXPECT_TRUE(sharedVal->hasUses());

    // Verify all users are in the use list
    std::set<User*> expectedUsers(users.begin(), users.end());
    std::set<User*> actualUsers;
    for (auto it = sharedVal->use_begin(); it != sharedVal->use_end(); ++it) {
        actualUsers.insert((*it)->getUser());
    }
    EXPECT_EQ(expectedUsers, actualUsers);

    // Clean up
    for (auto* user : users) {
        delete user;
    }
}

TEST_F(ValueTest, ValueReplacementInComplexHierarchy) {
    auto* int32Ty = context->getInt32Type();
    auto* original = ConstantInt::get(int32Ty, 100);
    auto* replacement = ConstantInt::get(int32Ty, 200);

    class TestUser : public User {
       public:
        TestUser(Type* ty, unsigned numOps)
            : User(ty, ValueKind::User, numOps, "test_user") {}
    };

    // Create a complex hierarchy
    auto* userA = new TestUser(int32Ty, 2);
    auto* userB = new TestUser(int32Ty, 1);
    auto* userC = new TestUser(int32Ty, 3);
    auto* userD = new TestUser(int32Ty, 1);

    userA->setOperand(0, original);
    userA->setOperand(1, ConstantInt::get(int32Ty, 1));
    userB->setOperand(0, original);
    userC->setOperand(0, userA);
    userC->setOperand(1, original);
    userC->setOperand(2, userB);
    userD->setOperand(0, userC);

    // Verify initial state
    EXPECT_EQ(original->getNumUses(), 3u);
    EXPECT_EQ(replacement->getNumUses(), 0u);

    // Replace all uses
    original->replaceAllUsesWith(replacement);

    // Verify replacement
    EXPECT_EQ(original->getNumUses(), 0u);
    EXPECT_EQ(replacement->getNumUses(), 3u);
    EXPECT_EQ(userA->getOperand(0), replacement);
    EXPECT_EQ(userB->getOperand(0), replacement);
    EXPECT_EQ(userC->getOperand(1), replacement);

    // Clean up
    delete userD;
    delete userC;
    delete userB;
    delete userA;
}

TEST_F(ValueTest, UseIteratorStability) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 777);  // Use a unique value

    class TestUser : public User {
       public:
        TestUser(Type* ty) : User(ty, ValueKind::User, 1, "test_user") {}
    };

    std::vector<TestUser*> users;
    const int numUsers = 5;

    // Create users
    for (int i = 0; i < numUsers; ++i) {
        auto* user = new TestUser(int32Ty);
        user->setOperand(0, val);
        users.push_back(user);
    }

    // Test iterator stability - collect users first, then delete
    std::vector<User*> usersToDelete;
    for (auto it = val->use_begin(); it != val->use_end(); ++it) {
        usersToDelete.push_back((*it)->getUser());
    }

    EXPECT_EQ(usersToDelete.size(), numUsers);

    // Now delete all users
    for (auto* user : usersToDelete) {
        delete user;
    }

    EXPECT_EQ(val->getNumUses(), 0u);
}

TEST_F(ValueTest, InstructionAsValueAndUser) {
    auto module = std::make_unique<Module>("test", context.get());
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* func = Function::Create(fnTy, "test_func", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", func);

    IRBuilder builder(bb);
    auto* arg0 = func->getArg(0);
    auto* arg1 = func->getArg(1);
    auto* const42 = builder.getInt32(42);

    // Create a chain of instructions
    auto* add1 = builder.createAdd(arg0, arg1, "add1");
    auto* add2 = builder.createAdd(add1, const42, "add2");
    auto* mul = builder.createMul(add2, add1, "mul");

    // Test that instructions are both Values and Users
    EXPECT_TRUE(isa<Value>(*add1));
    EXPECT_TRUE(isa<User>(*add1));
    EXPECT_TRUE(isa<Instruction>(*add1));

    // Test use-def relationships
    EXPECT_EQ(add1->getNumUses(), 2u);  // Used by add2 and mul
    EXPECT_EQ(add2->getNumUses(), 1u);  // Used by mul
    EXPECT_EQ(mul->getNumUses(), 0u);   // Not used

    // Test operands
    EXPECT_EQ(add1->getOperand(0), arg0);
    EXPECT_EQ(add1->getOperand(1), arg1);
    EXPECT_EQ(add2->getOperand(0), add1);
    EXPECT_EQ(add2->getOperand(1), const42);
    EXPECT_EQ(mul->getOperand(0), add2);
    EXPECT_EQ(mul->getOperand(1), add1);

    // Test users of add1
    std::set<User*> add1Users;
    for (auto it = add1->use_begin(); it != add1->use_end(); ++it) {
        add1Users.insert((*it)->getUser());
    }
    EXPECT_EQ(add1Users.size(), 2u);
    EXPECT_TRUE(add1Users.count(add2));
    EXPECT_TRUE(add1Users.count(mul));
}

TEST_F(ValueTest, OperandManipulation) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);
    auto* val3 = ConstantInt::get(int32Ty, 30);

    class TestUser : public User {
       public:
        TestUser(Type* ty, unsigned numOps)
            : User(ty, ValueKind::User, numOps, "test_user") {}

        void replaceOperand(unsigned idx, Value* newVal) {
            setOperand(idx, newVal);
        }
    };

    auto* user = new TestUser(int32Ty, 3);
    user->setOperand(0, val1);
    user->setOperand(1, val2);
    user->setOperand(2, val3);

    // Test initial state
    EXPECT_EQ(user->getNumOperands(), 3u);
    EXPECT_EQ(user->getOperand(0), val1);
    EXPECT_EQ(user->getOperand(1), val2);
    EXPECT_EQ(user->getOperand(2), val3);
    EXPECT_EQ(val1->getNumUses(), 1u);
    EXPECT_EQ(val2->getNumUses(), 1u);
    EXPECT_EQ(val3->getNumUses(), 1u);

    // Replace operand
    auto* newVal = ConstantInt::get(int32Ty, 40);
    user->replaceOperand(1, newVal);

    EXPECT_EQ(user->getOperand(1), newVal);
    EXPECT_EQ(val2->getNumUses(), 0u);
    EXPECT_EQ(newVal->getNumUses(), 1u);

    // Test out-of-bounds access
    EXPECT_EQ(user->getOperand(10), nullptr);

    delete user;
}

TEST_F(ValueTest, ConstantPooling) {
    auto* int32Ty = context->getInt32Type();

    // Test that identical constants are pooled
    auto* const1a = ConstantInt::get(int32Ty, 42);
    auto* const1b = ConstantInt::get(int32Ty, 42);
    auto* const2 = ConstantInt::get(int32Ty, 43);

    EXPECT_EQ(const1a, const1b);  // Should be the same object
    EXPECT_NE(const1a, const2);   // Should be different objects

    // Test float constants
    auto* floatTy = context->getFloatType();
    auto* float1a = ConstantFP::get(floatTy, 3.14f);
    auto* float1b = ConstantFP::get(floatTy, 3.14f);
    auto* float2 = ConstantFP::get(floatTy, 2.71f);

    EXPECT_EQ(float1a, float1b);
    EXPECT_NE(float1a, float2);
}

TEST_F(ValueTest, ValueHierarchyCasting) {
    auto module = std::make_unique<Module>("test", context.get());
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(fnTy, "test_func", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", func);

    IRBuilder builder(bb);
    auto* constant = builder.getInt32(42);
    auto* arg = func->getArg(0);
    auto* inst = builder.createAdd(constant, arg, "add");

    // Test Value casting
    Value* values[] = {constant, arg, inst, func, bb};

    for (auto* val : values) {
        EXPECT_TRUE(isa<Value>(*val));
    }

    // Test specific casting
    EXPECT_TRUE(isa<Constant>(*constant));
    EXPECT_FALSE(isa<Instruction>(*constant));
    EXPECT_FALSE(isa<Function>(*constant));

    EXPECT_TRUE(isa<Argument>(*arg));
    EXPECT_FALSE(isa<Constant>(*arg));
    EXPECT_FALSE(isa<Instruction>(*arg));

    EXPECT_TRUE(isa<Instruction>(*inst));
    EXPECT_TRUE(isa<User>(*inst));
    EXPECT_FALSE(isa<Constant>(*inst));

    EXPECT_TRUE(isa<Function>(*func));
    EXPECT_TRUE(isa<Constant>(*func));
    EXPECT_FALSE(isa<Instruction>(*func));

    EXPECT_TRUE(isa<BasicBlock>(*bb));
    EXPECT_FALSE(isa<Instruction>(*bb));
    // Note: BasicBlock is considered a User in this implementation due to
    // ValueKind ordering
    EXPECT_TRUE(isa<User>(*bb));
}

TEST_F(ValueTest, UseListModificationDuringIteration) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 999);  // Use a unique value

    class TestUser : public User {
       public:
        TestUser(Type* ty, int id)
            : User(ty, ValueKind::User, 1, "test_user"), id_(id) {}
        int getId() const { return id_; }

       private:
        int id_;
    };

    std::vector<TestUser*> users;
    for (int i = 0; i < 5; ++i) {
        auto* user = new TestUser(int32Ty, i);
        user->setOperand(0, val);
        users.push_back(user);
    }

    EXPECT_EQ(val->getNumUses(), 5u);

    // Collect user IDs while iterating
    std::vector<int> userIds;
    for (auto it = val->use_begin(); it != val->use_end(); ++it) {
        auto* testUser = static_cast<TestUser*>((*it)->getUser());
        userIds.push_back(testUser->getId());
    }

    EXPECT_EQ(userIds.size(), 5u);
    // Verify we got all IDs from 0 to 4
    std::sort(userIds.begin(), userIds.end());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(userIds[i], i);
    }

    // Clean up
    for (auto* user : users) {
        delete user;
    }

    EXPECT_EQ(val->getNumUses(), 0u);
}

TEST_F(ValueTest, EdgeCasesAndErrorConditions) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 12345);  // Use a unique value

    // Test empty use list iteration
    EXPECT_EQ(val->use_begin(), val->use_end());
    EXPECT_FALSE(val->hasUses());
    EXPECT_FALSE(val->hasOneUse());

    class TestUser : public User {
       public:
        TestUser(Type* ty) : User(ty, ValueKind::User, 0, "test_user") {}
    };

    auto* user = new TestUser(int32Ty);

    // Test user with no operands
    EXPECT_EQ(user->getNumOperands(), 0u);
    EXPECT_EQ(user->getOperand(0), nullptr);

    delete user;

    // Test hasOneUse
    auto* singleUser = new TestUser(int32Ty);
    singleUser->addOperand(val);
    EXPECT_TRUE(val->hasOneUse());

    auto* secondUser = new TestUser(int32Ty);
    secondUser->addOperand(val);
    EXPECT_FALSE(val->hasOneUse());

    delete singleUser;
    delete secondUser;
}

TEST_F(ValueTest, ConstUseIterators) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 87654);

    class TestUser : public User {
       public:
        TestUser(Type* ty, int id)
            : User(ty, ValueKind::User, 1, "test_user"), id_(id) {}
        int getId() const { return id_; }

       private:
        int id_;
    };

    std::vector<TestUser*> users;
    for (int i = 0; i < 3; ++i) {
        auto* user = new TestUser(int32Ty, i);
        user->setOperand(0, val);
        users.push_back(user);
    }

    // Test const use iterators
    const Value* constVal = val;
    auto constBegin = constVal->use_begin();
    auto constEnd = constVal->use_end();

    EXPECT_NE(constBegin, constEnd);

    // Count users through const iterator
    int count = 0;
    for (auto it = constBegin; it != constEnd; ++it) {
        ++count;
        EXPECT_NE((*it)->getUser(), nullptr);
    }
    EXPECT_EQ(count, 3);

    // Clean up
    for (auto* user : users) {
        delete user;
    }
}

TEST_F(ValueTest, UseRanges) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 98765);

    class TestUser : public User {
       public:
        TestUser(Type* ty, int id)
            : User(ty, ValueKind::User, 1, "test_user"), id_(id) {}
        int getId() const { return id_; }

       private:
        int id_;
    };

    std::vector<TestUser*> users;
    for (int i = 0; i < 4; ++i) {
        auto* user = new TestUser(int32Ty, i);
        user->setOperand(0, val);
        users.push_back(user);
    }

    // Test UseRange
    auto userRange = val->users();
    int count = 0;
    for (auto it = userRange.begin(); it != userRange.end(); ++it) {
        ++count;
        EXPECT_NE((*it)->getUser(), nullptr);
    }
    EXPECT_EQ(count, 4);

    // Test ConstUseRange
    const Value* constVal = val;
    auto constUserRange = constVal->users();
    count = 0;
    for (auto it = constUserRange.begin(); it != constUserRange.end(); ++it) {
        ++count;
        EXPECT_NE((*it)->getUser(), nullptr);
    }
    EXPECT_EQ(count, 4);

    // Clean up
    for (auto* user : users) {
        delete user;
    }
}

TEST_F(ValueTest, ValueToString) {
    // Create a module and function to test instruction toString
    auto module = std::make_unique<Module>("test", context.get());
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* func = Function::Create(fnTy, "test_func", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", func);

    IRBuilder builder(bb);
    auto* arg0 = func->getArg(0);
    auto* arg1 = func->getArg(1);

    // Test named instruction - instructions use the base Value::toString()
    auto* namedInst = builder.createAdd(arg0, arg1, "test_add");
    EXPECT_EQ(namedInst->toString(), "%test_add");

    // Test unnamed instruction
    auto* unnamedInst = builder.createAdd(arg0, arg1);
    EXPECT_EQ(unnamedInst->toString(), "%unnamed");

    // Test instruction with empty name
    auto* emptyNameInst = builder.createAdd(arg0, arg1, "");
    EXPECT_EQ(emptyNameInst->toString(), "%unnamed");

    // Constants override toString() to show their values
    auto* constant = ConstantInt::get(int32Ty, 42);
    EXPECT_EQ(constant->toString(), "42");
}

}  // namespace