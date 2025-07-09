#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/Module.h"

namespace midend {

class Module;
class Function;
class BasicBlock;
class AnalysisManager;

class AnalysisResult {
   public:
    virtual ~AnalysisResult() = default;
};

class Analysis {
   public:
    virtual ~Analysis() = default;

    virtual const std::string& getName() const = 0;

    virtual std::unique_ptr<AnalysisResult> runOnFunction(Function& f) {
        (void)f;
        return nullptr;
    }

    virtual std::unique_ptr<AnalysisResult> runOnModule(Module& m) {
        (void)m;
        return nullptr;
    }

    virtual bool supportsFunction() const { return false; }

    virtual bool supportsModule() const { return false; }

    virtual std::vector<std::string> getDependencies() const { return {}; }
};

template <typename DerivedT>
class AnalysisBase : public Analysis {
   private:
    std::string name_;

   public:
    explicit AnalysisBase(const std::string& name) : name_(name) {}

    const std::string& getName() const override { return name_; }
};

class AnalysisManager {
   public:
    using AnalysisFactory = std::function<std::unique_ptr<Analysis>()>;

   private:
    std::unordered_map<std::string, std::unique_ptr<AnalysisResult>>
        moduleAnalyses_;
    std::unordered_map<
        Function*,
        std::unordered_map<std::string, std::unique_ptr<AnalysisResult>>>
        functionAnalyses_;
    std::unordered_map<std::string, AnalysisFactory> analysisRegistry_;

   public:
    template <typename AnalysisT>
    void registerAnalysisType() {
        auto analysis = std::make_unique<AnalysisT>();
        std::string name = analysis->getName();
        analysisRegistry_[name] = []() {
            return std::make_unique<AnalysisT>();
        };
    }

    void registerAnalysisType(const std::string& name,
                              AnalysisFactory factory) {
        analysisRegistry_[name] = std::move(factory);
    }

    template <typename AnalysisT>
    void registerAnalysis(const std::string& name, Module&,
                          std::unique_ptr<AnalysisT> result) {
        moduleAnalyses_[name] = std::move(result);
    }

    template <typename AnalysisT>
    void registerAnalysis(const std::string& name, Function& f,
                          std::unique_ptr<AnalysisT> result) {
        functionAnalyses_[&f][name] = std::move(result);
    }

    template <typename AnalysisT>
    AnalysisT* getAnalysis(const std::string& name, Module& m) {
        auto it = moduleAnalyses_.find(name);
        if (it != moduleAnalyses_.end()) {
            return dynamic_cast<AnalysisT*>(it->second.get());
        }

        if (computeAnalysis(name, m)) {
            it = moduleAnalyses_.find(name);
            if (it != moduleAnalyses_.end()) {
                return dynamic_cast<AnalysisT*>(it->second.get());
            }
        }

        return nullptr;
    }

    template <typename AnalysisT>
    AnalysisT* getAnalysis(const std::string& name, Function& f) {
        auto it = functionAnalyses_.find(&f);
        if (it != functionAnalyses_.end()) {
            auto analysisIt = it->second.find(name);
            if (analysisIt != it->second.end()) {
                return dynamic_cast<AnalysisT*>(analysisIt->second.get());
            }
        }

        if (computeAnalysis(name, f)) {
            it = functionAnalyses_.find(&f);
            if (it != functionAnalyses_.end()) {
                auto analysisIt = it->second.find(name);
                if (analysisIt != it->second.end()) {
                    return dynamic_cast<AnalysisT*>(analysisIt->second.get());
                }
            }
        }

        return nullptr;
    }

    void invalidateAnalysis(const std::string& name, Module&) {
        moduleAnalyses_.erase(name);
    }

    void invalidateAnalysis(const std::string& name, Function& f) {
        auto it = functionAnalyses_.find(&f);
        if (it != functionAnalyses_.end()) {
            it->second.erase(name);
        }
    }

    void invalidateAllAnalyses(Module&) { moduleAnalyses_.clear(); }

    void invalidateAllAnalyses(Function& f) { functionAnalyses_.erase(&f); }

    void invalidateAll() {
        moduleAnalyses_.clear();
        functionAnalyses_.clear();
    }

    std::vector<std::string> getRegisteredAnalyses(Function& f) const;

    std::vector<std::string> getRegisteredAnalyses(Module&) const;

    bool isAnalysisRegistered(const std::string& name) const {
        return analysisRegistry_.find(name) != analysisRegistry_.end();
    }

    std::vector<std::string> getRegisteredAnalysisTypes() const;

    bool computeAnalysis(const std::string& name, Function& f);

    bool computeAnalysis(const std::string& name, Module& m);
};

class PassInfo {
   private:
    std::string name_;
    std::string description_;

   public:
    PassInfo(const std::string& name, const std::string& desc)
        : name_(name), description_(desc) {}

    const std::string& getName() const { return name_; }
    const std::string& getDescription() const { return description_; }
};

class Pass {
   public:
    enum PassKind {
        ModulePass,
        FunctionPass,
        BasicBlockPass,
    };

   private:
    PassKind kind_;
    PassInfo info_;

   protected:
    Pass(PassKind kind, const std::string& name, const std::string& desc = "")
        : kind_(kind), info_(name, desc) {}

   public:
    virtual ~Pass() = default;

    PassKind getKind() const { return kind_; }
    const PassInfo& getPassInfo() const { return info_; }
    const std::string& getName() const { return info_.getName(); }

    virtual void getAnalysisUsage(std::unordered_set<std::string>&,
                                  std::unordered_set<std::string>&) const {}

