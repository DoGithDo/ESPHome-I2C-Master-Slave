# ESPHome-I2C-Master-Slave for Heat recovery ventilation
# Overview <!-- omit from toc -->
This project describes the smartification of a heat recovery ventilation system. ESPHome and I2C are used between master and slave. The focus of the description is mainly on the I2C challenge and the experiences gained. In the end, the project was not put live in this form. I2C was not working on a 10 m connection; instead, an RS485 is used.

# Table of contents <!-- omit from toc -->
- [Heat recovery ventilation (HRV)](#heat-recovery-ventilation-hrv)
  - [Existing Setup](#existing-setup)
  - [New Setup](#new-setup)
    - [Concept](#concept)
    - [Master](#master)
    - [Slave](#slave)
    - [Mirroring](#mirroring)
    - [Self healing](#self-healing)
    - [Functions in Home Assistant](#functions-in-home-assistant)
- [Software](#software)
  - [Concept](#concept-1)
  - [Messages](#messages)
    - [Structure](#structure)
    - [Sending](#sending)
  - [Master](#master-1)
    - [Request Data](#request-data)
    - [Analyse Queue](#analyse-queue)
  - [Slave](#slave-1)
  - [Update of ESPHome entities using C++](#update-of-esphome-entities-using-c)
  - [I2C](#i2c)
    - [Master to Slave](#master-to-slave)
      - [Initialization](#initialization)
      - [Sending Data](#sending-data)
      - [Requesting Data](#requesting-data)
    - [Slave to Master](#slave-to-master)
      - [Initialization](#initialization-1)
      - [Receive Event](#receive-event)
      - [Request Event](#request-event)
  - [Issues on I2C and solutions](#issues-on-i2c-and-solutions)
- [The big but: it's not working](#the-big-but-its-not-working)
- [Experiences](#experiences)
  - [Positive](#positive)
  - [How to improve](#how-to-improve)

# Heat recovery ventilation (HRV)
## Existing Setup
There is a ventilation system with a heat exchanger for the flat in my basement. The ventilation is controlled via a rotary switch in the flat. The ventilation system decides whether the flap of the heat exchanger is open or closed based on the measured temperature of the fresh air.
The ventilation system consists of the following components:
- A fan for the fresh air
- A fan for the exhaust air
- Two temperature sensors with the option of setting when the flap is opened or closed
- A motor that opens or closes the flap
- A rotary switch in the flat, with 4 positions (off, 1, 2, 3), which is connected to the basement via an approx. 10 m long cable

There is no WLAN in the basement.


## New Setup
### Concept
The ventilation should be made ‘smart’. The following components are used from the existing setup:
- The two fans
- The motor for the ventilation flap
- The rotary switch (with the existing cable to the new master)
- The existing cable from flat (master) to basement (slave)

As there is no WLAN reception in the basement, communication between the flat and the basement should take place via the existing cable.
The plan was to have an ESP32 (master) in the flat, which has a connection to the Internet / Home Assistant / CO2 sensors in the flat. In the basement, an ESP32 (slave) with various sensors should control the fans and the flap. The communication between the two ESPs uses I2C.

### Master
The master in the flat has the following functionality:
- Mirrors all sensors from the slave so that they can be read by the user or by Home Assistant.
- Receives commands from the Home Assistant (via WLAN) and sends them to the slave via I2C.
- If the master has no connection to Home Assistant, it behaves autonomously (e.g. deciding whether summer or winter)

### Slave
The slave receives the commands from the master and supplies the data from the sensors to the master via I2C.
The most important components of the slave:
- Control of the two fans
- Control of the flap (relay)
- Temperature sensor for fresh air and exhaust air
Other sensors are also available:
- Measured speed of the two motors
- Measurement of power consumption
- Additional temperature sensors, for difference fresh air / exhaust air before or after the heat exchanger

Additional functions:
- The slave also has an autonomous mode in case the I2C communication fails.
- The slave can be set to access point mode via a command from the master (via I2C).

### Mirroring
All sensors of the slave are mirrored on the master. Example: The temperature of the fresh air is measured by the slave and then regularly sent to the master. It is available on the master in a template sensor.

### Self healing
If the connection between master and slave is interrupted, information is lost due to poor quality or a device restarts, the system heals itself.
Example: The master sends the command to the slave to close the flap. If the slave reports the status as open, the master sends the command to close again.

### Functions in Home Assistant
A few ideas on how Home Assistant automates the master, not described in detail:
- Stronger ventilation with more people in the home
- Increased ventilation in case of poor air quality (CO2, VOC)
- When the temperature is high in summer, the ventilation system should ventilate more strongly when the outside temperature is cooler (at night).
- If the tumble dryer in the flat heats up, the ventilation should run depending on the heating period.

# Software
## Concept
The ESP32 uses ESPHome. Unfortunately, ESPHome does not deliver any functions for an I2C slave. These functions were therefore implemented in C++.

Basic functionality:
- The entities (sensors, switches, ...) send messages between master and slave.
- These messages are constructed and parsed by a C++ package
- The messages are precalculated on the slave and then placed in a queue (C++ vector). From there they are picked up by the master.
- On the master, the received messages are also placed in a queue (C++ vector) and are processed later.

## Messages
### Structure
Examples of a message:

`TUB:22.6875#137$3*` (see explanation)

`PWF:-26.8848#248$1*`

`FHS:false#38$0*`

Explanation:
| Element   | Description                                                                                            |
| --------- | ------------------------------------------------------------------------------------------------------ |
| `TUB`     | Commands (type of message). These are defined as `enum` in i2c-handler-common.h                        |
| `:`       | Delimiter between command and payload.                                                                 |
| `22.6875` | Payload.  The payload is the e. g. the state of a sensor. In this case the temperature.                |
| `#`       | Delimiter between payload and checksum.                                                                |
| `137`     | Checksum                                                                                               |
| `$`       | Delimiter between cheksum and number of elements waiting in the queue. In case of master also end tag. |
| `3`       | Slave only: number of elements in the queue waiting to be delivered to the master                      |
| `*`       | Slave only: end tag                                                                                    |

### Sending
Master and Slave send messages.

Example in a switch:
```
    on_turn_on:
    - lambda: |-
        message("FHS", id(flap_heat_exchanger).state);
```

The `lambda` calls a C++ function. Parameter 1 is the command / type of message. Parameter 2 is the sensor value.

Example in an intervall:

```
- interval: ${update_interval_short}
    startup_delay: 3s
    then:
      - lambda: |-
          message("FFS", id(fan_fresh).speed);
```
Example on other triggers (here a fan):
```
    on_speed_set:
        - lambda: |-
            message("FFS", id(fan_fresh).speed);
```

## Master
The master periodically requests data from the slave and analyses the data in the queue. These functions were first called in the loop (`on_loop`). However, this led to performance issues (see below [Issues on I2C and Solutions](#issues-on-i2c-and-solutions)). 

```
interval:
 - interval: 0.3s
   then:
   - lambda: |-
      analyse_queue();
 - interval: 0.3s
   then:
   - lambda: |-
      request_data();
```
### Request Data
The master periodically requests the slave to supply data. The interval is defined as very short. However, the C++ function checks how many messages are still waiting in the slave's queue and adjusts the time until the next request accordingly.

### Analyse Queue
The data received from the slave is not analysed immediately, but written to an incoming queue. Function `analyse_queue` parses the message, checks the checksum and updates the ESPHome entities.

## Slave

## Update of ESPHome entities using C++
The ESPHome entities are directly updated using C++.

| Example                                                                      | Description                                                                        |
| ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| `id(temp_fresh_before_mirror).publish_state(std::stof(response_value))`      | Updating a mirrored sensor on master. Converting string to float before publishing |
| `id(fan_fresh_mirror).make_call().set_speed(stoi(response_value)).perform()` | Updating the speed of a fan. Converting string to integer before updating          |
| `id(flap_heat_exchanger).turn_off()`                                         | Turning off a switch                                                               |

During programming, these direct calls were very unpleasant. The development environment used (VSCode) did not recognise the ESPHome entities in C++.
I have spent some time reading up on Intellisense but have not found a solution to this (cosmetic) problem.

## I2C
### Master to Slave
#### Initialization
ESPHome supports Master to Slave communication. Therefore, I2C can be initialized directly in ESPHome:

```
i2c:
  scan: false
  frequency: 100kHz #lower values needs more time for Wire.requestFrom
```
I also tried the variant of initializing Wire directly in C++ with `Wire.begin()` and `on_boot`. There is no (performance) difference between these variants.

#### Sending Data
Used code for sending the data:

```
Wire.beginTransmission(SLAVE_ADDRESS);
Wire.write(send_message.c_str()); 
int error_code_i2c = Wire.endTransmission(false);
```
> [!NOTE]
> For a stable connection, use `Wire.endTransmission(false)`. Defaults to `true`, releasing the bus after each transmission. I got an unstable communication and had a lot of performance issues with true. See [Issues on I2C and solutions](#issues-on-i2c-and-solutions).

#### Requesting Data
```
Wire.requestFrom(SLAVE_ADDRESS, ANSWERSIZE)
```

```
while (Wire.available())
// read the buffer
// do something with the incoming data
```
> [!NOTE]
> "Do something with the incoming data" must be very performant. In a first version I processed the incoming messages directly, which led to performance problems. In this version the data is written into a queue (`vector`) and processed later.

### Slave to Master
#### Initialization
```
Wire.begin(SLAVE_ADDRESS);
Wire.onRequest(requestEvent);
Wire.onReceive(receiveEvent);
```
Initialization of the bus and definition of the request and receive events.

#### Receive Event
```
while (Wire.available())
{
    received_message += (char)Wire.read();
}
received_queue.emplace_back(received_message);
```
> [!NOTE]
> In a first version I processed the incoming messages directly, which led to performance problems. In this version the data is written into a queue (`received_queue` as `vector`) and processed later.


#### Request Event

```
Wire.write((byte *)response_message, ANSWERSIZE);
```
The `response_message` is taken out of the `response_queue` (a `vector`).
> [!NOTE]
> In a first version, Request Event did read the sensor value, calculate the checksum and concatenate the string with all delimiters. This took too long and I had performance issues. In this version the data is written into a queue (`response_queue` as `vector`) and waiting for delivery.

## Issues on I2C and solutions
During development I often had faulty messages. The cause was often that the ESP32 was busy and could not respond to the bus. In particular I had the following problems:
- Sending or receiving messages took too long because in addition to sending/receiving, I also prepared or processed the messages in the function. It is especially important that receiveEvent and requestEvent are as "empty" as possible. **Solution:** use queues.
- Whenever I had an undetected runtime error in C++ (e.g. an impossible conversion of an invalid string to a float) I2C would become blocked. The consequence was always an unstable I2C connection. Several consecutive messages would come out unclear until the system recovered. **Solution:** Error handling in type conversion (or in general: better coding).
- When I used `on_loop` in YAML, the ESP was overloaded. The I2C connection became unstable. Solution: use `interval`.
- For a stable connection, use `Wire.endTransmission(false)`. Defaults to `true`, releasing the bus after each transmission.

In some cases, the cause of these issues may be a bug that has been present in the Wire library for some time:
https://github.com/espressif/arduino-esp32/issues/5934

This issue may also be the reason why it takes longer for the bus to "recover" after an error occurs.

Calculating a checksum would not be necessary. Either the message was completely correct, or it was so incomplete that no delimiters were found in the message.

I ran this code for several days using a 10 cm cable. And with thousands of messages, it has a success rate of 100%.

# The big but: it's not working
With the short cable everything worked perfectly. With a long cable of 10 m reliable communication was no longer possible.
My friend - who built the hardware components of this project - tried various things. I don't understand all the electronic tricks, but he tried the following, for example:
- Using a P82B715 bus extender
- Changing the quality of the cable
- Using resistors and capacitors to make the signal "clearer"
- We did measure the data bits using an oscilloscope. There was a too early drop in the signal on the falling edge.

**Solution:** We are switching to a serial interface (RS485). The first prototype makes us confident.

# Experiences
## Positive
I wanted to do the project with ESPHome because you can implement many functions very easily (all sensors, web server, WiFi, ...). This has been confirmed.

## How to improve
- Installing WiFi in the basement would have provided a much simpler solution.
- Use a serial interface (RS485).
- Building an own component in ESPHome or a custom component much easier? Way over my head.
