var requestCount = 0;
var locationOptions = { "timeout": 15000, "maximumAge": 60000 }; // Wait 15s for pos to return. Cache pos for 60s.


function sendTimezoneToWatch() {
  // Send number of seconds to add to convert localtime to utc
  var offsetMinutes = new Date().getTimezoneOffset() * 60;
  Pebble.sendAppMessage({ "timezoneOffset": offsetMinutes });
}


function fetchWeather(latitude, longitude) {
    // Find the weather from openweathermap.org given latitude and longitude
    var req = new XMLHttpRequest();
    req.open('GET', "http://api.openweathermap.org/data/2.5/weather?" +
             "lat=" + latitude + "&lon=" + longitude + "&cnt=1", true);
    req.onload = function(e) {
        if (req.readyState == 4) {
            if(req.status == 200) {
                requestCount = requestCount + 1;
                console.log(req.responseText);
                console.log(requestCount);
                var response = JSON.parse(req.responseText);
                var temperature = Math.round(response.main.temp - 273.15);  //celsius
                var longitude = response.coord.lon;
                var latitude = response.coord.lat;
                var sunrise = response.sys.sunrise;
                var sunset = response.sys.sunset;
                console.log("Data found: Sending to Pebble.");
                Pebble.sendAppMessage({
                    "temperature":temperature + "\u00B0C",
                    "longitude":longitude.toString(), 
                    "latitude":latitude.toString(),
                    "sunrise":sunrise,
                    "sunset":sunset}
                );
            } else { 
                console.log("Error"); 
            }
        }
    };
    req.send(null);
}


function locationSuccess(pos) {
    // execute if the location is found
    var coordinates = pos.coords;
    fetchWeather(coordinates.latitude, coordinates.longitude);
}


function locationError(err) {
    // execute if there is an error getting location
    console.warn('location error (' + err.code + '): ' + err.message);
    Pebble.sendAppMessage({
        "temperature":"N/A",
        "longitude":"N/A",
        "latitude":"N/A",
        "sunrise":"N/A",
        "sunset":"N/A"}
    );
}


Pebble.addEventListener("ready",
    // "ready" is the first event received when the file is first loaded.
    function(e) {
        console.log("connect!" + e.ready);
        sendTimezoneToWatch();
        window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
        console.log(e.type);
    }
);


Pebble.addEventListener("appmessage",
    // "appmessage" is the event received when the watch wants new data.
    function(e) {
        sendTimezoneToWatch();
        window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);  
        console.log(e.type);
        console.log(e.payload.temperature);
        console.log("message!");
    }
);


Pebble.addEventListener("webviewclosed",
    function(e) {
        console.log("webview closed");
        console.log(e.type);
        console.log(e.response);
    }
);
