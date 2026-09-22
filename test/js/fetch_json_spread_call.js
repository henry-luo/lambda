// Response.json() returns an iterable array suitable for a spread call.
fetch("fetch_json_spread_call.json")
    .then(function(response) { return response.json(); })
    .then(function(versions) {
        const version = Object.assign(...versions);
        console.log(version.current);
        console.log(version.next);
    });
