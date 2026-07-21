function checkConfig(config) {
    for (var [property, expectedType] of Object.entries(config)) {
        console.log(property + ":" + expectedType);
    }
}

checkConfig({ offset: "number", placement: "string" });
