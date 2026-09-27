#ifndef RX_SERIAL_CLI_H
#define RX_SERIAL_CLI_H

#include <Arduino.h>

class SerialCLI {
public:
    SerialCLI();
    void begin();
    void update();

private:
    String _inputBuffer;
    void processCommand(const String& rawCmd);
    void printHelp();
    void printStatus();
};

extern SerialCLI cli;

#endif // RX_SERIAL_CLI_H
