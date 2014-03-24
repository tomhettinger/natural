var locationOptions = { "timeout": 15000, "maximumAge": 60000 };  // Wait 15s for pos to return. Cache pos for 60s.


function fetchWeather(latitude, longitude) {
    // Find the weather from openweathermap.org given latitude and longitude

    // Create outmessage and start adding information
    var outMessage = new Object();
    var offsetSeconds = new Date().getTimezoneOffset() * 60;
    if (typeof offsetSeconds !== 'undefined') {
        outMessage.timezoneOffset = offsetSeconds;
    }
    outMessage.latitude = latitude;
    outMessage.longitude = longitude;
    
    // Try weather.
    var req = new XMLHttpRequest();
    req.open('GET', "http://api.openweathermap.org/data/2.5/weather?" +
             "lat=" + latitude + "&lon=" + longitude + "&cnt=1", true);
    req.onload = function(e) {
        if (req.readyState == 4) {
            if(req.status == 200) {
                // If successful
                console.log(req.responseText);
                var response = JSON.parse(req.responseText);                
                outMessage.sunrise = response.sys.sunrise;
                outMessage.sunset = response.sys.sunset;
                console.log("Sending data to Pebble...");
                console.log(JSON.stringify(outMessage));
                Pebble.sendAppMessage(outMessage,
                    function(e) {
                        console.log("Successfully delivered message with transactionId="
                        + e.data.transactionId);
                    },
                    function(e) {
                        console.log("Unable to deliver message with transactionId="
                        + e.data.transactionId + " Error is: " + e.error.message);
                    }
                );
            } else { 
                // If failed
                console.log("Error");
                console.log("Sending data to Pebble...");
                console.log(JSON.stringify(outMessage));
                Pebble.sendAppMessage(outMessage,
                    function(e) {
                        console.log("Successfully delivered message with transactionId="
                        + e.data.transactionId);
                    },
                    function(e) {
                        console.log("Unable to deliver message with transactionId="
                        + e.data.transactionId + " Error is: " + e.error.message);
                    }
                );
            }
        }
    };
    req.send(null);
}


function locationSuccess(pos) {
    console.log("location found");
    fetchWeather(pos.coords.latitude, pos.coords.longitude);
}


function locationError(err) {
    console.warn('location error (' + err.code + '): ' + err.message);
    // Try to get and return timezone.
    var offsetSeconds = new Date().getTimezoneOffset() * 60;
    if (typeof offsetSeconds !== 'undefined') {
        console.log("Sending data to Pebble...");
        console.log(JSON.stringify(outMessage));
        Pebble.sendAppMessage({"timezoneOffset": offsetSeconds},
            function(e) {
                console.log("Successfully delivered message with transactionId="
                + e.data.transactionId);
            },
            function(e) {
                console.log("Unable to deliver message with transactionId="
                + e.data.transactionId + " Error is: " + e.error.message);
            }
        );
    }
}


function fetchAll() {
    window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
}


Pebble.addEventListener("ready",
    function(e) {
        console.log("JavaScript running and ready.");
    }
);


Pebble.addEventListener("appmessage",
    // "appmessage" is the event received when the watch wants new data.
    function(e) {
        console.log("Request received from Pebble.");
        console.log(JSON.stringify(e.payload));
        if (e.payload.req_all) {
            console.log("requesting all info");
            fetchAll();
        }
    }
);
