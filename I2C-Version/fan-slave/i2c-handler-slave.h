#pragma once

#include <cstdlib> //for random rand()
#include <string>
#include <vector>
#include "Wire.h"
#include "Arduino.h"
#include "i2c-handler-common.h"

// Queue for received data
static std::vector<std::string> received_queue;

// Queue for response data
static std::vector<std::string> response_queue;

void receiveEvent(int numBytesReceived)
{
    std::string received_message = "";
    while (Wire.available())
    {
        received_message += (char)Wire.read();
    }
    received_queue.emplace_back(received_message);
    //ESP_LOGI(TAG, "Received: %s", received_message.c_str());
    //ESP_LOGI(TAG, "Vector size: %i", received_queue.size());
    //ESP_LOGI(TAG, "Vector inserted last element: %s", received_queue[received_queue.size() - 1].c_str());
}

// read queue containing commands sent from master
std::string read_received_queue()
{
    std::string v_value;
    int n = received_queue.size();
    // ESP_LOGI(TAG, "Vector size: %i", received_queue.size());
    if (n > 0)
    {
        while (n > MAX_QUEUE_SIZE)
        {
            ESP_LOGE(TAG, "Queue overflow: %i elements", n);
            received_queue.erase(received_queue.begin());
            n = received_queue.size();
        }

        v_value = received_queue[0];
        received_queue.erase(received_queue.begin());
        // ESP_LOGI(TAG, "Selected value from queue: %s", v_value.c_str());
        // ESP_LOGI(TAG, "Vector size received queue: %i", received_queue.size());
        return v_value;
    }
    else
    {
        return {};
    };
}

// read queue containing messages to deliver to master
std::string read_response_queue()
{
    std::string v_value;
    int n = response_queue.size();
    if (n > 0)
    {
        // size of queue managed in message_to_queue
        v_value = response_queue[0];
        // ESP_LOGD(TAG, "Selected value from response queue: %s", v_value.c_str());
        response_queue.erase(response_queue.begin());
        return v_value;
    }
    else
    {
        return {};
    };
}

// Write built message (command, sensor value, checksum) to response queue
void message_to_queue(std::string command = "UND", std::string sensor_value = "")
{
    CommandKeys message_command_enum = string2enum(command);
    if (message_command_enum == UND)
    {
        ESP_LOGE(TAG, "Command not defined for response data: %s", command.c_str());
    }
    else // valid command
    {
        // pre_build_write
        std::string pre_built_message = build_message(command, sensor_value);
        response_queue.emplace_back(pre_built_message);
        // check queue size and inject special message
        int n = response_queue.size();
        if (n > (MAX_QUEUE_SIZE + 20))
        {
            while (n > (MAX_QUEUE_SIZE + 20))
            {
                ESP_LOGE(TAG, "Queue vector overflow: %i elements", n);
                response_queue.erase(response_queue.begin());
                n = response_queue.size();
            }
        }
    }
}

// Overloaded function for type conversion and message to queue
void message(std::string command)
{
    message_to_queue(command, "");
}
// Overloaded function for type conversion and message to queue
void message(std::string command, std::string sensor_value)
{
    message_to_queue(command, sensor_value);
}
// Overloaded function for type conversion and message to queue
void message(std::string command, float sensor_value)
{
    std::string sensor_value_helper = std::to_string(sensor_value);
    int pos1 = sensor_value_helper.find(".");
    if (pos1 > 0) // only if . found
    {
        sensor_value_helper = sensor_value_helper.substr(0, pos1 + 3); // two digits after .
    }
    message_to_queue(command, sensor_value_helper);
}
// Overloaded function for type conversion and message to queue
void message(std::string command, int sensor_value)
{
    message_to_queue(command, std::to_string(sensor_value));
}
// Overloaded function for type conversion and message to queue
void message(std::string command, bool sensor_value)
{
    if (sensor_value)
    {
        message_to_queue(command, "true");
    }
    else
    {
        message_to_queue(command, "false");
    }
}

