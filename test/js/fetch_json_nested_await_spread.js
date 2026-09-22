// Each nested await resolves before the following member call is evaluated.
(async function() {
    const response = {
        json: function() {
            return Promise.resolve([{ current: "19.4", next: "19.5" }]);
        }
    };
    const versions = await (await Promise.resolve(response)).json();
    const version = Object.assign(...versions);
    console.log(version.current);
    console.log(version.next);
})();
