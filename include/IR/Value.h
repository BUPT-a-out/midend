#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Support/Casting.h"

namespace midend {

class Type;
class Use;
class User;
class Context;

class Value {
   private:
    Type* type_;
    Use* useListHead_;  // Head of intrusive linked list
    std::string name_;
    ValueKind valueKind_;

   protected:
    Value(Type* ty, ValueKind kind, const std::string& name = "")
        : type_(ty), useListHead_(nullptr), name_(name), valueKind_(kind) {}

   public:
    virtual ~Value();

    Type* getType() const { return type_; }
    ValueKind getValueKind() const { return valueKind_; }

    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    bool hasName() const { return !name_.empty(); }

    void addUse(Use* use);
    void removeUse(Use* use);

    class use_iterator {
       private:
        Use* current_;

       public:
        use_iterator(Use* use) : current_(use) {}
        Use* operator*() const { return current_; }
        Use* operator->() const { return current_; }
        use_iterator& operator++();
        bool operator!=(const use_iterator& other) const {
            return current_ != other.current_;
        }
        bool operator==(const use_iterator& other) const {
            return current_ == other.current_;
        }
    };

    using const_use_iterator = use_iterator;

    use_iterator use_begin();
    use_iterator use_end();
    const_use_iterator use_begin() const;
    const_use_iterator use_end() const;

    bool hasUses() const { return useListHead_ != nullptr; }
    bool hasOneUse() const;
    size_t getNumUses() const;

    struct UseRange {
        Use* head;
        UseRange(Use* h) : head(h) {}
        use_iterator begin();
        use_iterator end();
        bool empty() const { return head == nullptr; }
    };

    struct ConstUseRange {
        Use* head;
        ConstUseRange(Use* h) : head(h) {}
        const_use_iterator begin() const;
        const_use_iterator end() const;
        bool empty() const { return head == nullptr; }
    };

    UseRange users();
    ConstUseRange users() const;

    void replaceAllUsesWith(Value* newValue);

    Context* getContext() const;

    virtual std::string toString() const;

    static bool classof(const Value*) { return true; }
};

class Use {
   private:
    Value* value_;
    User* user_;
    unsigned operandNo_;

    Use* next_;
    Use* prev_;

    friend class Value;

   public:
    Use(Value* v, User* u, unsigned opNo)
        : value_(v),
          user_(u),
          operandNo_(opNo),
          next_(nullptr),
          prev_(nullptr) {
        if (value_) {
            value_->addUse(this);
        }
    }

    ~Use() {
        if (value_) {
            value_->removeUse(this);
        }
    }

    Use(const Use&) = delete;
    Use& operator=(const Use&) = delete;

    Value* get() const { return value_; }
    User* getUser() const { return user_; }
    unsigned getOperandNo() const { return operandNo_; }

    void set(Value* v) {
        if (value_) {
            value_->removeUse(this);
        }
        value_ = v;
        if (value_) {
            value_->addUse(this);
        }
    }

    operator Value*() const { return value_; }
    Value* operator->() const { return value_; }
    Value& operator*() const { return *value_; }
};

class User : public Value {
   private:
    std::vector<std::unique_ptr<Use>> operands_;

   protected:
    User(Type* ty, ValueKind kind, unsigned numOps,
         const std::string& name = "")
        : Value(ty, kind, name) {
        if (numOps > 0) {
            operands_.reserve(numOps);
            for (unsigned i = 0; i < numOps; ++i) {
                operands_.push_back(std::make_unique<Use>(nullptr, this, i));
            }
        }
    }

   public:
    using op_iterator = std::vector<std::unique_ptr<Use>>::iterator;
    using const_op_iterator = std::vector<std::unique_ptr<Use>>::const_iterator;

    op_iterator op_begin() { return operands_.begin(); }
    op_iterator op_end() { return operands_.end(); }
    const_op_iterator op_begin() const { return operands_.begin(); }
    const_op_iterator op_end() const { return operands_.end(); }

    Value* getOperand(unsigned i) const {
        return i < operands_.size() ? operands_[i]->get() : nullptr;
    }

    void setOperand(unsigned i, Value* v) {
        if (i < operands_.size()) {
            operands_[i]->set(v);
        }
    }

    unsigned getNumOperands() const { return operands_.size(); }

    void addOperand(Value* v) {
        operands_.push_back(std::make_unique<Use>(v, this, operands_.size()));
    }

    void dropAllReferences() {
        for (auto& use : operands_) {
            use->set(nullptr);
        }
    }

    static bool classof(const Value* v) {
        return v->getValueKind() >= ValueKind::User;
    }
};

}  // namespace midend