/* LeafLoop
 *
 * Parses Nissan LEAF CAN messages using CarLoop. Tested on:
 * - MY 2014 X (24kWh)
 * - MY 2012 G (24Kwh) - dcqc and l1l2 counts do not work, no dashboard SoC present (returns internal SoC value instead)
 *
 * Requires:
 * - CarLoop + optional GPS module https://store.carloop.io/
 * - Particle Electron or Photon https://store.particle.io/
 *
 * CAN message decoding compiled from:
 * - https://docs.google.com/spreadsheets/d/1EHa4R85BttuY4JZ-EnssH4YZddpsDVu6rUFm0P7ouwg
 * - https://github.com/lincomatic/LeafCAN
 * - https://github.com/openvehicles/Open-Vehicle-Monitoring-System/blob/master/vehicle/OVMS.X/vehicle_nissanleaf.c
 * - ... and a lot of time looking through CAN messages the hard way!
 *
 * Disclaimer - no warranty is implied, use at your own risk - messing around with your car's computer systems could be dangerous!!
 *
 * Copyright 2019 Mike Debney
 * Distributed under the MIT license.
 *
 * Controller addresses:
 * VCM – 797 - 79A
 * LBC – 79B - 7BB
 * OBC – 792 - 793
 * Inv/MC – 784 - 78C
 * E Shifter – 79D - 7BD
 * HVAC – 744 - 764
 * TCU - ??? - ???
 * VSP - 73F - 761
 * ??? - 743 - 765
 * ??? - 745 - 763  <- traction control?
 * ??? - 784 - ???
 * ??? - 746 - ???
 */


#include "application.h"
#include "carloop.h"
#include "math.h"

SYSTEM_THREAD(ENABLED);  // make sure code still runs while disconnected

STARTUP(cellular_credentials_set("internet", "", "", NULL));  // 2degrees (NZ) APN settings, change as required

Carloop<CarloopRevision2> carloop;

bool SERIAL_DEBUG = false;  // turn this on to print current status on serial port - however this will stop the GPS from working

int lastWakeUp = 0;
int lastMessage = 0;
int lastPoll = 0;
int lastUpdate = 0;
const int pollInterval = 60;

bool issuePoll = false;
String pollArgument;
bool issueWake = false;

bool issuePollLbc = false;
bool issuePollObc = false;
bool issuePollVcm = false;

int isAwake = 0;
const int awakeTimeout = 5;

int uptime = -1;
int lastConnected = -1;
int connectedUptime = -1;
double strength = -1;
double quality = -1;
double accBatteryVoltage;

bool isConnected = false;
bool hasLocation = false;
double lat;
double lng;

String carState;
String prndb;

int hvGids = -1;
double hvKwh = -1;
int hvSoh = -1;
double hvSoc = -1;
bool hasDashboardSoc = false;
double hvFullSoc = -1;
double hvV = -1;
double hvAh = -1;
double hvHx = -1;
double hvTempC = -100;
double accV = -1;

int outsideTempC = -100;
int insideTempC = -100;

int odoKm = -1;
int range = -1;
int rangeAcDifference = 0;

int isCharging = -1;
int dcqc = -1;
int l2l1 = -1;
int isChargingQc = -1;
int isChargingAc = -1;

int lights = -1;
int headlightsOn = -1;
int parkingLightsOn = -1;
int fogLightsOn = -1;

int doors = -1;
int doorR = -1;
int doorRR = -1;
int doorRL = -1;
int doorFR = -1;
int doorFL = -1;

int isLocked = -1;

