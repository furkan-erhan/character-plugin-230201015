#pragma once

#include <plugin/IPlugin.h>
#include <string>
#include <vector>

namespace arkheon::astsim {
class ModelFactoryRegistry;
}

namespace student::charanim {

class StudentCharAnimPlugin final : public arkheon::astlib::IPlugin {
public:
    StudentCharAnimPlugin() = default;
    ~StudentCharAnimPlugin() override = default;

    [[nodiscard]] int getInterfaceVersion() const override;
    [[nodiscard]] arkheon::astlib::PluginMetadata getMetadata() const override;

    void initialize(arkheon::astlib::PluginContext& context) override;
    void tick(double dt) override;
    void shutdown() override;

private:
    bool initialized_ = false;
    bool shutdown_ = false;
    bool animationRegistered_ = false;
    std::string modelType_ = "animationModelNathanHuman";
    arkheon::astsim::ModelFactoryRegistry* modelFactoryRegistry_ = nullptr;
    std::vector<std::string> registeredCodes_;
};

} // namespace student::charanim

extern "C" {
__declspec(dllexport) arkheon::astlib::IPlugin* create_plugin();
__declspec(dllexport) void destroy_plugin(arkheon::astlib::IPlugin* plugin);
__declspec(dllexport) const char* get_plugin_signature();
}
