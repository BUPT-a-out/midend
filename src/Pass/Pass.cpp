#include "Pass/Pass.h"

#include <iostream>
#include <sstream>

#include "IR/BasicBlock.h"
#include "IR/Function.h"

namespace midend {

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
    std::unordered_set<std::string> required, preserved;
    pass.getAnalysisUsage(required, preserved);

    for (const auto& requiredAnalysis : required) {
        if (analysisManager_.getAnalysis<AnalysisResult>(requiredAnalysis, m) ==
            nullptr) {
            std::cerr << "Warning: Required analysis " << requiredAnalysis
                      << " not found for module " << m.getName() << std::endl;
            return false;
        }
    }

    bool changed = false;
    switch (pass.getKind()) {
        case Pass::PassKind::ModulePass:
        case Pass::PassKind::FunctionPass:
            changed = pass.runOnModule(m, analysisManager_);
            break;
        case Pass::PassKind::BasicBlockPass:
            for (auto& fn : m) {
                changed |= pass.runOnFunction(*fn, analysisManager_);
            }
            break;
    }

    if (changed) {
        analysisManager_.invalidateAllAnalyses(m);
    }

    return changed;
}

bool FunctionPassManager::runPassOnFunction(Pass& pass, Function& f) {
    std::unordered_set<std::string> required, preserved;
    pass.getAnalysisUsage(required, preserved);

    for (const auto& requiredAnalysis : required) {
        if (analysisManager_.getAnalysis<AnalysisResult>(requiredAnalysis, f) ==
            nullptr) {
            std::cerr << "Warning: Required analysis " << requiredAnalysis
                      << " not found for function " << f.getName() << std::endl;
            return false;
        }
    }

    bool changed = false;
    switch (pass.getKind()) {
        case Pass::PassKind::FunctionPass:
        case Pass::PassKind::BasicBlockPass:
            changed = pass.runOnFunction(f, analysisManager_);
            break;
        default:
            std::cerr << "Warning: Module passes cannot be run in function "
                         "pass manager"
                      << std::endl;
            break;
    }

    if (changed) {
        auto registeredAnalyses = analysisManager_.getRegisteredAnalyses(f);
        for (const auto& analysisName : registeredAnalyses) {
            if (preserved.find(analysisName) == preserved.end()) {
                analysisManager_.invalidateAnalysis(analysisName, f);
            }
        }
    }

    return changed;
}

bool PassBuilder::parsePassPipeline(PassManager& pm,
                                    const std::string& pipeline) {
    std::istringstream iss(pipeline);
    std::string passName;

    while (std::getline(iss, passName, ',')) {
        passName.erase(0, passName.find_first_not_of(" \t"));
        passName.erase(passName.find_last_not_of(" \t") + 1);

        auto pass = createPass(passName);
        if (!pass) {
            return false;
        }
        pm.addPass(std::move(pass));
    }

    return true;
}

bool PassBuilder::parsePassPipeline(FunctionPassManager& fpm,
                                    const std::string& pipeline) {
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

bool PassManager::run(Module& m) {
    bool changed = false;
    for (auto& pass : passes_) {
        changed |= runPassOnModule(*pass, m);
    }
    return changed;
}

void PassManager::clear() {
    passes_.clear();
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
        return false;
    }

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

    auto result = analysis->runOnFunction(f);
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

    auto result = analysis->runOnModule(m);
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