void setup()
{
    Cellular.setBandSelect("900"); 
    Serial.begin(9600);
    
    // expose variables to particle's cloud (maximum of 20 allowed)
    Particle.variable("total_uptime", &uptime, INT);
    Particle.variable("connected_uptime", &connectedUptime, INT);
    Particle.variable("signal_strength", &strength, DOUBLE);
    Particle.variable("signal_quality", &quality, DOUBLE);
    
    Particle.variable("acc_batt_v", &accBatteryVoltage, DOUBLE);
    Particle.variable("car_state", &carState, STRING);
    Particle.variable("prndb", &prndb, STRING);
    Particle.variable("odo_km", &odoKm, INT);
    
    Particle.variable("gps_lat", &lat, DOUBLE);
    Particle.variable("gps_lng", &lng, DOUBLE);

    Particle.variable("hv_soc", &hvSoc, DOUBLE);
    Particle.variable("hv_soh", &hvSoh, INT);
    Particle.variable("hv_kwh", &hvKwh, DOUBLE);
    Particle.variable("hv_temp_c", &hvTempC, DOUBLE);
    Particle.variable("range_km", &range, INT);
    Particle.variable("dc_qc", &dcqc, INT);
    Particle.variable("l2_l1", &l2l1, INT);
    
    Particle.variable("lights", &lights, INT);
    Particle.variable("doors", &doors, INT);
    Particle.variable("locked", &isLocked, INT);
    
    // allow remote query of on board computer systems
    Particle.function("refresh", refreshCommand);
    
    // add CANBUS filters for just the messages we care about
    carloop.begin();
    carloop.can().addFilter(0x5b3, 0x7ff);
    carloop.can().addFilter(0x5c5, 0x7ff);
    carloop.can().addFilter(0x510, 0x7ff);
    carloop.can().addFilter(0x50d, 0x7ff);
    carloop.can().addFilter(0x421, 0x7ff);
    carloop.can().addFilter(0x5a9, 0x7ff);
    carloop.can().addFilter(0x5bf, 0x7ff);
    carloop.can().addFilter(0x79a, 0x7ff);
    carloop.can().addFilter(0x7bb, 0x7ff);
    carloop.can().addFilter(0x793, 0x7ff);
    carloop.can().addFilter(0x60d, 0x7ff);
    carloop.can().addFilter(0x625, 0x7ff);
}

void loop() {
    uptime = System.uptime();
    
    #if Wiring_WiFi
        WiFiSignal sig = WiFi.RSSI();
        strength = (double)sig.getQuality();
        quality = (double)sig.getQuality();
    #elif Wiring_Cellular
        CellularSignal sig = Cellular.RSSI();
        strength = (double)sig.getStrength();
        quality = (double)sig.getQuality();
    #endif
    
    bool wasConnected = isConnected;
    isConnected = Particle.connected();
    
    if (!wasConnected && isConnected) {
        publishEvent("connected", String(uptime));
        lastConnected = uptime;
    }
    if (isConnected) {
        connectedUptime = uptime - lastConnected;
    }
    else {
        lastConnected = -1;
        connectedUptime = -1;
    }
    
    carloop.update();
    accBatteryVoltage = carloop.battery();
    
    // automatically poll car systems when stopped or off
    if (isAllowedSend() && isAwake) {
        // and when charging or car is on
        if (isCharging || carState == "ON") {
            autoPoll();
        }
    }
    
    String previousCarState = carState;
    String previousPrndb = prndb;
    double previousSoc = hvSoc;
    
    // process any can messages
    int received = receive();
    if (received > 0) {
        lastMessage = Time.now();
    }
    
    // actively query controllers while car is stopped
    checkAwake();
    
    // update gps location
    updateLocation(!wasConnected && isConnected
        || (previousCarState != carState && carState != "ACC")); 
    
    // publish SoC when car turns on or off
    if (hvSoc > 0 && previousCarState != carState && carState != "ACC") { 
        publishEvent("soc", String(hvSoc));
    }
    
    if (SERIAL_DEBUG) {
        publishSerial();
    }
	delay(100);
}

void publishEvent(String event, String params) {
    if (Particle.connected()) {
        Particle.publish("event", event + ":" + params, 60, PRIVATE);
    }
}

void publishSerial() {
    Serial.write(27);     
    Serial.print("[2J");   
    Serial.write(27);
    Serial.print("[H"); 
    
    Serial.printlnf("Uptime %ds", uptime);
    Serial.printlnf("Connected %ds", connectedUptime);
    Serial.printlnf("Strength %f", strength);
    Serial.printlnf("Quality %f", quality);
    Serial.printlnf("Accessory Voltage %fV", accBatteryVoltage);
    Serial.printlnf("Car State %s", (const char *)carState);
    Serial.printlnf("PRNDB %s", (const char *)prndb);
    Serial.printlnf("HV SOC %f", hvSoc);
    Serial.printlnf("HV SOH %d", hvSoh);
    Serial.printlnf("HV Energy %fkWh", hvKwh);
    Serial.printlnf("HV Voltage %fV", hvV);
    Serial.printlnf("HV Capacity %fAh", hvAh);
    Serial.printlnf("HV hx %f", hvHx);
    Serial.printlnf("HV Temperature %fC", hvTempC);
    Serial.printlnf("Internal Temperature %dC", insideTempC);
    Serial.printlnf("External Temperature %dC", outsideTempC);
    Serial.printlnf("Odometer %dkm", odoKm);
    Serial.printlnf("Range %dkm", range);
    Serial.printlnf("Fast Charges %d", dcqc);
    Serial.printlnf("Slow Charges %d", l2l1);
    Serial.printlnf("Lights %d", lights);
    Serial.printlnf("Doors %d", doors);
    Serial.printlnf("Locked %d", isLocked);
}

