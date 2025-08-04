#include "Pass/Pass.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "Support/Logger.h"

namespace midend {

static Logger globalLogger("midend");

enum class TokenType {
    IDENTIFIER,
    LPAREN,
    RPAREN,
    COMMA,
    ASTERISK,
    NUMBER,
    WHITESPACE,
    EOF_TOKEN
};

struct Token {
    TokenType type;
    std::string value;
    size_t position;
    size_t line;
    size_t column;
};

class ASTNode {
   public:
    virtual ~ASTNode() = default;
    virtual bool execute(PassManager& pm, PassBuilder& builder,
                         Module* module = nullptr) = 0;
    virtual bool execute(FunctionPassManager& fpm, PassBuilder& builder) = 0;
    virtual std::string toString() const = 0;
    virtual bool validate(PassBuilder& builder, std::string& error,
                          size_t& position) const = 0;
};

class PassNode : public ASTNode {
   private:
    std::string passName_;
    size_t position_;

   public:
    PassNode(const std::string& name, size_t pos)
        : passName_(name), position_(pos) {}

    bool execute(PassManager& pm, PassBuilder& builder,
                 Module* module) override {
        (void)module;
        auto pass = builder.createPass(passName_);
        if (!pass) {
            return false;
        }
        pm.addPass(std::move(pass));
        return true;
    }

    bool execute(FunctionPassManager& fpm, PassBuilder& builder) override {
        auto pass = builder.createPass(passName_);
        if (!pass) {
            return false;
        }
        if (pass->getKind() == Pass::PassKind::ModulePass) {
            return false;
        }
        fpm.addPass(std::move(pass));
        return true;
    }

    std::string toString() const override { return passName_; }

    bool validate(PassBuilder& builder, std::string& error,
                  size_t& position) const override {
        auto pass = builder.createPass(passName_);
        if (!pass) {
            error = "Unknown pass: '" + passName_ + "'";
            position = position_;
            return false;
        }
        return true;
    }

    const std::string& getName() const { return passName_; }
    size_t getPosition() const { return position_; }
};

class SequenceNode : public ASTNode {
   private:
    std::vector<std::unique_ptr<ASTNode>> children_;

   public:
    void addChild(std::unique_ptr<ASTNode> child) {
        children_.push_back(std::move(child));
    }

    bool execute(PassManager& pm, PassBuilder& builder,
                 Module* module) override {
        for (auto& child : children_) {
            if (!child->execute(pm, builder, module)) {
                return false;
            }
        }
        return true;
    }

    bool execute(FunctionPassManager& fpm, PassBuilder& builder) override {
        for (auto& child : children_) {
            if (!child->execute(fpm, builder)) {
                return false;
            }
        }
        return true;
    }

    std::string toString() const override {
        std::string result;
        for (size_t i = 0; i < children_.size(); ++i) {
            if (i > 0) result += ",";
            result += children_[i]->toString();
        }
        return result;
    }

    bool validate(PassBuilder& builder, std::string& error,
                  size_t& position) const override {
        for (const auto& child : children_) {
            if (!child->validate(builder, error, position)) {
                return false;
            }
        }
        return true;
    }

    bool isEmpty() const { return children_.empty(); }
};

class LoopNode : public ASTNode {
   private:
    std::unique_ptr<ASTNode> body_;
    int maxIterations_;
    int position_;

   public:
    LoopNode(std::unique_ptr<ASTNode> body, int maxIter, size_t pos)
        : body_(std::move(body)), maxIterations_(maxIter), position_(pos) {}

    bool execute(PassManager& pm, PassBuilder& builder,
                 Module* module) override {
        (void)module;

        if (maxIterations_ == 0) {
            return true;
        }

        pm.beginLoop(maxIterations_);
        if (!body_->execute(pm, builder, module)) {
            return false;
        }
        pm.endLoop();

        return true;
    }

    bool execute(FunctionPassManager& fpm, PassBuilder& builder) override {
        int iteration = 0;

        for (int i = 0; i < (maxIterations_ < 0 ? 1 : maxIterations_); ++i) {
            if (!body_->execute(fpm, builder)) {
                return false;
            }
            iteration++;
        }

        return true;
    }

    std::string toString() const override {
        std::string result = "(" + body_->toString() + ")";
        if (maxIterations_ >= 0) {
            result += "*" + std::to_string(maxIterations_);
        }
        return result;
    }

