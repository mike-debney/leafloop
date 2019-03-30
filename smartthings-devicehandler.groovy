/**
 * LEAFLoop device handler
 *
 * Copyright 2019 Mike Debney
 * Distributed under the MIT license.
 */
 
metadata {
    definition (name: "LEAFLoop Device", namespace: "mikedebney", author: "Mike Debney", category: "C2") 
    {
        capability "Battery"
        capability "Energy Meter"
        capability "Presence Sensor"
        capability "Refresh"
        capability "Temperature Measurement"
        capability "Voltage Measurement"
        capability "Lock"
        capability "Switch"
        capability "Contact Sensor"
        
        attribute "hvDisplay", "string"
        attribute "presenceDisplay", "string"
        attribute "state", "string"
        attribute "prndb", "string"
        attribute "soh", "number"
        attribute "range", "number"
        attribute "odometer", "number"
        attribute "latitude", "number"
        attribute "longitude", "number"
        attribute "location", "string"
        attribute "l2l1", "number"
        attribute "dcqc", "number"
        attribute "signalStrength", "number"
	}

    tiles(scale: 2) {
        multiAttributeTile(name:"battery", type:"generic", width: 6, height: 2, canChangeIcon: true) {
            tileAttribute("device.battery", key: "PRIMARY_CONTROL") {
                attributeState "default", label:'${currentValue}%', icon: 'st.Transportation.transportation6', backgroundColors:[
					[value: 80, color: "#153591"],
					[value: 70, color: "#1e9cbb"],
					[value: 60, color: "#90d2a7"],
					[value: 50, color: "#44b621"],
					[value: 40, color: "#f1d801"],
					[value: 30, color: "#d04e00"],
                    [value: 20, color: "#bc2323"]
				]
            }
            tileAttribute("hvDisplay", key: "SECONDARY_CONTROL") {
    			attributeState("default", label:'${currentValue}', icon: "https://raw.githubusercontent.com/bspranger/Xiaomi/master/images/XiaomiBattery.png")
		   	}
        }

        valueTile("lock", "device.lock", width: 2, height: 1) {
            state "unlocked", label:'', icon:"http://cdn.device-icons.smartthings.com/tesla/tesla-unlocked@2x.png"
            state "locked", label:'', icon:"http://cdn.device-icons.smartthings.com/tesla/tesla-locked@2x.png"
		}
        
        valueTile("contact", "device.contact", width: 2, height: 1) {
            state "closed", label:'', icon:"st.bmw.doors-none-open"
            state "open", label:'', icon:"st.bmw.doors-RF-LF-open"
		}
        
        valueTile("switch", "device.switch", width: 2, height: 1) {
            state "off", label:'', icon:"st.switches.light.off"
            state "on", label:'', icon:"st.switches.light.on"
		}                     
        
        valueTile("temperature", "device.temperature", width: 2, height: 1) {
			state("default", label:'${currentValue}°')
		}
        
        valueTile("signalStrength", "signalStrength", width: 2, height: 1) {
			state "default", label:'${currentValue}%\nsignal'
		}
             
        standardTile("refresh", "device.refresh", inactiveLabel: false, decoration: "flat", width: 2, height: 2) {
            state "default", label:"", action:"refresh.refresh", icon:"st.secondary.refresh"
        }  
 
        valueTile("voltage", "device.voltage", width: 2, height: 1) {
			state "voltage", label:'${currentValue}V\nacc'
		}
        
        valueTile("odometer", "odometer", width: 2, height: 1) {
			state "default", label:'${currentValue}km\nodometer'
		}      
        
   		valueTile("presenceDisplay", "presenceDisplay", width: 2, height: 1) {
            state "default", label:'${currentValue}'
        }
                
        valueTile("location", "location", width: 4, height: 1) {
			state "default", label:'${currentValue}'
		}
        
        valueTile("l2l1", "l2l1", width: 2, height: 1) {
			state "default", label:'${currentValue}\nslow charges'
		}
        
        valueTile("dcqc", "dcqc", width: 2, height: 1) {
			state "default", label:'${currentValue}\nfast charges'
		}
        
        valueTile("soh", "soh", width: 2, height: 1) {
			state "default", label:'${currentValue}%\nhealth'
		}
        
        main(["battery"])
 	}
}

def refresh() {
	log.debug "Executing refresh"
    parent.pollChildDevice(device)
    
    unschedule()
}

def updateAccV(value) {
    def voltage = value.doubleValue()
    sendEvent(name: "voltage", value: voltage, unit: 'V', displayed: false, descriptionText: "Accessory battery is ${voltage}V")
    state.voltage = value
}

def updateHvSoc(value) {
   	sendEvent(name: "battery", value: value, unit: '%', displayed: true, descriptionText: "HV battery has ${value}% charge")
    state.battery = value
}

def updateHvSoh(value) {
    sendEvent(name: "soh", value: value, unit: '%', displayed: true, descriptionText: "HV battery has ${value}% health")
    state.hv_soh = value
}

def updateHvKwh(value) {
    def kwh = value.doubleValue()
    sendEvent(name: "energy", value: kwh, unit: 'kWh', displayed: true, descriptionText: "HV battery has ${kwh}kWh")
    state.energy = value
    refreshOverview() 
}

def updateHvTemp(value) {
 	def temp = value.toInteger()
   	sendEvent(name: "temperature", value: temp, unit: '°', displayed: true, descriptionText: "HV battery is ${value}°")
    state.hv_temp = value
}

