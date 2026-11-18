#pragma once
#include <string>
#include <vector>
#include <cstdint>

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    static std::vector<std::string> ListAvailablePorts();

    bool Open(const std::string& port_name, uint32_t baudrate = 115200);
    void Close();
    bool IsOpen() const;

    bool WriteLine(const std::string& line);
    bool ReadLine(std::string& out_line, uint32_t timeout_ms = 50);

private:
#ifdef _WIN32
    void* handle_ = reinterpret_cast<void*>(-1);
#else
    int fd_ = -1;
#endif
    std::string rx_buffer_;
};