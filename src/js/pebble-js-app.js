var locationOptions = { "timeout": 15000, "maximumAge": 60000 };  // Wait 15s for pos to return. Cache pos for 60s.


function isJSON(text) {
    return (/^[\],:{}\s]*$/.test(text.replace(/\\["\\\/bfnrtu]/g, '@').
    replace(/"[^"\\\n\r]*"|true|false|null|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?/g, ']').
    replace(/(?:^|:|,)(?:\s*\[)+/g, '')));
}


function updateProgress(evt) {
    if (evt.lengthComputable) {
        var percentComplete = evt.loaded / evt.total;
        console.log("JS: update progress = " + percentComplete);
    }
}


function transferFailed(evt) {
    console.log("JS: transfer failed.");
}


function transferCanceled(evt) {
    console.log("JS: transfer canceled.");
}


function locationSuccess(location) {
    var tzOffset = new Date().getTimezoneOffset() * 60;
    var req = new XMLHttpRequest();
    var url = "http://api.openweathermap.org/data/2.5/weather?" + "lat=" + location.coords.latitude + "&lon=" + location.coords.longitude + "&cnt=1";
    req.ontimeout = function(e) {console.log("JS: Timeout on httprequest.");}
    req.addEventListener("progress", updateProgress, false);
    req.addEventListener("error", transferFailed, false);
    req.addEventListener("abort", transferCanceled, false);
    
    req.onload = function(e) {
        console.log("JS: Transfer complete. readystate=" + req.readyState + " status=" + req.status);

        // If request is succesful.
        if (req.readyState == 4 && req.status == 200) {

            // If text is in JSON format
            if (isJSON(req.responseText)) {
                console.log("JS: Parsing text...");
                console.log(req.responseText);
                try {
                    var response = JSON.parse(req.responseText);
                } 
                catch (e) {
                    console.log("JS: Unable to convert text to JSON object.");
                    Pebble.sendAppMessage( {"status": "failed", "tzOffset": tzOffset} );
                    return;
                }
                var temperature = Math.round(response.main.temp - 273.15);
                var sunrise = response.sys.sunrise;
                var sunset = response.sys.sunset;
                console.log("JS: Sending weather...");
                Pebble.sendAppMessage( {"status": "reporting", "sunrise": sunrise, "sunset": sunset, "temperature": temperature, "tzOffset": tzOffset} );
            }

            else {
                console.log('JS: Response text is not in JSON format.');
                Pebble.sendAppMessage( {"status": "failed", "tzOffset": tzOffset} );
            }
        }

        else {
            console.log("JS: Error communicating with Open Weather Map.");
            Pebble.sendAppMessage( {"status": "failed", "tzOffset": tzOffset} );
        }
    }

    console.log("JS: Location found, getting weather...");
    req.open("GET", url, true);
    req.send(null);
}


function locationError(error) {
    console.log("JS: Failed to get coords: " + error.message + "\n");
    var tzOffset = new Date().getTimezoneOffset() * 60;
    Pebble.sendAppMessage( {"status": "failed", "tzOffset": tzOffset} );
}


function readyHandler(e) {
    console.log("JS: Ready.");
    Pebble.sendAppMessage( {"status": "ready"} );
}


function receivedHandler(message) {
    if(message.payload.status == "retrieve") {
        console.log("JS: Recieved status \"retrieve\", getting location...");
        window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    }
}


Pebble.addEventListener("ready", readyHandler);
Pebble.addEventListener("appmessage", receivedHandler);
