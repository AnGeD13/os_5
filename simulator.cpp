#include "serial.hpp"
#include <iostream>
#include <random>
#include <string>
#include <cstdint>


void csleep(double timeout) {
#if defined (WIN32)
    if (timeout <= 0.0)
        ::Sleep(INFINITE);
    else
        ::Sleep(static_cast<DWORD>(timeout * 1e3));
#else
    if (timeout <= 0.0)
        pause();
    else {
        struct timespec t;
        t.tv_sec = (int)timeout;
        t.tv_nsec = (int)((timeout - t.tv_sec)*1e9);
        nanosleep(&t, NULL);
    }
#endif
}

float getNextTemperature(float currentTemperature) {
    const float temperatureStep = 0.2f;

    if (std::rand() % 2 == 0) {
        currentTemperature += temperatureStep;
    } else {
        currentTemperature -= temperatureStep;
    }

    return currentTemperature;
}

void getTemperature(cplib::SerialPort& port) {
    float currentTemperature = std::rand() % 23 - 5;

    while (true) {
        currentTemperature = getNextTemperature(currentTemperature);

        std::cout << std::to_string(currentTemperature) << std::endl;
        port << std::to_string(currentTemperature) << "\n";
        csleep(SERIAL_PORT_DEFAULT_TIMEOUT);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: [port]" << std::endl;
        return -1;
    }

    cplib::SerialPort smport(std::string(argv[1]), cplib::SerialPort::BAUDRATE_115200);
    if (!smport.IsOpen()) {
        std::cout << "Failed to open port '" << argv[1] << "'! Terminating..." << std::endl;
        return -2;
    }
    
    getTemperature(smport);

    smport.Close();
    return 0;
}