    bool validate(PassBuilder& builder, std::string& error,
                  size_t& position) const override {
        return body_->validate(builder, error, position);
    }
};

class Tokenizer {
   private:
    std::string input_;
    size_t position_;
    size_t line_;
    size_t column_;
    std::vector<Token> tokens_;

    bool isAlpha(char c) const {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    bool isAlphaNum(char c) const {
        return isAlpha(c) || (c >= '0' && c <= '9');
    }

    bool isDigit(char c) const { return c >= '0' && c <= '9'; }

    bool isWhitespace(char c) const {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    void skipWhitespace() {
        while (position_ < input_.length() && isWhitespace(input_[position_])) {
            if (input_[position_] == '\n') {
                line_++;
                column_ = 1;
            } else {
                column_++;
            }
            position_++;
        }
    }

    Token makeToken(TokenType type, const std::string& value, size_t startPos) {
        return {type, value, startPos, line_, column_ - value.length()};
    }

   public:
    ParseResult tokenize(const std::string& input) {
        input_ = input;
        position_ = 0;
        line_ = 1;
        column_ = 1;
        tokens_.clear();

        while (position_ < input_.length()) {
            skipWhitespace();

            if (position_ >= input_.length()) break;

            size_t startPos = position_;
            char current = input_[position_];

            if (isAlpha(current)) {
                std::string identifier;
                while (position_ < input_.length() &&
                       isAlphaNum(input_[position_])) {
                    identifier += input_[position_++];
                    column_++;
                }
                tokens_.push_back(
                    makeToken(TokenType::IDENTIFIER, identifier, startPos));

            } else if (isDigit(current)) {
                std::string number;
                while (position_ < input_.length() &&
                       isDigit(input_[position_])) {
                    number += input_[position_++];
                    column_++;
                }
                tokens_.push_back(
                    makeToken(TokenType::NUMBER, number, startPos));

            } else {
                switch (current) {
                    case '(':
                        tokens_.push_back(
                            makeToken(TokenType::LPAREN, "(", startPos));
                        break;
                    case ')':
                        tokens_.push_back(
                            makeToken(TokenType::RPAREN, ")", startPos));
                        break;
                    case ',':
                        tokens_.push_back(
                            makeToken(TokenType::COMMA, ",", startPos));
                        break;
                    case '*':
                        tokens_.push_back(
                            makeToken(TokenType::ASTERISK, "*", startPos));
                        break;
                    default:
                        return ParseResult::createError(
                            "Unexpected character: '" +
                                std::string(1, current) + "'",
                            position_);
                }
                position_++;
                column_++;
            }
        }

        tokens_.push_back(makeToken(TokenType::EOF_TOKEN, "", position_));
        return ParseResult::createSuccess();
    }

    const std::vector<Token>& getTokens() const { return tokens_; }
};

class Parser {
   private:
    std::vector<Token> tokens_;
    size_t currentToken_;

    const Token& current() const {
        if (currentToken_ >= tokens_.size()) {
            static Token eofToken{TokenType::EOF_TOKEN, "", 0, 0, 0};
            return eofToken;
        }
        return tokens_[currentToken_];
    }

    bool match(TokenType type) {
        if (current().type == type) {
            currentToken_++;
            return true;
        }
        return false;
    }

    ParseResult expect(TokenType type, const std::string& context) {
        if (current().type != type) {
            std::string expected = tokenTypeToString(type);
            std::string actual = tokenTypeToString(current().type);
            return ParseResult::createError(
                "Expected " + expected + " in " + context + ", got " + actual,
                current().position);
        }
        currentToken_++;
        return ParseResult::createSuccess();
    }

    std::string tokenTypeToString(TokenType type) const {
        switch (type) {
            case TokenType::IDENTIFIER:
                return "pass name";
            case TokenType::LPAREN:
                return "'('";
            case TokenType::RPAREN:
                return "')'";
            case TokenType::COMMA:
                return "','";
            case TokenType::ASTERISK:
                return "'*'";
            case TokenType::NUMBER:
                return "number";
            case TokenType::EOF_TOKEN:
                return "end of input";
            default:
                return "unknown";
        }
    }

    std::pair<ParseResult, std::unique_ptr<ASTNode>> parsePipeline() {
        return parseSequence();
    }

    std::pair<ParseResult, std::unique_ptr<ASTNode>> parseSequence() {
        auto sequence = std::make_unique<SequenceNode>();

        if (current().type == TokenType::EOF_TOKEN) {
            return {ParseResult::createSuccess(), std::move(sequence)};
        }

        auto [result, element] = parseElement();
        if (!result.success) {
            return {result, nullptr};
        }
        sequence->addChild(std::move(element));

        while (current().type != TokenType::EOF_TOKEN &&
               current().type != TokenType::RPAREN) {
            if (current().type == TokenType::COMMA) {
                match(TokenType::COMMA);
                if (current().type == TokenType::EOF_TOKEN ||
                    current().type == TokenType::RPAREN) {
                    break;
                }
            }

            if (current().type != TokenType::IDENTIFIER &&
                current().type != TokenType::LPAREN) {
                break;
            }

            auto [elemResult, nextElement] = parseElement();
            if (!elemResult.success) {
                return {elemResult, nullptr};
            }
            sequence->addChild(std::move(nextElement));
        }

        return {ParseResult::createSuccess(), std::move(sequence)};
    }

    std::pair<ParseResult, std::unique_ptr<ASTNode>> parseElement() {
        if (current().type == TokenType::IDENTIFIER) {
            return parsePass();
        } else if (current().type == TokenType::LPAREN) {
            return parseLoop();
        } else {
            return {ParseResult::createError("Expected pass name or '('",
                                             current().position),
                    nullptr};
        }
    }

    std::pair<ParseResult, std::unique_ptr<ASTNode>> parsePass() {
        if (current().type != TokenType::IDENTIFIER) {
            return {ParseResult::createError("Expected pass name",
                                             current().position),
                    nullptr};
        }

        std::string passName = current().value;
        size_t position = current().position;
        currentToken_++;

        return {ParseResult::createSuccess(),
                std::make_unique<PassNode>(passName, position)};
    }

    std::pair<ParseResult, std::unique_ptr<ASTNode>> parseLoop() {
        size_t loopStartPos = current().position;

        auto result = expect(TokenType::LPAREN, "loop");
        if (!result.success) return {result, nullptr};

        auto [seqResult, body] = parseSequence();
        if (!seqResult.success) return {seqResult, nullptr};

        result = expect(TokenType::RPAREN, "loop");
        if (!result.success) return {result, nullptr};

        int maxIterations = -1;

        if (match(TokenType::ASTERISK)) {
            if (current().type != TokenType::NUMBER) {
                return {ParseResult::createError("Expected number after '*'",
                                                 current().position),
                        nullptr};
            }

            try {
                maxIterations = std::stoi(current().value);
                if (maxIterations < 0) {
                    return {ParseResult::createError(
                                "Repetition count must be non-negative",
                                current().position),
                            nullptr};
                }
            } catch (const std::exception&) {
                return {ParseResult::createError(
                            "Invalid number: " + current().value,
                            current().position),
                        nullptr};
            }
            currentToken_++;
        }

        return {ParseResult::createSuccess(),
                std::make_unique<LoopNode>(std::move(body), maxIterations,
                                           loopStartPos)};
    }

   public:
    std::pair<ParseResult, std::unique_ptr<ASTNode>> parse(
        const std::vector<Token>& tokens) {
        tokens_ = tokens;
        currentToken_ = 0;

        auto [result, ast] = parsePipeline();
        if (!result.success) return {result, nullptr};

        if (current().type != TokenType::EOF_TOKEN) {
            return {ParseResult::createError("Unexpected token at end of input",
                                             current().position),
                    nullptr};
        }

        return {result, std::move(ast)};
    }
};

// ============================================================================
// Pass Implementation
// ============================================================================

bool FunctionPass::runOnModule(Module& m, AnalysisManager& am) {
    bool changed = false;
    for (auto& fn : m) {
        changed |= runOnFunction(*fn, am);
    }
    return changed;
}

bool BasicBlockPass::runOnFunction(Function& f, AnalysisManager& am) {
    bool changed = false;
    for (auto& bb : f) {
        changed |= runOnBasicBlock(*bb, am);
    }
    return changed;
}

// ============================================================================
// PassManager Implementation
// ============================================================================

bool PassManager::runPassOnModule(Pass& pass, Module& m) {
    globalLogger.info() << "Running pass: " << pass.getName();
    bool changed = false;
    std::unordered_set<std::string> required, preserved;
    pass.getAnalysisUsage(required, preserved);
    if (pass.getKind() == Pass::PassKind::FunctionPass ||
        pass.getKind() == Pass::PassKind::BasicBlockPass) {
        for (auto f : m) {
            changed |= runPassOnFunction(pass, *f);
        }
    } else {
        changed = pass.runOnModule(m, analysisManager_);
    }

    if (changed) {
        analysisManager_.invalidateAllAnalyses(m);
        globalLogger.debug() << "Invalidated all analyses";
    }

    return changed;
}

bool PassManager::runPassOnFunction(Pass& pass, Function& f) {
    std::unordered_set<std::string> before_required, before_preserved;
    pass.getAnalysisUsage(before_required, before_preserved);

    bool changed = false;
    switch (pass.getKind()) {
        case Pass::PassKind::FunctionPass:
        case Pass::PassKind::BasicBlockPass:
            changed = pass.runOnFunction(f, analysisManager_);
            break;
        default:
            globalLogger.warning()
                << "Module passes cannot be run on functions: "
                << pass.getName();
            break;
    }

    if (changed) {
        std::unordered_set<std::string> after_required, after_preserved;
        pass.getAnalysisUsage(after_required, after_preserved);
        auto registeredAnalyses = analysisManager_.getRegisteredAnalyses(f);
        for (const auto& analysisName : registeredAnalyses) {
            if (after_preserved.find(analysisName) == after_preserved.end()) {
                analysisManager_.invalidateAnalysis(analysisName, f);
                globalLogger.debug()
                    << "Invalidated function analysis: " << analysisName
                    << " for function " << f.getName();
            }
        }
    }

    return changed;
}

bool FunctionPassManager::runPassOnFunction(Pass& pass, Function& f) {
    std::unordered_set<std::string> before_required, before_preserved;
    pass.getAnalysisUsage(before_required, before_preserved);

    bool changed = false;
    switch (pass.getKind()) {
        case Pass::PassKind::FunctionPass:
        case Pass::PassKind::BasicBlockPass:
            changed = pass.runOnFunction(f, analysisManager_);
            break;
        default:
            globalLogger.warning()
                << "Module passes cannot be run on functions: "
                << pass.getName();
            break;
    }

    if (changed) {
        std::unordered_set<std::string> after_required, after_preserved;
        pass.getAnalysisUsage(after_required, after_preserved);
        auto registeredAnalyses = analysisManager_.getRegisteredAnalyses(f);
        for (const auto& analysisName : registeredAnalyses) {
            if (after_preserved.find(analysisName) == after_preserved.end()) {
                analysisManager_.invalidateAnalysis(analysisName, f);
            }
        }
    }

    return changed;
}

ParseResult PassBuilder::parsePassPipeline(PassManager& pm,
                                           const std::string& pipeline) {
    try {
        Tokenizer tokenizer;
        auto tokenResult = tokenizer.tokenize(pipeline);
        if (!tokenResult.success) {
            return tokenResult;
        }

        Parser parser;
        auto [parseResult, ast] = parser.parse(tokenizer.getTokens());
        if (!parseResult.success) {
            return parseResult;
        }

        if (!ast) {
            return ParseResult::createError("Failed to create AST", 0);
        }

        std::string error;
        size_t errorPos;
        if (!ast->validate(*this, error, errorPos)) {
            return ParseResult::createError(error, errorPos);
        }

        if (!ast->execute(pm, *this, nullptr)) {
            return ParseResult::createError("Failed to execute pipeline", 0);
        }

        return ParseResult::createSuccess();

    } catch (const std::exception& e) {
        return ParseResult::createError(
            std::string("Internal error: ") + e.what(), 0);
    }
}

ParseResult PassBuilder::parsePassPipeline(FunctionPassManager& fpm,
                                           const std::string& pipeline) {
    try {
        Tokenizer tokenizer;
        auto tokenResult = tokenizer.tokenize(pipeline);
        if (!tokenResult.success) {
            return tokenResult;
        }

        Parser parser;
        auto [parseResult, ast] = parser.parse(tokenizer.getTokens());
        if (!parseResult.success) {
            return parseResult;
        }

        if (!ast) {
            return ParseResult::createError("Failed to create AST", 0);
        }

        std::string error;
        size_t errorPos;
        if (!ast->validate(*this, error, errorPos)) {
            return ParseResult::createError(error, errorPos);
        }

        if (!ast->execute(fpm, *this)) {
            return ParseResult::createError("Failed to execute pipeline", 0);
        }

        return ParseResult::createSuccess();

    } catch (const std::exception& e) {
        return ParseResult::createError(
            std::string("Internal error: ") + e.what(), 0);
    }
}

bool PassBuilder::parsePassPipeline(FunctionPassManager& fpm,
                                    const std::string& pipeline, bool) {
    std::istringstream iss(pipeline);
    std::string passName;

    while (std::getline(iss, passName, ',')) {
        passName.erase(0, passName.find_first_not_of(" \t"));
        passName.erase(passName.find_last_not_of(" \t") + 1);

        auto pass = createPass(passName);
        if (!pass) {
            return false;
        }

        if (pass->getKind() == Pass::PassKind::ModulePass) {
            return false;
        }

        fpm.addPass(std::move(pass));
    }

    return true;
}

void PassManager::beginLoop(int maxIterations) {
    passItems_.push_back(
        {PassItemType::LOOP_BEGIN, nullptr, maxIterations, ""});
}

void PassManager::endLoop() {
    passItems_.push_back({PassItemType::LOOP_END, nullptr, 0, ""});
}

std::string PassManager::generateLoopContent(size_t start, size_t end) const {
    std::string result;
    for (size_t i = start; i < end; ++i) {
        if (passItems_[i].type == PassItemType::PASS) {
            if (!result.empty()) result += ",";
            result += passItems_[i].pass->getName();
        } else if (passItems_[i].type == PassItemType::LOOP_BEGIN) {
            // Find the matching end
            int depth = 1;
            size_t j = i + 1;
            while (j < end && depth > 0) {
                if (passItems_[j].type == PassItemType::LOOP_BEGIN) {
                    depth++;
                } else if (passItems_[j].type == PassItemType::LOOP_END) {
                    depth--;
                }
                if (depth > 0) j++;
            }

            if (!result.empty()) result += ",";
            result += "(" + generateLoopContent(i + 1, j) + ")";
            if (passItems_[i].maxIterations >= 0) {
                result += "*" + std::to_string(passItems_[i].maxIterations);
            }
            i = j;
        }
    }
    return result;
}

bool PassManager::executeRange(size_t start, size_t end, Module& m) {
    bool changed = false;

    size_t index = start;
    while (index < end) {
        auto& item = passItems_[index];

        if (item.type == PassItemType::PASS) {
            changed |= runPassOnModule(*item.pass, m);
            index++;
        } else if (item.type == PassItemType::LOOP_BEGIN) {
            // Find matching LOOP_END
            size_t loopEnd = index + 1;
            int depth = 1;
            while (loopEnd < end && depth > 0) {
                if (passItems_[loopEnd].type == PassItemType::LOOP_BEGIN) {
                    depth++;
                } else if (passItems_[loopEnd].type == PassItemType::LOOP_END) {
                    depth--;
                }
                if (depth > 0) loopEnd++;
            }

            if (item.loopContent.empty()) {
                item.loopContent = generateLoopContent(index + 1, loopEnd);
            }

            int maxIterations = item.maxIterations;

            if (maxIterations < 0) {
                globalLogger.info() << "Running loop until convergence: ("
                                    << item.loopContent << ")";

                const int MAX_CONVERGENCE_ITERATIONS = 100;
                int iteration = 0;
                bool loopChanged = true;

                while (loopChanged && iteration < MAX_CONVERGENCE_ITERATIONS) {
                    auto indent = globalLogger.indent();
                    globalLogger.info()
                        << "Convergence iteration " << (iteration + 1);

                    {
                        auto indent = globalLogger.indent();

                        bool prevChanged = changed;
                        changed |= executeRange(index + 1, loopEnd, m);
                        loopChanged = (changed != prevChanged);
                    }
                    if (!loopChanged) {
                        globalLogger.info()
                            << "Converged after " << (iteration + 1)
                            << " iteration" << (iteration == 0 ? "" : "s");
                    }
                    iteration++;
                }

                if (iteration >= MAX_CONVERGENCE_ITERATIONS) {
                    globalLogger.warning() << "Loop did not converge after "
                                           << MAX_CONVERGENCE_ITERATIONS
                                           << " iterations, stopping";
                }
            } else if (maxIterations == 0) {
                globalLogger.debug() << "Skipping loop with 0 iterations";
            } else {
                globalLogger.info()
                    << "Running loop up to " << maxIterations << " iteration"
                    << (maxIterations == 1 ? "" : "s") << ": ("
                    << item.loopContent << ")*" << maxIterations;

                for (int iter = 0; iter < maxIterations; ++iter) {
                    auto indent = globalLogger.indent();

                    if (maxIterations > 1) {
                        globalLogger.info() << "Iteration " << (iter + 1) << "/"
                                            << maxIterations;
                    }

                    bool loopChanged = false;
                    {
                        auto indent2 = globalLogger.indent();
                        loopChanged |= executeRange(index + 1, loopEnd, m);
                        changed |= loopChanged;
                    }

                    if (!loopChanged) {
                        globalLogger.info()
                            << "Converged early after " << (iter + 1)
                            << " iteration" << (iter == 0 ? "" : "s")
                            << " (out of " << maxIterations << " max)";
                        break;
                    }
                }
            }

            index = loopEnd + 1;
        } else {
            index++;
        }
    }

    return changed;
}

bool PassManager::run(Module& m) {
    // First, validate loop structure
    int loopDepth = 0;
    for (const auto& item : passItems_) {
        if (item.type == PassItemType::LOOP_BEGIN) {
            loopDepth++;
        } else if (item.type == PassItemType::LOOP_END) {
            loopDepth--;
            if (loopDepth < 0) {
                globalLogger.error() << "Loop end without matching loop begin";
                return false;
            }
        }
    }
    if (loopDepth != 0) {
        globalLogger.error() << "Unclosed loop detected (missing endLoop)";
        return false;
    }

    // Execute all passes
    return executeRange(0, passItems_.size(), m);
}

void PassManager::clear() {
    passItems_.clear();
    analysisManager_.invalidateAll();
}

bool FunctionPassManager::run() {
    if (!function_) return false;
    return run(*function_);
}

bool FunctionPassManager::run(Function& f) {
    bool changed = false;
    for (auto& pass : passes_) {
        changed |= runPassOnFunction(*pass, f);
    }
    return changed;
}

void FunctionPassManager::clear() {
    passes_.clear();
    if (function_) {
        analysisManager_.invalidateAllAnalyses(*function_);
    }
}

// ============================================================================
// PassRegistry Implementation
// ============================================================================

PassRegistry* PassRegistry::instance_ = nullptr;

bool AnalysisManager::computeAnalysis(const std::string& name, Function& f) {
    auto it = analysisRegistry_.find(name);
    if (it == analysisRegistry_.end()) {
        globalLogger.error() << "Analysis " << name << " not registered.";
        return false;
    }
    globalLogger.debug() << "Computing analysis: " << name << " for function "
                         << f.getName();

    auto analysis = it->second();
    if (!analysis || !analysis->supportsFunction()) {
        return false;
    }

    for (const auto& dep : analysis->getDependencies()) {
        if (!getAnalysis<AnalysisResult>(dep, f)) {
            if (!computeAnalysis(dep, f)) {
                return false;
            }
        }
    }

    auto result = analysis->runOnFunction(f, *this);
    if (!result) {
        return false;
    }

    functionAnalyses_[&f][name] = std::move(result);
    return true;
}

bool AnalysisManager::computeAnalysis(const std::string& name, Module& m) {
    auto it = analysisRegistry_.find(name);
    if (it == analysisRegistry_.end()) {
        return false;
    }

    auto analysis = it->second();
    if (!analysis || !analysis->supportsModule()) {
        return false;
    }

    for (const auto& dep : analysis->getDependencies()) {
        if (!getAnalysis<AnalysisResult>(dep, m)) {
            if (!computeAnalysis(dep, m)) {
                return false;
            }
        }
    }

    auto result = analysis->runOnModule(m, *this);
    if (!result) {
        return false;
    }

    moduleAnalyses_[name] = std::move(result);
    return true;
}

std::vector<std::string> AnalysisManager::getRegisteredAnalyses(
    Function& f) const {
    std::vector<std::string> analyses;
    auto it = functionAnalyses_.find(&f);
    if (it != functionAnalyses_.end()) {
        for (const auto& pair : it->second) {
            analyses.push_back(pair.first);
        }
    }
    return analyses;
}

std::vector<std::string> AnalysisManager::getRegisteredAnalyses(Module&) const {
    std::vector<std::string> analyses;
    for (const auto& pair : moduleAnalyses_) {
        analyses.push_back(pair.first);
    }
    return analyses;
}

std::vector<std::string> AnalysisManager::getRegisteredAnalysisTypes() const {
    std::vector<std::string> types;
    for (const auto& pair : analysisRegistry_) {
        types.push_back(pair.first);
    }
    return types;
}

std::vector<std::string> PassRegistry::getRegisteredPasses() const {
    std::vector<std::string> names;
    names.reserve(registry_.size());
    for (const auto& [name, _] : registry_) {
        names.push_back(name);
    }
    return names;
}

}  // namespace midend