    virtual bool runOnModule(Module&, AnalysisManager&) { return false; }
    virtual bool runOnFunction(Function&, AnalysisManager&) { return false; }
    virtual bool runOnBasicBlock(BasicBlock&, AnalysisManager&) {
        return false;
    }
};

class ModulePass : public Pass {
   protected:
    ModulePass(const std::string& name, const std::string& desc = "")
        : Pass(PassKind::ModulePass, name, desc) {}

   public:
    virtual bool runOnModule(Module& m, AnalysisManager& am) override = 0;

    static bool classof(const Pass* p) {
        return p->getKind() == PassKind::ModulePass;
    }
};

class FunctionPass : public Pass {
   protected:
    FunctionPass(const std::string& name, const std::string& desc = "")
        : Pass(PassKind::FunctionPass, name, desc) {}

   public:
    virtual bool runOnFunction(Function& f, AnalysisManager& am) override = 0;

    bool runOnModule(Module& m, AnalysisManager& am) override;

    static bool classof(const Pass* p) {
        return p->getKind() == PassKind::FunctionPass;
    }
};

class BasicBlockPass : public Pass {
   protected:
    BasicBlockPass(const std::string& name, const std::string& desc = "")
        : Pass(PassKind::BasicBlockPass, name, desc) {}

   public:
    virtual bool runOnBasicBlock(BasicBlock& bb,
                                 AnalysisManager& am) override = 0;

    bool runOnFunction(Function& f, AnalysisManager& am) override;

    static bool classof(const Pass* p) {
        return p->getKind() == PassKind::BasicBlockPass;
    }
};

template <typename DerivedT>
class PassBase : public Pass {
   public:
    static std::unique_ptr<Pass> create() {
        return std::make_unique<DerivedT>();
    }
};

class PassManager {
   private:
    std::vector<std::unique_ptr<Pass>> passes_;
    AnalysisManager analysisManager_;

    bool runPassOnModule(Pass& pass, Module& m);

   public:
    PassManager() = default;

    template <typename PassT, typename... Args>
    void addPass(Args&&... args) {
        passes_.push_back(std::make_unique<PassT>(std::forward<Args>(args)...));
    }

    void addPass(std::unique_ptr<Pass> pass) {
        passes_.push_back(std::move(pass));
    }

    bool run(Module& m);

    AnalysisManager& getAnalysisManager() { return analysisManager_; }
    const AnalysisManager& getAnalysisManager() const {
        return analysisManager_;
    }

    size_t getNumPasses() const { return passes_.size(); }

    void clear();
};

class FunctionPassManager {
   private:
    Function* function_;
    std::vector<std::unique_ptr<Pass>> passes_;
    AnalysisManager analysisManager_;

    bool runPassOnFunction(Pass& pass, Function& f);

   public:
    explicit FunctionPassManager(Function* f) : function_(f) {}

    template <typename PassT, typename... Args>
    void addPass(Args&&... args) {
        passes_.push_back(std::make_unique<PassT>(std::forward<Args>(args)...));
    }

    void addPass(std::unique_ptr<Pass> pass) {
        passes_.push_back(std::move(pass));
    }

    bool run();
    bool run(Function& f);

    AnalysisManager& getAnalysisManager() { return analysisManager_; }
    const AnalysisManager& getAnalysisManager() const {
        return analysisManager_;
    }

    size_t getNumPasses() const { return passes_.size(); }

    void clear();
};

class PassBuilder {
   public:
    using PassFactory = std::function<std::unique_ptr<Pass>()>;

   private:
    std::unordered_map<std::string, PassFactory> passRegistry_;

   public:
    void registerPass(const std::string& name, PassFactory factory) {
        passRegistry_[name] = std::move(factory);
    }

    template <typename PassT>
    void registerPass(const std::string& name) {
        registerPass(name, []() { return std::make_unique<PassT>(); });
    }

    std::unique_ptr<Pass> createPass(const std::string& name) const {
        auto it = passRegistry_.find(name);
        if (it != passRegistry_.end()) {
            return it->second();
        }
        return nullptr;
    }

    bool parsePassPipeline(PassManager& pm, const std::string& pipeline);
    bool parsePassPipeline(FunctionPassManager& fpm,
                           const std::string& pipeline);
};

class PassRegistry {
   public:
    using PassConstructor = std::function<std::unique_ptr<Pass>()>;

   private:
    static PassRegistry* instance_;  // = nullptr
    std::unordered_map<std::string, PassConstructor> registry_;

    PassRegistry() = default;

   public:
    static PassRegistry& getInstance() {
        if (!instance_) {
            instance_ = new PassRegistry();
        }
        return *instance_;
    }

    void registerPass(const std::string& name, PassConstructor ctor) {
        registry_[name] = std::move(ctor);
    }

    template <typename PassT>
    void registerPass(const std::string& name) {
        registerPass(name, []() { return std::make_unique<PassT>(); });
    }

    std::unique_ptr<Pass> createPass(const std::string& name) const {
        auto it = registry_.find(name);
        if (it != registry_.end()) {
            return it->second();
        }
        return nullptr;
    }

    bool hasPass(const std::string& name) const {
        return registry_.find(name) != registry_.end();
    }

    std::vector<std::string> getRegisteredPasses() const;
};

template <typename PassT>
class RegisterPass {
   public:
    RegisterPass(const std::string& name) {
        PassRegistry::getInstance().registerPass<PassT>(name);
    }
};

#define REGISTER_PASS(PassClass, Name) \
    static RegisterPass<PassClass> PassClass##Registration(Name);

}  // namespace midend