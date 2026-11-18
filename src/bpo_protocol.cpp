#include "bpo_protocol.hpp"
#include <chrono>
#include <cstdio>
#include <sstream>

static double NowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

BpoClient::BpoClient() = default;
BpoClient::~BpoClient() { Disconnect(); }

bool BpoClient::Connect(const std::string& port) {
    Disconnect();
    if (!serial_.Open(port, 115200)) return false;

    {
        std::lock_guard<std::mutex> lk(data_mutex);
        telemetry_ = BpoTelemetry{};
        telemetry_.connected = true;
        hist_ain1.Clear();
        hist_esen.Clear();
        for (auto& h : hist_pwm_duty) h.Clear();
    }
    start_time_ = NowSec();
    running_ = true;
    worker_ = std::thread(&BpoClient::WorkerLoop, this);

    // Сразу запрашиваем PING и INFO
    SendCommand("PING");
    SendCommand("INFO");
    return true;
}

void BpoClient::Disconnect() {
    if (running_.exchange(false)) {
        if (worker_.joinable()) worker_.join();
    }
    if (serial_.IsOpen()) {
        serial_.WriteLine("TEST_MODE 0");
        serial_.Close();
    }
    std::lock_guard<std::mutex> lk(data_mutex);
    telemetry_.connected = false;
    telemetry_.test_mode = false;
}

void BpoClient::SendCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lk(cmd_mutex_);
    // Ограничим очередь на случай слишком частых движений слайдера
    if (cmd_queue_.size() > 32) cmd_queue_.pop_front();
    cmd_queue_.push_back(cmd);
}

void BpoClient::SetTestMode(bool enable) {
    SendCommand(enable ? "TEST_MODE 1" : "TEST_MODE 0");
}

void BpoClient::SetColumnMask(int col_1_to_5, uint8_t mask) {
    if (col_1_to_5 < 1 || col_1_to_5 > 5) return;
    {
        std::lock_guard<std::mutex> lk(data_mutex);
        telemetry_.ui_cols[col_1_to_5 - 1] = mask;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "SET_UI %d 0x%02X", col_1_to_5, mask);
    SendCommand(buf);
}

void BpoClient::SetUiAll(bool on) {
    {
        std::lock_guard<std::mutex> lk(data_mutex);
        telemetry_.ui_cols.fill(on ? 0xFF : 0x00);
    }
    SendCommand(on ? "SET_UI ALL" : "SET_UI OFF");
}

void BpoClient::SetUiBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    {
        std::lock_guard<std::mutex> lk(data_mutex);
        telemetry_.ui_brightness = pct;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "SET_UI BRIGHT %u", pct);
    SendCommand(buf);
}

void BpoClient::SetValveOutput(int ch_1_to_4, uint8_t pct) {
    if (ch_1_to_4 < 1 || ch_1_to_4 > 4) return;
    if (pct > 100) pct = 100;
    {
        std::lock_guard<std::mutex> lk(data_mutex);
        telemetry_.out_pwm[ch_1_to_4 - 1] = pct;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "SET_OUT %d %u", ch_1_to_4, pct);
    SendCommand(buf);
}

BpoTelemetry BpoClient::GetTelemetry() {
    std::lock_guard<std::mutex> lk(data_mutex);
    return telemetry_;
}

std::vector<std::string> BpoClient::GetLogLines() {
    std::lock_guard<std::mutex> lk(data_mutex);
    return {log_lines_.begin(), log_lines_.end()};
}

void BpoClient::ClearLog() {
    std::lock_guard<std::mutex> lk(data_mutex);
    log_lines_.clear();
}

void BpoClient::AppendLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(data_mutex);
    if (log_lines_.size() >= 200) log_lines_.pop_front();
    log_lines_.push_back(line);
}

