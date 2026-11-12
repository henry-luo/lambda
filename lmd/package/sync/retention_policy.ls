// policy defaults need no calendar database in ordinary sync/editor processes.
pub let permanent = {version:1,mode:'forever',timezone:"UTC",keep_all_ms:0,rules:[],
    arrival_grace_ms:86400000,delete_grace_ms:86400000,clock_skew_ms:300000}
pub fn daily(zone="UTC", arrival_grace_ms=86400000, delete_grace_ms=86400000) =>
    {*:permanent,mode:'buckets',timezone:zone,rules:[{unit:'day',count:null}],
        arrival_grace_ms:arrival_grace_ms,delete_grace_ms:delete_grace_ms}
