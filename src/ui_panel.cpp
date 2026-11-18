#include "ui_panel.hpp"
#include "imgui.h"
#include "implot.h"
#include <cstdio>

UiPanel::UiPanel() {
    ports_ = SerialPort::ListAvailablePorts();
}

void UiPanel::Render(BpoClient& client) {
    BpoTelemetry tel = client.GetTelemetry();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("BPO_GNS_Main", nullptr, flags);

    RenderConnectionBar(client, tel);
    ImGui::Separator();

    // 3 колонки рабочего пространства
    float avail_w = ImGui::GetContentRegionAvail().x;
    float col1_w = avail_w * 0.34f;
    float col2_w = avail_w * 0.28f;

    ImGui::BeginChild("ColMatrix", ImVec2(col1_w, -180.0f), true);
    RenderDisplayMatrixPanel(client, tel);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ColOutputs", ImVec2(col2_w, -180.0f), true);
    RenderOutputsAndPeripheralsPanel(client, tel);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ColTelemetry", ImVec2(0.0f, -180.0f), true);
    RenderInputsAndTelemetryPanel(client, tel);
    ImGui::EndChild();

    // Нижняя консоль UART
    ImGui::BeginChild("ConsolePane", ImVec2(0.0f, 0.0f), true);
    RenderConsoleLog(client);
    ImGui::EndChild();

    ImGui::End();
}

