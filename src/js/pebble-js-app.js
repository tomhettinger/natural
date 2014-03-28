var locationOptions = { "timeout": 15000, "maximumAge": 60000 };  // Wait 15s for pos to return. Cache pos for 60s.



function updateProgress(oEvent) {
    if (oEvent.lengthComputable) {
        var percentComplete = oEvent.loaded / oEvent.total;
        console.log("JS: update progress = " + percentComplete);
    }
}


function transferComplete(evt) {
    console.log("JS: transfer complete.");
}


function transferFailed(evt) {
    console.log("JS: transfer failed.");
}


function transferCanceled(evt) {
    console.log("JS: transfer canceled.");
}


function locationSuccess(location) {
    var req = new XMLHttpRequest();
    var url = "http://api.openweathermap.org/data/2.5/weather?" + "lat=" + location.coords.latitude + "&lon=" + location.coords.longitude + "&cnt=1"

    // Experimental:
    /* https://developer.mozilla.org/en-US/docs/Web/API/XMLHttpRequest/Using_XMLHttpRequest */
    req.ontimeout = function(e) {
        console.log("JS: Timeout on httprequest.");
    }

    req.addEventListener("progress", updateProgress, false);
    req.addEventListener("load", transferComplete, false);
    req.addEventListener("error", transferFailed, false);
    req.addEventListener("abort", transferCanceled, false);

    req.onload = function(e) {
        if(req.readyState == 4 && req.status == 200) {
            var response = JSON.parse(req.responseText);
            var sunrise = response.sys.sunrise;
            var sunset = response.sys.sunset;

            console.log("JS: Sending weather.");
            Pebble.sendAppMessage(
                {"status": "reporting",
                "sunrise": sunrise,
                "sunset": sunset,
                "tzOffset": new Date().getTimezoneOffset() * 60
                }
            );
        }

        else {
            console.log("JS: Error communicating with Open Weather Map.");
            Pebble.sendAppMessage(
                {"status": "failed",
                "tzOffset": new Date().getTimezoneOffset() * 60
                }
            );
        }
    }

    console.log("JS: Location found, getting weather...");
    req.open("GET", url, true);
    // At this point, sometimes the code never makes it to either if() case or else() case.  Why?

    req.send(null);
}


function locationError(error) {
    console.log("JS: Failed to get coords: " + error.message + "\n");
    Pebble.sendAppMessage(
        {"status": "failed",
        "tzOffset": new Date().getTimezoneOffset() * 60
        }
    );
}


function readyHandler(e) {
    console.log("JS: Ready.");
    Pebble.sendAppMessage({"status": "ready"});
}


function receivedHandler(message) {
    if(message.payload.status == "retrieve") {
        console.log("JS: Recieved status \"retrieve\", getting location...")
        window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    }
}


Pebble.addEventListener("ready", readyHandler);
Pebble.addEventListener("appmessage", receivedHandler);