def updateOdoKm(value) {
    sendEvent(name: "odometer", value: value, unit: 'km', displayed: true, descriptionText: "Odometer is ${value}km")
    state.odo_km = value
}

def updateRangeKm(value) {
    sendEvent(name: "range", value: value, unit: 'km', displayed: false, descriptionText: "Estimated range is ${value}km")
    state.range = value
    refreshOverview() 
}

def updateDoors(value) {
	def doors_value = value.toInteger()
    def doors_open = "closed";
    if (doors_value == 1) {
        doors_open = "open";
    }
    sendEvent(name: "contact", value: doors_open, displayed: true, descriptionText: "Doors are ${doors_open}")
    state.doors = value
}

def updateLights(value) {
    def lights_on = "off";
    def lights_value = value.toInteger()
    if (lights_value == 1) {
        lights_on = "on";
    }
    sendEvent(name: "switch", value: lights_on, displayed: true, descriptionText: "Lights are ${lights_on}")
    state.lights = value
}

def updateLocked(value) {
    def locked = "unknown"
    def locked_value = value.toInteger()
    if (locked_value == 0x01) {
        locked = "locked"
    }
    else if (locked_value == 0x00) {
        locked = "unlocked"
    }
    sendEvent(name: "lock", value: locked, displayed: true, descriptionText: "Car is ${locked}")
    state.locked = value
}

def updateLatitude(value) {
	if (value != 0) {
    	def latitude = value.doubleValue()
        sendEvent(name: "latitude", value: latitude, displayed: false)
        state.latitude = latitude
        unschedule()
    	runIn(1, refreshPresence, [overwrite: true])
    }
}

def updateLongitude(value) {
	if (value != 0) {
    	def longitude = value.doubleValue()
        sendEvent(name: "longitude", value: longitude, displayed: false)
        state.longitude = longitude
        unschedule()
    	runIn(1, refreshPresence, [overwrite: true])
    }
}

def updateSignalStrength(value) {
	def signal = Math.round(value.doubleValue() * 10) / 10
    sendEvent(name: "signalStrength", value: signal, displayed: true, descriptionText: "Signal strength is ${signal}%")
    state.signalStrengh = signal
}

def updateState(value) {
    sendEvent(name: "state", value: value, displayed: true, descriptionText: "Car is ${value}")
    state.state = value
}

def updatePrndb(value) {
    sendEvent(name: "prndb", value: value, displayed: true, descriptionText: "Car is in ${value}")
    state.prndb = value
    refreshOverview() 
}

def updateL2l1(value) {
	def count = value.toInteger()
    sendEvent(name: "l2l1", value: count, displayed: true, descriptionText: "${count} slow charges")
    state.l2l1 = count
}

def updateDcqc(value) {
	def count = value.toInteger()
    sendEvent(name: "dcqc", value: count, displayed: true, descriptionText: "${count} fast charges")
    state.dcqc = count
}

def refreshOverview() {
    if (state.energy != null && state.range != null && state.prndb != null) {
        def hvDisplay = (Math.round(state.energy * 100) / 100) + "kWh         " + state.range + "km                       " + state.prndb
   		sendEvent(name: "hvDisplay", value: hvDisplay, displayed: false)
   	}
}

def refreshPresence() {
	if (state.latitude != null && state.latitude != 0 && state.longitude != null && state.longitude != 0) {
    	def location = "${state.latitude}, ${state.longitude}"
    	sendEvent(name: "location", value: location, displayed: true, descriptionText: "Coordinates are ${location}")
        
        state.is_present = parent.resolveGeofence(state.latitude, state.longitude)
        log.debug "present: ${state.is_present}"

        def present = "not present"
        if (state.is_present) {
            present = "present"
        }
        sendEvent(name: "presence", value: present, displayed: true, descriptionText: "Is ${present}")
        sendEvent(name: "presenceDisplay", value: present, displayed: false)
    }
}

def parseState(event, value) {
	log.info "Updating ${event}: ${value}"
    
    if (value != null) {
        switch(event) {
        	case 'acc_v':
                updateAccV(value);
                break;
            case 'signal_strength':
                updateSignalStrength(value);
                break;
            case 'hv_soc':
                updateHvSoc(value);
                break;
            case 'hv_soh':
                updateHvSoh(value);
                break;
            case 'hv_kwh':
                updateHvKwh(value);
                break;
            case 'hv_temp_c':
                updateHvTemp(value);
                break;
            case 'odo_km':
                updateOdoKm(value);
                break;
            case 'odo_km':
                updateOdoKm(value);
                break;
            case 'range_km':
                updateRangeKm(value);
                break;
            case 'doors':
                updateDoors(value);
                break;
            case 'lights':
                updateLights(value);
                break;
            case 'locked':
                updateLocked(value);
                break;
            case 'car_state':
                updateState(value);
                break;
            case 'prndb':
                updatePrndb(value);
                break;
            case 'dc_qc':
                updateDcqc(value);
                break;
            case 'l2_l1':
                updateL2l1(value);
                break;
            case 'gps_lat':
                updateLatitude(value);
                break;
            case 'gps_lng':
                updateLongitude(value);
                break;
            case 'location':
            	def coords = value.split(', ')
            	updateLatitude(coords[0]);
                updateLongitude(coords[1]);
                unschedule()
    			refreshPresence()
                break;
        }
    }
}
