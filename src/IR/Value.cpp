#include "IR/Value.h"

#include <sstream>

#include "IR/Type.h"

namespace midend {

Value::~Value() {
    while (useListHead_) {
        Use* use = useListHead_;
        useListHead_ = use->next_;
        use->value_ = nullptr;
        use->next_ = nullptr;
        use->prev_ = nullptr;
    }
}

Value::use_iterator& Value::use_iterator::operator++() {
    current_ = current_->next_;
    return *this;
}

Value::use_iterator Value::use_begin() { return use_iterator(useListHead_); }

Value::use_iterator Value::use_end() { return use_iterator(nullptr); }

Value::const_use_iterator Value::use_begin() const {
    return use_iterator(useListHead_);
}

Value::const_use_iterator Value::use_end() const {
    return use_iterator(nullptr);
}

bool Value::hasOneUse() const { return useListHead_ && !useListHead_->next_; }

size_t Value::getNumUses() const {
    size_t count = 0;
    for (Use* use = useListHead_; use; use = use->next_) {
        ++count;
    }
    return count;
}

Value::use_iterator Value::UseRange::begin() { return use_iterator(head); }

Value::use_iterator Value::UseRange::end() { return use_iterator(nullptr); }

Value::const_use_iterator Value::ConstUseRange::begin() const {
    return const_use_iterator(head);
}

Value::const_use_iterator Value::ConstUseRange::end() const {
    return const_use_iterator(nullptr);
}

Value::UseRange Value::users() { return UseRange(useListHead_); }

Value::ConstUseRange Value::users() const {
    return ConstUseRange(useListHead_);
}

void Value::addUse(Use* use) {
    use->next_ = useListHead_;
    use->prev_ = nullptr;
    if (useListHead_) {
        useListHead_->prev_ = use;
    }
    useListHead_ = use;
}

void Value::removeUse(Use* use) {
    if (use->prev_) {
        use->prev_->next_ = use->next_;
    } else {
        useListHead_ = use->next_;
    }
    if (use->next_) {
        use->next_->prev_ = use->prev_;
    }
    use->next_ = nullptr;
    use->prev_ = nullptr;
}

void Value::replaceAllUsesWith(Value* newValue) {
    while (useListHead_) {
        Use* use = useListHead_;
        use->set(newValue);
    }
}

Context* Value::getContext() const {
    return type_ ? type_->getContext() : nullptr;
}

std::string Value::toString() const {
    std::stringstream ss;
    if (hasName()) {
        ss << "%" << getName();
    } else {
        ss << "%unnamed";
    }
    return ss.str();
}

}  // namespace midend