void UiPanel::RenderConnectionBar(BpoClient& client, const BpoTelemetry& tel) {
    ImGui::Text("Порт:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);

    const char* preview = ports_.empty() ? "Порты не найдены" : ports_[selected_port_idx_].c_str();
    ImGui::BeginDisabled(tel.connected);
    if (ImGui::BeginCombo("##PortCombo", preview)) {
        for (int i = 0; i < static_cast<int>(ports_.size()); ++i) {
            bool is_sel = (selected_port_idx_ == i);
            if (ImGui::Selectable(ports_[i].c_str(), is_sel)) selected_port_idx_ = i;
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Обновить")) {
        ports_ = SerialPort::ListAvailablePorts();
        selected_port_idx_ = 0;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (!tel.connected) {
        ImGui::BeginDisabled(ports_.empty());
        if (ImGui::Button("Подключить", ImVec2(110, 0))) {
            client.Connect(ports_[selected_port_idx_]);
        }
        ImGui::EndDisabled();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button("Отключить", ImVec2(110, 0))) {
            client.Disconnect();
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::BeginDisabled(!tel.connected);
    if (!tel.test_mode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.25f, 1.0f));
        if (ImGui::Button("ВОЙТИ В TEST_MODE", ImVec2(165, 0))) {
            client.SetTestMode(true);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.10f, 1.0f));
        if (ImGui::Button("ВЫЙТИ ИЗ TEST_MODE", ImVec2(165, 0))) {
            client.SetTestMode(false);
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextColored(tel.connected ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       tel.connected ? "ONLINE" : "OFFLINE");
    ImGui::SameLine();
    ImGui::Text("| MCU: %s", tel.fw_info.c_str());
}

void UiPanel::RenderDisplayMatrixPanel(BpoClient& client, const BpoTelemetry& tel) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "МАТРИЦА ИНДИКАЦИИ (5x8)");
    ImGui::Separator();

    ImGui::BeginDisabled(!tel.test_mode);

    // Быстрые кнопки управления всей матрицей
    if (ImGui::Button("ВСЕ ВКЛ (ALL)", ImVec2(120, 0))) client.SetUiAll(true);
    ImGui::SameLine();
    if (ImGui::Button("ВСЕ ВЫКЛ (OFF)", ImVec2(120, 0))) client.SetUiAll(false);

    // Слайдер яркости
    int bright = tel.ui_brightness;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##Bright", &bright, 0, 100, "Яркость матрицы: %d%%")) {
        float now = static_cast<float>(ImGui::GetTime());
        if (now - last_bright_send_time_ > 0.03f || bright == 0 || bright == 100) {
            client.SetUiBrightness(static_cast<uint8_t>(bright));
            last_bright_send_time_ = now;
        }
    }

    ImGui::Spacing();
    ImGui::Text("Побитовое управление столбцами (клик по ячейке):");

    const char* seg_names[8] = {"Bit 0 (A)", "Bit 1 (B)", "Bit 2 (C)", "Bit 3 (D)",
                                "Bit 4 (E)", "Bit 5 (F)", "Bit 6 (G)", "Bit 7 (DP)"};

    if (ImGui::BeginTable("MatrixTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Бит / Сегмент", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        for (int c = 1; c <= 5; ++c) {
            char hdr[16];
            std::snprintf(hdr, sizeof(hdr), "COL %d", c);
            ImGui::TableSetupColumn(hdr, ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableHeadersRow();

        for (int bit = 0; bit < 8; ++bit) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(seg_names[bit]);

            for (int col = 0; col < 5; ++col) {
                ImGui::TableSetColumnIndex(col + 1);
                bool active = (tel.ui_cols[col] & (1u << bit)) != 0;

                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.25f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.35f, 0.25f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.33f, 0.38f, 1.0f));
                }

                char btn_id[32];
                std::snprintf(btn_id, sizeof(btn_id), "%s##c%db%d", active ? "1" : "0", col, bit);
                if (ImGui::Button(btn_id, ImVec2(-1.0f, 24.0f))) {
                    uint8_t new_mask = tel.ui_cols[col] ^ static_cast<uint8_t>(1u << bit);
                    client.SetColumnMask(col + 1, new_mask);
                }
                ImGui::PopStyleColor(2);
            }
        }

        // Строка с HEX-масками и кнопками сброса столбца
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("HEX маска");
        for (int col = 0; col < 5; ++col) {
            ImGui::TableSetColumnIndex(col + 1);
            char hex_btn[24];
            std::snprintf(hex_btn, sizeof(hex_btn), "0x%02X##clr%d", tel.ui_cols[col], col);
            if (ImGui::Button(hex_btn, ImVec2(-1.0f, 0.0f))) {
                // Клик по HEX-кнопке инвертирует столбец (0x00 <-> 0xFF)
                client.SetColumnMask(col + 1, tel.ui_cols[col] == 0 ? 0xFF : 0x00);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Клик: заполнить 0xFF / очистить 0x00");
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndDisabled();
}

void UiPanel::RenderOutputsAndPeripheralsPanel(BpoClient& client, const BpoTelemetry& tel) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ВЫХОДЫ КЛАПАНОВ И ПЕРИФЕРИЯ");
    ImGui::Separator();

    bool emrg_active = ((tel.inputs_mask >> 8) & 1u) == 0; // Активный 0 на входе EMRG
    if (emrg_active && tel.connected) {
        ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "ВНИМАНИЕ: СТОП (EMRG) АКТИВЕН!");
    }

    ImGui::BeginDisabled(!tel.test_mode);

    float now = static_cast<float>(ImGui::GetTime());
    for (int i = 0; i < 4; ++i) {
        int val = tel.out_pwm[i];
        char label[32];
        std::snprintf(label, sizeof(label), "Канал OUT %d (ШИМ %%)", i + 1);
        ImGui::TextUnformatted(label);

        char slider_id[32];
        std::snprintf(slider_id, sizeof(slider_id), "##out_%d", i);
        ImGui::SetNextItemWidth(-70.0f);
        if (ImGui::SliderInt(slider_id, &val, 0, 100, "%d%%")) {
            if (now - last_out_send_time_[i] > 0.03f || val == 0 || val == 100) {
                client.SetValveOutput(i + 1, static_cast<uint8_t>(val));
                last_out_send_time_[i] = now;
            }
        }
        ImGui::SameLine();
        char stop_id[32];
        std::snprintf(stop_id, sizeof(stop_id), "0%%##stop_%d", i);
        if (ImGui::Button(stop_id, ImVec2(-1.0f, 0.0f))) {
            client.SetValveOutput(i + 1, 0);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Звук и подсветка:");

    if (ImGui::Button("Зуммер 1x (BUZZ)", ImVec2(-1.0f, 0.0f))) client.SendCommand("SET_UI BUZZ");
    if (ImGui::Button("Зуммер 2x (BUZZ2)", ImVec2(-1.0f, 0.0f))) client.SendCommand("SET_UI BUZZ2");
    if (ImGui::Button("Подсветка кнопок 50%", ImVec2(-1.0f, 0.0f))) client.SendCommand("SET_UI LIGHT");

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Внешняя Flash W25Q32 (SPI1):");
    ImGui::BeginDisabled(!tel.connected);
    if (ImGui::Button("Проверить JEDEC ID", ImVec2(-1.0f, 0.0f))) {
        client.SendCommand("TEST_FLASH");
    }
    ImGui::EndDisabled();
    ImGui::TextColored(tel.flash_ok ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                       "Статус: %s", tel.flash_status.c_str());
}

void UiPanel::RenderInputsAndTelemetryPanel(BpoClient& client, const BpoTelemetry& tel) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ДИСКРЕТНЫЕ ВХОДЫ И ТЕЛЕМЕТРИЯ");
    ImGui::Separator();

    // 1. Дискретные входы (маска 0x3FF)
    const char* in_names[10] = {"KB1", "KB2", "KB3", "KB4", "KB5", "KB6", "KB7", "KB8", "EMRG", "SG"};
    ImGui::Text("Маска входов: 0x%03X", tel.inputs_mask);

    for (int i = 0; i < 10; ++i) {
        if (i > 0 && i != 5) ImGui::SameLine();
        bool high = ((tel.inputs_mask >> i) & 1u) != 0;
        ImVec4 col = high ? ImVec4(0.2f, 0.75f, 0.35f, 1.0f) : ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::Button(in_names[i], ImVec2(54.0f, 26.0f));
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // 2. ШИМ входы джойстика
    ImGui::Text("ШИМ джойстика (TIM1 CH1..4):");
    if (ImGui::BeginTable("PwmTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Канал");
        ImGui::TableSetupColumn("Частота (Гц)");
        ImGui::TableSetupColumn("Скважность (%)");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("PWM %d", i + 1);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%u Гц", tel.pwm[i].freq_hz);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f %%", tel.pwm[i].duty_pct);
        }
        ImGui::EndTable();
    }

    // 3. График АЦП в реальном времени (ImPlot)
    ImGui::Spacing();
    ImGui::Text("АЦП: AIN1 = %u | ESEN = %u", tel.adc_ain1, tel.adc_esen);

    std::lock_guard<std::mutex> lk(client.data_mutex);
    if (ImPlot::BeginPlot("ADC1 (AIN1 / ESEN)", ImVec2(-1.0f, -1.0f), ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes("Время (с)", "RAW (0..4095)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 4150.0, ImGuiCond_Once);

        if (!client.hist_ain1.x.empty()) {
            ImPlot::PlotLine("AIN1", client.hist_ain1.x.data(), client.hist_ain1.y.data(),
                             static_cast<int>(client.hist_ain1.x.size()), 0, client.hist_ain1.offset);
        }
        if (!client.hist_esen.x.empty()) {
            ImPlot::PlotLine("ESEN", client.hist_esen.x.data(), client.hist_esen.y.data(),
                             static_cast<int>(client.hist_esen.x.size()), 0, client.hist_esen.offset);
        }
        ImPlot::EndPlot();
    }
}

void UiPanel::RenderConsoleLog(BpoClient& client) {
    ImGui::Text("Консоль UART:");
    ImGui::SameLine();
    ImGui::BeginDisabled(!client.IsConnected());
    ImGui::SetNextItemWidth(320.0f);
    bool enter = ImGui::InputText("##ManualCmd", manual_cmd_, sizeof(manual_cmd_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Отправить") || enter) && manual_cmd_[0] != '\0') {
        client.SendCommand(manual_cmd_);
        manual_cmd_[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("HELP")) client.SendCommand("HELP");
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Очистить лог")) client.ClearLog();

    ImGui::Separator();
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    auto lines = client.GetLogLines();
    for (const auto& l : lines) {
        if (l.rfind("< ERR", 0) == 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", l.c_str());
        } else if (l.rfind("> ", 0) == 0) {
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "%s", l.c_str());
        } else {
            ImGui::TextUnformatted(l.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}