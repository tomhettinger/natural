function HTTPGET(url) {
    var req = new XMLHttpRequest();
    req.open("GET", url, false);
    req.send(null);
    return req.responseText;
}


var getWeather = function() {
    // Get the weather info
    var response = HTTPGET("http://api.openweathermap.org/data/2.5/weather?q=Lansing,MI");
    // Convert to JSON
    var json = JSON.parse(response);
    // Extract the data
    var sunrise = json.sys.sunrise;  // this is an integer
    var sunset = json.sys.sunset;  // this is an integer
    var location = json.name;
    // Console the output to check all is working.
    console.log("Sunrise is " + sunrise + " and sunset is " + sunset + " in " + location);
    // Construct a key-value dictionary.
    var dict = {"KEY_SUNRISE" : sunrise, "KEY_SUNSET" : sunset}
    // Send the data to the Pebble watch.
    Pebble.sendAppMessage(dict);
};


Pebble.addEventListener("ready",
    function(e) {
        // App is read to receive JS messages
        getWeather();
    }
);

