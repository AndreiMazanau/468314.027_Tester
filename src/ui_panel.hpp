#pragma once
#include "bpo_protocol.hpp"
#include <string>
#include <vector>

class UiPanel {
public:
    UiPanel();
    void Render(BpoClient& client);

private:
    void RenderConnectionBar(BpoClient& client, const BpoTelemetry& tel);
    void RenderDisplayMatrixPanel(BpoClient& client, const BpoTelemetry& tel);
    void RenderFrontPanelPreview(const BpoTelemetry& tel);
    void RenderOutputsAndPeripheralsPanel(BpoClient& client, const BpoTelemetry& tel);
    void RenderInputsAndTelemetryPanel(BpoClient& client, const BpoTelemetry& tel);
    void RenderConsoleLog(BpoClient& client);

    std::vector<std::string> ports_;
    int selected_port_idx_ = 0;
    char manual_cmd_[128]{};
    float last_out_send_time_[4]{};
    float last_bright_send_time_ = 0.0f;
    float last_light_send_time_ = 0.0f;
};