#pragma once

#include <string>
#include <vector>
#include "Wire.h"
#include "Arduino.h"
#include "i2c-handler-common.h"

// Error counter on I2C
float count_total_I2C_bus = 0;
float count_error_no_send = 0;
float count_error_wrong_answer_size = 0;
float count_error_wrong_delimiter = 0;
float count_error_wrong_checksum = 0;

// queue for received data
std::vector<std::string> v_queue;

// millis to trigger rerun of request data
static unsigned long last_exec_millis = 1;
static unsigned int rerun_period_millis = 2000;
const int MAX_WAIT_MILLIS = 3000;

// instead of initialization in yaml:
// void initialize_wire_master()
// {
//     Wire.begin();
//     //    ESP_LOGD(TAG, "Wire connection Master initialized");
// }

// get error text for error code on Wire.endTransmission
std::string error_text_i2c_send(int error_code_i2c_send)
{
    std::string error_text = "";
    switch (error_code_i2c_send)
    {

    case 0:
        error_text = "0: success";
        break;
    case 1:
        error_text = "1: data too long to fit in transmit buffer";
        break;
    case 2:
        error_text = "2: received NACK on transmit of address";
        break;
    case 3:
        error_text = "3: received NACK on transmit of data";
        break;
    case 4:
        error_text = "4: other error";
        break;
    case 5:
        error_text = "5: timeout";
        break;
    default:
        error_text = "";
    }
    return error_text;
}

// send message to slave with command and value
void send_command(std::string command = "UND", std::string sensor_value = "")
{
    // ESP_LOGI(TAG, "---------------------------------");
    count_total_I2C_bus += 1;
    id(count_receives_i2cbus).publish_state(count_total_I2C_bus); // ESPHome ID
    // Reset Counter
    if (count_total_I2C_bus > 100000000) // or __FLT_MAX___
    {
        count_total_I2C_bus = 0;
        count_error_no_send = 0;
        count_error_wrong_answer_size = 0;
        count_error_wrong_delimiter = 0;
        count_error_wrong_checksum = 0;
    }
    // Check if valid command
    CommandKeys send_command_enum = string2enum(command);
    if (send_command_enum == UND)
    {
        ESP_LOGE(TAG, "Command not defined for sending data: %s", command.c_str());
    }
    else // valid command
    {
        std::string send_message = build_message(command, sensor_value);
        // Write command
        ESP_LOGI(TAG, "Sent: %s", send_message.c_str());
        Wire.beginTransmission(SLAVE_ADDRESS);
        Wire.write(send_message.c_str()); 
        int error_code_i2c = Wire.endTransmission(false); // most important for stable I2C Communication: set Wire.endTransmission(false). Defaults to true, releasing the bus after each transmission
        if (error_code_i2c > 0)
        {
            id(error_code_i2c_send_text).publish_state(error_text_i2c_send(error_code_i2c));
            count_error_no_send += 1;
            id(count_error_no_send_sensor).publish_state(count_error_no_send);
            ESP_LOGE(TAG, "No transmission to slave: %s", error_text_i2c_send(error_code_i2c).c_str());
        }
    }
}

// Overloaded function for type conversion and command to send queue
void command(std::string command)
{
    send_command(command, "");
}
// Overloaded function for type conversion and command to send queue
void command(std::string command, std::string sensor_value)
{
    send_command(command, sensor_value);
}
// Overloaded function for type conversion and command to send queue
void command(std::string command, bool sensor_value)
{
    if (sensor_value)
    {
        send_command(command, "true");
    }
    else
    {
        send_command(command, "false");
    }
}
// Overloaded function for type conversion and command to send queue
void command(std::string command, int sensor_value)
{
    send_command(command, std::to_string(sensor_value));
}
// Overloaded function for type conversion and command to send queue
void command(std::string command, float sensor_value)
{
    send_command(command, std::to_string(sensor_value));
}

// Sends request to slave to deliver data in response queue on slave
void request_data()
{
    if (millis() > (last_exec_millis + rerun_period_millis))
    {
        count_total_I2C_bus += 1;
        id(count_receives_i2cbus).publish_state(count_total_I2C_bus);
        std::string response_message = "";
        int nr_of_bytes_i2c_request = Wire.requestFrom(SLAVE_ADDRESS, ANSWERSIZE);
        // ESP_LOGD(TAG, "sending request ...");
        if (nr_of_bytes_i2c_request == 0)
        // no data received. If no data received, waits for timeout of 1 s (bug espressif) !!!!!! Size mostly 0 or ANSWERSIZE, check for < ANSWERSIZE no sense
        // especially high probability for no data received if Wire.endTransmission(true)
        {
            count_error_wrong_answer_size += 1;
            ESP_LOGE(TAG, "No data from slave received");
            id(count_error_wrong_answer_size_sensor).publish_state(count_error_wrong_answer_size);
        }
        else
        {
            id(timeout_slave).publish_state(1); // heartbeat for connection sensor
            // read data to queue
            while (Wire.available())
            {
                response_message += (char)Wire.read();
            }
            int pos4 = response_message.find(DELIMITER_QUEUE);
            if (pos4 <= 2) // corresponds to emtpy message from slave "0*" or ""
            {
                ESP_LOGI(TAG, "Empty message from slave: %s", response_message.c_str());
                if (rerun_period_millis < MAX_WAIT_MILLIS)
                {
                    rerun_period_millis += 1000;
                }
            }
            else
            {
                v_queue.emplace_back(response_message);
                ESP_LOGI(TAG, "Received: %s", response_message.c_str());
                // ESP_LOGI(TAG, "Vector size: %i", v_queue.size());
                // ESP_LOGI(TAG, "Vector inserted last element: %s", v_queue[v_queue.size() - 1].c_str());
                // Adapt loop time
                int pos3 = response_message.find(DELIMITER_END);
                int vector_size = stoi(response_message.substr(pos3 + 1, pos4 - pos3 - 1));
                if (vector_size > 6)
                {
                    rerun_period_millis = 100;
                }
                else if (vector_size < 2 && rerun_period_millis < MAX_WAIT_MILLIS)
                {
                    rerun_period_millis += 100;
                }
                else if (vector_size > 4 && rerun_period_millis > 200)
                {
                    rerun_period_millis -= 200;
                }
                else if (rerun_period_millis > MAX_WAIT_MILLIS)
                {
                    rerun_period_millis = MAX_WAIT_MILLIS;
                }
                // ESP_LOGI(TAG, "Wait period for next run (millis): %i", rerun_period_millis);
            }
        }
        last_exec_millis = millis();
    }
}

