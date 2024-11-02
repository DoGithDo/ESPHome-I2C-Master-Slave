#pragma once

#include <string>
#include "Arduino.h"
#include "esphome.h"


////////////////// items for performance measurement  ///////////////////////////////////
/*
#include <chrono>

using namespace std::chrono;

auto beg = high_resolution_clock::now();
auto end = high_resolution_clock::now();
auto duration = duration_cast<microseconds>(end - beg);
ESP_LOGD(TAG, "duration: %i ms", duration);
*/

// I2C
const int SLAVE_ADDRESS = 9;
const int ANSWERSIZE = 22;     // max of 32 in buffer, size has impact on performance especially on low bus frequency
const int MAX_QUEUE_SIZE = 10; // for vectors

// Delimiters
const char *DELIMITER_COMMAND = ":"; // pos1
const char *DELIMITER_CHK_SUM = "#"; // pos2
const char *DELIMITER_END = "$";     // pos3
const char *DELIMITER_QUEUE = "*";   // pos4

// Logger
const char *TAG = "I2C";

// Commands for master <-> slave
enum CommandKeys
{
    UND, // Undefined, no or wrong command
    TFB, // float Temperature Fresh air Before heat exchanger, state only
    TFA, // float Temperature Fresh air After heat exchanger, state only
    TUB, // Temperature Used air Before heat exchanger, state only
    TUA, // Temperature Used air After heat exchanger, state only
    RES, // Restart Slave, command only
    FFS, // Fan Fresh State, state and command
    FUS, // Fan Used State, state and command
    TFS, // Tacho fan Fresh air State, state
    TUS, // Tacho fan Used air State, state
    PWF, // PoWer fan Fresh air state, state
    PWU, // PoWer fan Used air state, state
    FHS, // Flap Heat exchanger State, state and command
    WIS, // WIfi on slave State, state and command
};

// convert string with command to enum CommandKeys
CommandKeys string2enum(const std::string str)
{
    if (str == "TFB")
        return TFB;
    else if (str == "TFA")
        return TFA;
    else if (str == "TUB")
        return TUB;
    else if (str == "TUA")
        return TUA;
    else if (str == "RES")
        return RES;
    else if (str == "FFS")
        return FFS;
    else if (str == "FUS")
        return FUS;
    else if (str == "FHS")
        return FHS;
    else if (str == "TFS")
        return TFS;
    else if (str == "TUS")
        return TUS;
    else if (str == "PWF")
        return PWF;
    else if (str == "PWU")
        return PWU;
    else if (str == "WIS")
        return WIS;
    else
    {
        ESP_LOGE(TAG, "No valid command, set command to UND");
        return UND;
    }
}

// calculate checksum, used for master and slave
int calc_checksum(std::string check)
{
    uint8_t data[ANSWERSIZE];
    std::copy(check.begin(), check.end(), std::begin(data));
    int chk_sum = esphome::crc8(data, check.length());
    /*
    // old version fletcher checksum
    const char *arr = check.c_str();
    // const char arr[] = "Test";
    int chk_sum;
    uint8_t sum1 = 0;
    uint8_t sum2 = 0;
    int i = 0;
    while (((uint8_t)arr[i]) != 0)
    {
        sum1 = sum1 + (uint8_t)arr[i];
        sum2 = sum2 + sum1;
        i++;
    }
    chk_sum = (sum2 << 8) | sum1;
    // ESP_LOGD(TAG, "Function ChecksumFletcher16: %i", chk_sum);
    */
    return chk_sum;
}

// Build message to send with command, value and checksum
std::string build_message(std::string command, std::string sensor_value = "")
{
    std::string message = "";
    message.append(command); // append for much better performance as with sprintf or +
    message.append(DELIMITER_COMMAND);
    message.append(sensor_value);
    int checksum_calced = calc_checksum(message);
    char checksum_char[6]; // XXX
    sprintf(checksum_char, "%d", checksum_calced);
    message.append(DELIMITER_CHK_SUM);
    message.append(checksum_char);
    message.append(DELIMITER_END);
    return message;
}
