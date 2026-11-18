#include "serial_port.hpp"
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <dirent.h>
#endif

SerialPort::SerialPort() = default;
SerialPort::~SerialPort() { Close(); }

std::vector<std::string> SerialPort::ListAvailablePorts() {
    std::vector<std::string> ports;
#ifdef _WIN32
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devInfoData{};
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
            HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (hKey != INVALID_HANDLE_VALUE) {
                char portName[64]{};
                DWORD size = sizeof(portName);
                DWORD type = 0;
                if (RegQueryValueExA(hKey, "PortName", nullptr, &type, reinterpret_cast<LPBYTE>(portName), &size) == ERROR_SUCCESS) {
                    if (std::string(portName).rfind("COM", 0) == 0) {
                        ports.emplace_back(portName);
                    }
                }
                RegCloseKey(hKey);
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
#else
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
#ifdef __APPLE__
            if (name.rfind("cu.usb", 0) == 0 || name.rfind("cu.SLAB", 0) == 0 || name.rfind("cu.wch", 0) == 0) {
                ports.push_back("/dev/" + name);
            }
#else
            if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0) {
                ports.push_back("/dev/" + name);
            }
#endif
        }
        closedir(dir);
    }
#endif
    std::sort(ports.begin(), ports.end());
    return ports;
}

bool SerialPort::Open(const std::string& port_name, uint32_t baudrate) {
    Close();
    rx_buffer_.clear();
#ifdef _WIN32
    std::string full_name = "\\\\.\\" + port_name;
    HANDLE h = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return false; }
    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return false; }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 5;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(h, &timeouts);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handle_ = h;
    return true;
#else
    fd_ = open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) { Close(); return false; }

    speed_t spd = B115200;
    if (baudrate == 9600) spd = B9600;
    else if (baudrate == 57600) spd = B57600;
    cfsetospeed(&tty, spd);
    cfsetispeed(&tty, spd);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) { Close(); return false; }
    tcflush(fd_, TCIOFLUSH);
    return true;
#endif
}

void SerialPort::Close() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

bool SerialPort::IsOpen() const {
#ifdef _WIN32
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
}

bool SerialPort::WriteLine(const std::string& line) {
    if (!IsOpen()) return false;
    std::string data = line + "\r\n";
#ifdef _WIN32
    DWORD written = 0;
    return WriteFile(static_cast<HANDLE>(handle_), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
#else
    ssize_t res = write(fd_, data.data(), data.size());
    return res == static_cast<ssize_t>(data.size());
#endif
}

bool SerialPort::ReadLine(std::string& out_line, uint32_t timeout_ms) {
    if (!IsOpen()) return false;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto pos = rx_buffer_.find('\n');
        if (pos != std::string::npos) {
            out_line = rx_buffer_.substr(0, pos);
            rx_buffer_.erase(0, pos + 1);
            if (!out_line.empty() && out_line.back() == '\r') out_line.pop_back();
            return true;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) return false;

        char buf[256];
#ifdef _WIN32
        DWORD bytes_read = 0;
        if (ReadFile(static_cast<HANDLE>(handle_), buf, sizeof(buf), &bytes_read, nullptr) && bytes_read > 0) {
            rx_buffer_.append(buf, bytes_read);
        }
#else
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        struct timeval tv{};
        long rem = static_cast<long>(timeout_ms - elapsed);
        tv.tv_sec = rem / 1000;
        tv.tv_usec = (rem % 1000) * 1000;
        if (select(fd_ + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            ssize_t n = read(fd_, buf, sizeof(buf));
            if (n > 0) rx_buffer_.append(buf, n);
        }
#endif
    }
}