void BpoClient::WorkerLoop() {
    int poll_step = 0;
    const char* poll_cmds[3] = {"GET_IN", "GET_PWM", "GET_ADC"};

    while (running_.load()) {
        std::string cmd;
        bool is_user_cmd = false;
        {
            std::lock_guard<std::mutex> lk(cmd_mutex_);
            if (!cmd_queue_.empty()) {
                cmd = cmd_queue_.front();
                cmd_queue_.pop_front();
                is_user_cmd = true;
            }
        }

        if (!is_user_cmd) {
            cmd = poll_cmds[poll_step];
            poll_step = (poll_step + 1) % 3;
        }

        if (is_user_cmd) AppendLog("> " + cmd);

        if (serial_.WriteLine(cmd)) {
            std::string resp;
            if (serial_.ReadLine(resp, 60)) {
                if (is_user_cmd || resp.rfind("ERR", 0) == 0) {
                    AppendLog("< " + resp);
                }
                ParseResponse(cmd, resp);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(is_user_cmd ? 5 : 20));
    }
}

void BpoClient::ParseResponse(const std::string& cmd, const std::string& resp) {
    std::lock_guard<std::mutex> lk(data_mutex);
    float t = static_cast<float>(NowSec() - start_time_);

    if (resp.rfind("OK INFO", 0) == 0 || resp.rfind("OK VER=", 0) == 0) {
        telemetry_.fw_info = resp.substr(3);
    }
    else if (resp == "OK MODE=1") {
        telemetry_.test_mode = true;
        telemetry_.ui_cols.fill(0x00);
        telemetry_.ui_brightness = 100;
        telemetry_.out_pwm.fill(0);
    }
    else if (resp == "OK MODE=0") {
        telemetry_.test_mode = false;
        telemetry_.out_pwm.fill(0);
    }
    else if (resp.rfind("OK IN=0x", 0) == 0) {
        unsigned int val = 0;
        if (std::sscanf(resp.c_str() + 8, "%x", &val) == 1) {
            telemetry_.inputs_mask = static_cast<uint16_t>(val);
        }
    }
    else if (resp.rfind("OK ADC=", 0) == 0) {
        unsigned int a1 = 0, a2 = 0;
        if (std::sscanf(resp.c_str() + 7, "%u, %u", &a1, &a2) == 2) {
            telemetry_.adc_ain1 = static_cast<uint16_t>(a1);
            telemetry_.adc_esen = static_cast<uint16_t>(a2);
            hist_ain1.AddPoint(t, static_cast<float>(a1));
            hist_esen.AddPoint(t, static_cast<float>(a2));
        }
    }
    else if (resp.rfind("OK PWM=", 0) == 0) {
        unsigned int f[4]{}, d_int[4]{}, d_dec[4]{};
        int n = std::sscanf(resp.c_str() + 7,
                            "%u:%u.%u, %u:%u.%u, %u:%u.%u, %u:%u.%u",
                            &f[0], &d_int[0], &d_dec[0],
                            &f[1], &d_int[1], &d_dec[1],
                            &f[2], &d_int[2], &d_dec[2],
                            &f[3], &d_int[3], &d_dec[3]);
        if (n == 12) {
            for (int i = 0; i < 4; ++i) {
                telemetry_.pwm[i].freq_hz = static_cast<uint16_t>(f[i]);
                telemetry_.pwm[i].duty_pct = static_cast<float>(d_int[i]) + static_cast<float>(d_dec[i]) * 0.1f;
                hist_pwm_duty[i].AddPoint(t, telemetry_.pwm[i].duty_pct);
            }
        }
    }
    else if (resp.rfind("OK FLASH=", 0) == 0) {
        telemetry_.flash_ok = true;
        telemetry_.flash_status = "OK (" + resp.substr(9) + ")";
    }
    else if (resp.rfind("ERR FLASH=", 0) == 0) {
        telemetry_.flash_ok = false;
        telemetry_.flash_status = "FAIL (" + resp.substr(10) + ")";
    }
}