// read queue containing data sent back from slave, called by queue only
std::string read_queue()
{
    std::string v_value;
    int n = v_queue.size();
    if (n > 0)
    {
        while (n > MAX_QUEUE_SIZE)
        {
            ESP_LOGE(TAG, "Queue overflow: %i elements", n);
            v_queue.erase(v_queue.begin());
            n = v_queue.size();
        }

        v_value = v_queue[0];
        v_queue.erase(v_queue.begin());
        // ESP_LOGI(TAG, "Selected value from queue: %s", v_value.c_str());
        return v_value;
    }
    else
    {
        return {};
    };
}

// Convert string to boolean
bool string2bool(std::string response_value)
{
    if (response_value == "true")
    {
        return true;
    }
    else
    {
        return false;
    }
}

// check quality of data in queue, execute corresponding commands and / or sensor updates
void analyse_queue()
{
    std::string response_message = read_queue();
    if (response_message != "")
    {
        int pos1 = response_message.find(DELIMITER_COMMAND);
        int pos2 = response_message.find(DELIMITER_CHK_SUM);
        int pos3 = response_message.find(DELIMITER_END);
        if (pos1 == -1 || pos2 == -1 || pos3 == -1)
        {
            count_error_wrong_delimiter += 1;
            ESP_LOGE(TAG, "Delimiter not found in %s", response_message.c_str());
            id(count_error_wrong_delimiter_sensor).publish_state(count_error_wrong_delimiter);
        }
        else
        {
            std::string response_checksum = response_message.substr(pos2 + 1, pos3 - pos2 - 1);
            std::string response_checksum_input = response_message.substr(0, pos2);
            int checksum_recalced = calc_checksum(response_checksum_input);
            if (stoi(response_checksum) != checksum_recalced)
            {
                count_error_wrong_checksum += 1;
                ESP_LOGE(TAG, "Wrong cheksum slave %s <> master %i", response_checksum.c_str(), checksum_recalced);
                id(count_error_wrong_checksum_sensor).publish_state(count_error_wrong_checksum);
            }
            else
            {
                CommandKeys response_command = string2enum(response_message.substr(0, pos1));
                std::string response_value = response_message.substr(pos1 + 1, pos2 - pos1 - 1);
                // ESP_LOGD(TAG, "Received response_value %s", response_value.c_str());
                switch (response_command)
                {
                case TFB:
                    id(temp_fresh_before_mirror).publish_state(std::stof(response_value));
                    break;
                case TFA:
                    id(temp_fresh_after_mirror).publish_state(std::stof(response_value));
                    break;
                case TUB:
                    id(temp_used_before_mirror).publish_state(std::stof(response_value));
                    break;
                case TUA:
                    id(temp_used_after_mirror).publish_state(std::stof(response_value));
                    break;
                case RES:
                    ESP_LOGI(TAG, "Success: Restart command to slave");
                    break;
                // Fan fresh air
                case FFS:
                    id(fan_fresh_mirror).make_call().set_speed(stoi(response_value)).perform();
                    break;
                // Fan used air
                case FUS:
                    id(fan_used_mirror).make_call().set_speed(stoi(response_value)).perform();
                    break;
                // Tacho fresh air
                case TFS:
                    id(tacho_fresh_mirror).publish_state(std::stof(response_value));
                    break;
                // Tacho used air
                case TUS:
                    id(tacho_used_mirror).publish_state(std::stof(response_value));
                    break;
                // Power fresh air
                case PWF:
                    id(pwr_fresh_mirror).publish_state(std::stof(response_value));
                    break;
                // Power used air
                case PWU:
                    id(pwr_used_mirror).publish_state(std::stof(response_value));
                    break;
                case FHS:
                    id(flap_heat_exchanger_mirror).publish_state(string2bool(response_value));
                    break;
                case WIS:
                    id(wifi_slave_mirror).publish_state(string2bool(response_value));
                    break;
                case UND:
                    ESP_LOGE(TAG, "Command not defined (UND)");
                    break;
                }
            }
        }
    }
}