// Check data in receive queue and execute commands
void analyse_receive_queue()
{
    if (received_queue.size() > 0)
    {
        std::string received_message = read_received_queue();
        if (received_message != "")
        {
            ESP_LOGI(TAG, "Message to analyse: %s", received_message.c_str());
            // ESP_LOGI(TAG, "Messages in queue: %i", received_queue.size());
            int pos1 = received_message.find(DELIMITER_COMMAND);
            int pos2 = received_message.find(DELIMITER_CHK_SUM);
            int pos3 = received_message.find(DELIMITER_END);
            if (pos1 == -1 || pos2 == -1 || pos3 == -1)
            {
                ESP_LOGE(TAG, "Delimiter not found in %s", received_message.c_str());
            }
            else
            {
                std::string received_checksum = received_message.substr(pos2 + 1, pos3 - pos2 - 1);
                std::string received_checksum_input = received_message.substr(0, pos2);
                int checksum_recalced = calc_checksum(received_checksum_input);
                if (stoi(received_checksum) != checksum_recalced)
                {
                    ESP_LOGE(TAG, "Wrong cheksum slave %s <> master %i", received_checksum.c_str(), checksum_recalced);
                }
                else
                {
                    CommandKeys received_command = string2enum(received_message.substr(0, pos1));
                    std::string received_value = received_message.substr(pos1 + 1, pos2 - pos1 - 1);
                    // ESP_LOGD(TAG, "Received response_value %s", received_value.c_str());
                    // ESP_LOGI(TAG, "Received command: %s", received_command.c_str());
                    /////////////// Execute commands
                    switch (received_command)
                    {
                    // Restart slave
                    case RES:
                        message_to_queue("RES");
                        delay(10000);
                        id(restart_button).press();
                        break;
                    // Fan fresh air
                    case FFS:
                        if (received_value != to_string(id(fan_fresh).speed))
                        {
                            int int_received_value = stoi(received_value);
                            if (int_received_value > 0) // check if correct conversion from string to int
                            {
                                id(fan_fresh).make_call().set_speed(int_received_value).perform();
                            }
                        }
                        break;
                    // Fan used air
                    case FUS:
                        if (received_value != to_string(id(fan_used).speed))
                        {
                            int int_received_value = stoi(received_value);
                            if (int_received_value > 0) // check if correct conversion from string to int
                            {
                                id(fan_used).make_call().set_speed(int_received_value).perform();
                            }
                        }
                        break;
                    // Flap heat exchanger
                    case FHS:
                        // ESP_LOGI(TAG, "Status FHS: %s", std::to_string(id(flap_heat_exchanger).state).c_str());
                        if (received_value == "true" && id(flap_heat_exchanger).state == false)
                        {
                            // ESP_LOGI(TAG, "Status flap to true, status before turn on: %s", std::to_string(id(flap_heat_exchanger).state).c_str());
                            id(flap_heat_exchanger).turn_on();
                        }
                        if (received_value == "false" && id(flap_heat_exchanger).state == true)
                        {
                            // ESP_LOGI(TAG, "Status flap to false, status before turn off: %s", std::to_string(id(flap_heat_exchanger).state).c_str());
                            id(flap_heat_exchanger).turn_off();
                        }
                        break;
                    // Wifi slave
                    case WIS:
                        // ESP_LOGI(TAG, "Command wifi status: %s", std::to_string(id(wifi_slave).state).c_str());
                        if (received_value == "true" && id(wifi_slave).state == false)
                        {
                            id(wifi_slave).turn_on();
                        }
                        if (received_value == "false" && id(wifi_slave).state == true)
                        {
                            id(wifi_slave).turn_off();
                        }
                        break;
                    case UND:
                        ESP_LOGE(TAG, "Command not defined (UND)");
                        break;
                    }
                }
            }
        }
    }
}

// Sends first message in queue to master
void requestEvent()
{
    char response_message[ANSWERSIZE] = "";
    sprintf(response_message, "%s%i%s", read_response_queue().c_str(), response_queue.size(), DELIMITER_QUEUE); // empty response queue results in "0*"
    Wire.write((byte *)response_message, ANSWERSIZE);
    ESP_LOGI(TAG, "Response: %s.", response_message);
    id(timeout_master).publish_state(1); // heartbeat for connection sensor
}

// Setup wire in slave mode, define onRequest and onReceive
void initialize_wire_slave()
{
    Wire.begin(SLAVE_ADDRESS);
    Wire.onRequest(requestEvent);
    Wire.onReceive(receiveEvent);
}

// generate random data for testing for temperature fresh air before heat exchanger
float write_random_TFB()
{
    float random_float = rand() % 5 + 10; // random 0-4
    return random_float;
}

// generate random data for testing for temperature used air before heat exchanger
float write_random_TUB()
{
    float random_float = rand() % 5 + 20; // random 0-4
    return random_float;
}