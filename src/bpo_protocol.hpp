#pragma once
#include "serial_port.hpp"
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PwmChannelData {
    uint16_t freq_hz = 0;
    float    duty_pct = 0.0f;
};

struct BpoTelemetry {
    bool connected = false;
    bool test_mode = false;
    std::string fw_info = "—";
    std::string flash_status = "Не проверено";
    bool flash_ok = false;

    uint16_t inputs_mask = 0; // Биты 0..7: KB1..KB8, бит 8: EMRG, бит 9: SG
    std::array<PwmChannelData, 4> pwm{};
    uint16_t adc_ain1 = 0;
    uint16_t adc_esen = 0;

    // Состояние выходов и UI (зеркало отправленных команд)
    std::array<uint8_t, 5> ui_cols{0, 0, 0, 0, 0};
    uint8_t ui_brightness = 100;
    std::array<uint8_t, 4> out_pwm{0, 0, 0, 0};
};

// Кольцевой буфер для графиков ImPlot
struct ScrollingBuffer {
    int max_size = 500;
    int offset = 0;
    std::vector<float> x;
    std::vector<float> y;

    ScrollingBuffer(int max_sz = 500) : max_size(max_sz) {
        x.reserve(max_sz);
        y.reserve(max_sz);
    }

    void AddPoint(float time_s, float val) {
        if (static_cast<int>(x.size()) < max_size) {
            x.push_back(time_s);
            y.push_back(val);
        } else {
            x[offset] = time_s;
            y[offset] = val;
            offset = (offset + 1) % max_size;
        }
    }

    void Clear() {
        x.clear();
        y.clear();
        offset = 0;
    }
};

class BpoClient {
public:
    BpoClient();
    ~BpoClient();

    bool Connect(const std::string& port);
    void Disconnect();
    bool IsConnected() const { return running_.load(); }

    void SendCommand(const std::string& cmd);
    void SetTestMode(bool enable);
    void SetColumnMask(int col_1_to_5, uint8_t mask);
    void SetUiAll(bool on);
    void SetUiBrightness(uint8_t pct);
    void SetValveOutput(int ch_1_to_4, uint8_t pct);

    BpoTelemetry GetTelemetry();
    std::vector<std::string> GetLogLines();
    void ClearLog();

    // Доступ к буферам графиков (под мьютексом)
    std::mutex data_mutex;
    ScrollingBuffer hist_ain1{600};
    ScrollingBuffer hist_esen{600};
    std::array<ScrollingBuffer, 4> hist_pwm_duty{600, 600, 600, 600};

private:
    void WorkerLoop();
    void ParseResponse(const std::string& cmd, const std::string& resp);
    void AppendLog(const std::string& line);

    SerialPort serial_;
    std::thread worker_;
    std::atomic<bool> running_{false};

    std::mutex cmd_mutex_;
    std::deque<std::string> cmd_queue_;

    BpoTelemetry telemetry_;
    std::deque<std::string> log_lines_;
    double start_time_ = 0.0;
};