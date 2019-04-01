/**
 * LEAFLoop SmartApp
 *
 * Copyright 2019 Mike Debney
 * Distributed under the MIT license.
 */
 
definition(
    name: "LEAFLoop",
    namespace: "mikedebney",
    author: "Mike Debney",
    description: "Connects to api.particle.io and communicates with devices running LeafLoop firmware.",
    category: "Green Living",
    iconUrl: "https://s3.amazonaws.com/smartapp-icons/Convenience/Cat-Convenience.png",
    iconX2Url: "https://s3.amazonaws.com/smartapp-icons/Convenience/Cat-Convenience@2x.png",
    iconX3Url: "https://s3.amazonaws.com/smartapp-icons/Convenience/Cat-Convenience@2x.png") {
}

mappings {
    path("/event") {
        action: [
            POST: "handleRequest"
        ]
    }
}

preferences {
    section() {
        input "api_token", "text", title: "particle.io API token", required: true
        input "geofence_radius_m", "text", title: "Geofence radius (m)", required: true
    }
}

def installed() {
	createChildDevices()
}

def updated() {
	createChildDevices()
}

def getChildNamespace() { "mikedebney" }
def getChildTypeName() { "LEAFLoop Device" }

def createChildDevices() { 
	// request the list of devices on particle account
    httpGet([
        uri: "https://api.particle.io",
        path: "/v1/devices",
        headers: [
        	 'Authorization': "Bearer ${api_token}"
        ]
    ]) 
    { resp ->
    	log.info resp.data
    	resp.data.each { leaf ->
            // create new child device if not already added
            def child_dni = leaf.id.toString()
            def child = getChildDevice(child_dni)
            if (!child && leaf.name.toLowerCase().indexOf("leaf") > -1) {
                log.info "Adding LEAF ${leaf.name} / ${child_dni}"
                addChildDevice(getChildNamespace(), getChildTypeName(), child_dni, location.hubs[0].id, [
                    "label" : "${leaf.name}",
                    "name" : "${leaf.name}"
                ]).save()
                child = getChildDevice(child_dni)
            }
            if (child) {
            	pollChildDevice(child)
            }
        }
    }
}

def handleRequest() {
    def data = request.JSON
    log.debug "request data: ${data}"
    def child_dni = data.coreid
    def event = data.data.split(':');
    def variable = event[0]
    def value = event[1]
    def child = getChildDevice(child_dni)
    if (child) {
        child.parseState(variable, value)
    }
}

def pollChildDevice(child) {   
	def child_dni = child.deviceNetworkId
    requestVariable(child_dni, 'signal_strength')
    requestVariable(child_dni, 'acc_batt_v')
    requestVariable(child_dni, 'car_state')
    requestVariable(child_dni, 'prndb')
    requestVariable(child_dni, 'odo_km')
    requestVariable(child_dni, 'gps_lat')
    requestVariable(child_dni, 'gps_lng')
    requestVariable(child_dni, 'hv_soc')
    requestVariable(child_dni, 'hv_soh')
    requestVariable(child_dni, 'hv_kwh')
    requestVariable(child_dni, 'hv_temp_c')
    requestVariable(child_dni, 'range_km')
    requestVariable(child_dni, 'dc_qc')
    requestVariable(child_dni, 'l2_l1')
    requestVariable(child_dni, 'lights')
    requestVariable(child_dni, 'doors')
    requestVariable(child_dni, 'locked')
}

def requestVariable(child_dni, variable) {   
    httpGet([
        uri: "https://api.particle.io",
        path: "/v1/devices/${child_dni}/${variable}",
        headers: [
        	'Authorization': "Bearer ${api_token}"
        ]
    ]) { resp ->
        log.debug "response data: ${resp.data}"
        def data = resp.data
        def child = getChildDevice(child_dni)
        if (child) {
            child.parseState(data.name, data.result)
        }
    }
}

def resolveGeofence(lat1, lon1) {
	if (lat1 == null || lon1 == null || location.latitude == null || location.longitude == null || geofence_radius_m == null) {
    	log.error "Invalid geofence configuration"
    	return 0;
    }
    
    // use haversine to calculate distance between current location and home location   
    def R = 6371; // radius of the earth
    def lat2 = location.latitude
    def lon2 = location.longitude

    def latDistance = Math.toRadians(lat2 - lat1);
    def lonDistance = Math.toRadians(lon2 - lon1);
    def a = Math.sin(latDistance / 2) * Math.sin(latDistance / 2) + Math.cos(Math.toRadians(lat1)) * Math.cos(Math.toRadians(lat2)) * Math.sin(lonDistance / 2) * Math.sin(lonDistance / 2);
    def c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    def distance = R * c * 1000; // convert to meters

	log.debug "Geofence distance: ${distance}m"
    def is_inside = distance <= geofence_radius_m.toInteger()
    return is_inside
}
