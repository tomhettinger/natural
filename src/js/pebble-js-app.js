var lat;
var lon;
var locationOptions = { "timeout": 15000, "maximumAge": 60000 };  // Wait 15s for pos to return. Cache pos for 60s.


function hasContents(obj){
    return (Object.getOwnPropertyNames(obj).length > 0);
}


function fetchTimezone() {
    // Send number of seconds to add to convert localtime to utc
    var offsetSeconds = new Date().getTimezoneOffset() * 60;
    Pebble.sendAppMessage(
        {"timezoneOffset": offsetSeconds},
        function(e) {
            console.log("Successfully delivered message with transactionId=" + e.data.transactionId);
        },
        function(e) {
            console.log("Unable to deliver message with transactionId=" + e.data.transactionId
            + " Error is: " + e.error.message);
        }
    );
}


function fetchLocation() {
    var latitude = 42.7336;
    var longitude = -84.5467;
    Pebble.sendAppMessage(
        {"longitude":longitude.toString(),
        "latitude":latitude.toString()},
        function(e) {
            console.log("Successfully delivered message with transactionId="
            + e.data.transactionId);
        },
        function(e) {
            console.log("Unable to deliver message with transactionId="
            + e.data.transactionId
            + " Error is: " + e.error.message);
        }
    );
}


function fetchWeather_orig(latitude, longitude) {
    // Find the weather from openweathermap.org given latitude and longitude
    var req = new XMLHttpRequest();
    req.open('GET', "http://api.openweathermap.org/data/2.5/weather?" +
             "lat=" + latitude + "&lon=" + longitude + "&cnt=1", true);
    req.onload = function(e) {
        if (req.readyState == 4) {
            if(req.status == 200) {
                console.log(req.responseText);
                var response = JSON.parse(req.responseText);
                var temperature = Math.round(response.main.temp - 273.15);  //celsius
                var longitude = response.coord.lon;
                var latitude = response.coord.lat;
                var sunrise = response.sys.sunrise;
                var sunset = response.sys.sunset;
                console.log("Data found: Sending to Pebble.");

                Pebble.sendAppMessage(
                    {"temperature":temperature + "\u00B0C",
                    //"longitude":longitude.toString(),
                    //"latitude":latitude.toString(),
                    "sunrise":sunrise,
                    "sunset":sunset},
                    function(e) {
                        console.log("Successfully delivered message with transactionId="
                        + e.data.transactionId);
                    },
                    function(e) {
                        console.log("Unable to deliver message with transactionId="
                        + e.data.transactionId
                        + " Error is: " + e.error.message);
                    }
                );

            } else { 
                console.log("Error"); 
            }
        }
    };
    req.send(null);
}


function fetchWeather(latitude, longitude) {
    // Find the weather from openweathermap.org given latitude and longitude
    var req = new XMLHttpRequest();
    req.open('GET', "http://api.openweathermap.org/data/2.5/weather?" +
             "lat=" + latitude + "&lon=" + longitude + "&cnt=1", true);
    req.onload = function(e) {
        if (req.readyState == 4) {
            if(req.status == 200) {
                console.log(req.responseText);
                var response = JSON.parse(req.responseText);
                var sunrise = response.sys.sunrise;
                var sunset = response.sys.sunset;
                return {"sunrise": sunrise, 
                        "sunset": sunset};
            } else { 
                console.log("Error"); 
            }
        }
    };
    req.send(null);
}


function locationSuccess(pos) {
    console.log("location found");
    var coordinates = pos.coords;
    lat = pos.coords.latitude;
    lon = pos.coords.longitude;
}


function locationError(err) {
    console.warn('location error (' + err.code + '): ' + err.message);
}


function fetchAll() {
    var outMessage = new Object();

    // Get timezone offset
    var offsetSeconds = new Date().getTimezoneOffset() * 60;
    if (typeof offsetSeconds !== 'undefined') {
        outMessage.timezoneOffset = offsetSeconds;
    } else {
        outMessage.timezoneOffset = 0;
    }
    
    // Get location
    window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    //this executes immediately, before loc is found!
    console.log("finished checking location");
    if (typeof lon !== 'undefined' && typeof lat !== 'undefined') {
        outMessage.longitude = lon;
        outMessage.latitude = lat;

        // Get weather
        var weather = fetchWeather(lat, lon);
        var sunrise = weather.sunrise;
        var sunset = weather.sunset;
        if (typeof sunrise !== 'undefined' && typeof lat !== 'undefined') {
            outMessage.sunrise = sunrise;
            outMessage.sunset = sunset;
        }
    }

    // Send whatever results we have.  Coords should be first so that rise/set times are cleared first (if loc change).
    if (hasContents(outMessage)) {
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
        console.log("No data to send to Pebble.");
    }
}


Pebble.addEventListener("ready",
    function(e) {
        console.log("JavaScript running and ready.");
        fetchAll();
    }
);


Pebble.addEventListener("appmessage",
    // "appmessage" is the event received when the watch wants new data.
    function(e) {
        console.log("message received");
        console.log(JSON.stringify(e.payload));
        if (e.payload.req_timezone) {
            console.log("requesting timezone");
            fetchTimezone();
        }
        if (e.payload.req_weather) {
            console.log("requesting weather");
            fetchWeather_orig(42.7336, -84.5467);
        }
        if (e.payload.req_location) {
            console.log("requesting location");
            fetchLocation();
        }
        if (e.payload.req_all) {
            console.log("requesting all info");
            fetchAll();
        }
    }
);