void updateLocation(bool forceSend) {
    // log gps coords
    WITH_LOCK(carloop.gps()) {
        if (carloop.gps().location.isValid()) {
            String previousCarState = carState;
            String previousPrndb = prndb;
    
            lat = carloop.gps().location.lat();
            lng = carloop.gps().location.lng();
            
            if (!hasLocation || forceSend) {
                publishEvent("location", String(lat) + ", " + String(lng));
            }
            hasLocation = true;
        }
        else {
            hasLocation = false;
        }
    } 
}

int receive() {
    // process incoming messages with the correct function
    CANMessage message;
    int received = 0;
    while(carloop.can().receive(message)) {
        received += 1;
        switch (message.id) {
            case 0x5b3:
                parse5b3(message.data);
                break;
            case 0x5c5:
                parse5c5(message.data);
                break;
            case 0x510:
                parse510(message.data);
                break;
            case 0x50d:
                parse50d(message.data);
                break;
            case 0x421:
                parse421(message.data);
                break;
            case 0x5a9:
                parse5A9(message.data);
                break;
            case 0x7bb:
                parse7bb(message.data);
                break;
            case 0x79a:
                parse79a(message.data);
                break;
            case 0x793:
                parse793(message.data);
                break;
            case 0x625:
                parse625(message.data);
                break;
            case 0x60d:
                parse60d(message.data);
                break;
            default:
                break;
        }
    }
    return received;
}

void parse50d(unsigned char* data) {
    if (data[0] == 0xFE) {
        return;
    }
    
    double soc = (double)data[0];
    if (soc > 0 && soc != hvSoc) {
        hvSoc = soc;
        hasDashboardSoc = true;
    }
}

void parse5b3(unsigned char* data) {
    if (data[1] == 0x00) {
        return;
    }
    
    hvTempC = data[0] * 0.25;
    hvSoh = (data[1] & 0xFE) >> 1;
    hvGids = (data[4] & 1) * 0x100 + data[5];
    hvKwh = hvGids * 0.0775;
}

void parse5c5(unsigned char* data) {
    int km = (data[1] << 16) | (data[2] << 8) | data[3];
    if (km != odoKm) {
        odoKm = km;
    }
}

void parse510(unsigned char* data) {
    int temperatureC = round(data[7] / 2.0 - 42);  // may not be correct conversion
    if (temperatureC < 80 
        && temperatureC != outsideTempC) {
        outsideTempC = temperatureC;
    }
}

void parse5A9(unsigned char* data) {
    if (data[1] == 0xFF) {
        return;
    }
    int prevRange = range;
    int prevRangeAcDiff = rangeAcDifference;
    
    range = ((data[1] << 4) | (data[2] >> 4)) / 5;
    
    int rangeDiff = ~data[0];
    if (rangeDiff & 0x0080) {
        rangeAcDifference = 0x7F & rangeDiff;
    }
    else {
        rangeAcDifference = 0x80 | rangeDiff;
    }
}

void parse7bb(unsigned char* data) {
    // response from LBC 79b
    int group = data[0] & 0x0F;
    switch (group) {
        case 0x03:
            hvV = ((data[1] << 8) | data[2]) * 0.01;
            accV = ((data[3] << 8) | data[4]) / 1024;
            break;
        case 0x04:
            hvHx = ((data[2] << 8) | data[3]) * 0.01;
            hvFullSoc = (((data[5] << 16) | data[6] << 8) | data[7]) * 0.0001;
            if (!hasDashboardSoc) {
                hvSoc = hvFullSoc;
            }
            break;
        case 0x05:
            hvAh = ((data[2] << 16) | (data[3] << 8) | data[4]) * 0.0001;
            break;
        default:
            break;
    }
    if (isAllowedSend()) {
        pollLbc(true);  // query next page
    }
}

