{ let _sc = "inner"; }
function _sf(_sc) {
    return function() {
        return _sc;
    };
}
_sf(42)();