void parse793(unsigned char* data) {
    // response from OBC 792   --  this may or may not actually work
    int group1 = data[2];
    int group2 = data[3];
    int wasCharging = isCharging;
    switch (group1) {
            switch(group2) {
                case 0x42:
                    isChargingAc = data[4] != 0x00; // is charging?
                    isCharging = isChargingAc || isChargingQc;
                    if (wasCharging != isCharging) {
                        if (isCharging) {
                            publishEvent("charge", "ac");
                        }
                        else {
                            publishEvent("charge", "stop");
                        }
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

void parse79a(unsigned char* data) {
    // response from VCM 797 - does not seem to work on MY 2012
    int group1 = data[2];
    int group2 = data[3];
    double tempC;
    int prevDcqc = dcqc;
    int prevL1l2 = l2l1;
    switch (group1) {
        case 0x11:
            switch (group2) {
                case 0x5d:
                    tempC = floor(((data[4] - 41) - 32) / 1.8);  // there may be an issue with this conversion
                    if (tempC != insideTempC
                        && tempC < 80) {
                        insideTempC = tempC;
                    }
                    break;
                default:
                    break;
            }
        case 0x12: 
            switch (group2) {
                case 0x03:
                    dcqc = data[5];
                    if (dcqc == prevDcqc + 1) {
                        publishEvent("dc_qc", String(dcqc));
                    }
                    break;
                case 0x05:    
                    l2l1 = (data[4] << 8) | data[5];
                    if (l2l1 == prevL1l2 + 1) {
                        // seems to increment when charge begins (not just when the cable is connected)
                        publishEvent("l2_lq", String(l2l1));
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

void parse60d(unsigned char* data) {
    int prevDoor = doors;
    int prevDoorR = doorR;
    int prevDoorRR = doorRR;
    int prevDoorRL = doorRL;
    int prevDoorFR = doorFR;
    int prevDoorFL = doorFL;
    doors = data[0];
    doorR = (data[0] & 0x80) != 0x00;
    doorRR = (data[0] & 0x40) != 0x00;
    doorRL = (data[0] & 0x20) != 0x00;
    doorFR = (data[0] & 0x10) != 0x00;
    doorFL = (data[0] & 0x08) != 0x00;
    if (doorR != prevDoorR
        || doorRR != prevDoorRR
        || doorRL != prevDoorRL
        || doorFR != prevDoorFR
        || doorFL != prevDoorFL) {
        publishEvent("doors", String(doorR || doorRR || doorRL || doorFR|| doorFL));
    }
    
    int prevLocked = isLocked;
    isLocked = (data[2] & 0x18) == 0x18;
    if (isLocked != prevLocked) {
        publishEvent("locked", String(isLocked));
    }

    String prevState = carState;
    switch ((data[1] >> 1) & 3) {
        case 0:
            carState = "OFF";
            break;
        case 1:
            carState = "ACC";
            break;
        case 2:
        case 3:
            carState = "ON";
            break;
        default:
            break;
    }
    if (carState != prevState) {
        publishEvent("state", String(carState));
    }
}

void parse625(unsigned char* data) {
    int prevHeadlightsOn = headlightsOn;
    int prevParkingLightsOn = parkingLightsOn;
    int prevFogLightsOn = fogLightsOn;

    lights = data[1];
    switch(lights) {  // this needs more research
        case 0x40:
            headlightsOn = 0;
            parkingLightsOn = 1;
            fogLightsOn = 0;
            break;
        case 0x60:
            headlightsOn = 1;
            parkingLightsOn = 1;
            fogLightsOn = 0;
            break;
        case 0x68:
            headlightsOn = 1;
            parkingLightsOn = 1;
            fogLightsOn = 1;
            break;
        case 0x00:
        default:
            headlightsOn = 0;
            parkingLightsOn = 0;
            fogLightsOn = 0;
            break;
    }
    
    if (prevHeadlightsOn != headlightsOn
        || prevParkingLightsOn != parkingLightsOn
        || prevFogLightsOn != fogLightsOn) {
        publishEvent("lights", String(headlightsOn || parkingLightsOn || fogLightsOn));
    }
}

void parse421(unsigned char* data) {
    String prevPrndb = prndb;
    switch(data[0]) {
        case 0x08:
            prndb = "P";
            break;
        case 0x10:
            prndb = "R";
            break;
        case 0x18:
            prndb = "N";
            break;
        case 0x20:
            prndb = "D";
            break;
        case 0x38:
            prndb = "B";  // this also means ECO on MY 2012
            break;
        default:
            break;
    }
    if (prevPrndb != prndb) {
        publishEvent("prndb", String(prndb));
    }
}

bool isAllowedSend() {
    // only allow sending of messages when car is stopped or off
    return prndb == "P" || carState == "OFF";
}

void checkAwake() {
    // set as not awake if no messages rececived for a while
    int wasAwake = isAwake;
    int currentTime = Time.now();
    isAwake = currentTime - lastMessage < awakeTimeout;
    if (!wasAwake && isAwake) {
        lastWakeUp = currentTime;
        autoPoll();  // attempt to query controllers for info
    }
}

void autoPoll() {
    // issue commands only when stopped
    int currentTime = Time.now();
    if (currentTime - lastPoll >= pollInterval) {
        lastPoll = currentTime;
        issuePollLbc = true;
        issuePollObc = true;
        issuePollVcm = true;
    }
    
    if (issuePollLbc) {
        pollLbc(false);
    }   
    if (issuePollObc) {
        pollObc(false);
    }   
    if (issuePollVcm) {
        pollVcm(false);
    }
}

int refreshCommand(String command) {
    issuePollLbc = true;
    issuePollObc = true;
    issuePollVcm = true;
    return 1;
}

void pollLbc(bool nextPage) {
    issuePollLbc = false;
    
    CANMessage message;
    message.id = 0x79b; 
    message.len = 8;
    message.rtr = false;
    message.extended = false;
    
    if (!nextPage) {
        message.data[0] = 0x02;
        message.data[1] = 0x21;
        message.data[2] = 0x01;
        message.data[3] = 0x00;
        message.data[4] = 0x00;
        message.data[5] = 0x00;
        message.data[6] = 0x00;
        message.data[7] = 0x00;
    }
    else {
        message.data[0] = 0x30;
        message.data[1] = 0x01;
        message.data[2] = 0x00;
        message.data[3] = 0x00;
        message.data[4] = 0x00;
        message.data[5] = 0x00; 
        message.data[6] = 0x00;
        message.data[7] = 0x00;
    }
    
    carloop.can().transmit(message);
}

void pollObc(bool nextPage) {
    issuePollObc = false;
    int interval = 10;
    
    CANMessage message;
    message.id = 0x792; 
    message.len = 8;
    message.rtr = false;
    message.extended = false;
    
    // check if charging
    message.data[0] = 0x03;
    message.data[1] = 0x22;
    message.data[2] = 0x11;
    message.data[3] = 0x42;
    message.data[4] = 0x00;
    message.data[5] = 0x00;
    message.data[6] = 0x00;
    message.data[7] = 0x00;
    carloop.can().transmit(message);
    
    delay(interval);
}

void pollVcm(bool nextPage) {
    issuePollVcm = false;
    int interval = 10;
    
    CANMessage message;
    message.id = 0x797; 
    message.len = 8;
    message.rtr = false;
    message.extended = false;
    
    // temperatures
    message.data[0] = 0x03;
    message.data[1] = 0x22;
    message.data[2] = 0x11;
    message.data[3] = 0x5d;
    message.data[4] = 0x00;
    message.data[5] = 0x00;
    message.data[6] = 0x00;
    message.data[7] = 0x00;
    carloop.can().transmit(message);

    delay(interval);
    
    // quick charge counter
    message.data[0] = 0x03;
    message.data[1] = 0x22;
    message.data[2] = 0x12;
    message.data[3] = 0x03;
    message.data[4] = 0x00;
    message.data[5] = 0x00;
    message.data[6] = 0x00;
    message.data[7] = 0x00;
    carloop.can().transmit(message);

    delay(interval);
    
    // l2l1 charge counter
    message.data[0] = 0x03;
    message.data[1] = 0x22;
    message.data[2] = 0x12;
    message.data[3] = 0x05;
    message.data[4] = 0x00;
    message.data[5] = 0x00;
    message.data[6] = 0x00;
    message.data[7] = 0x00;
    carloop.can().transmit(message);
}
