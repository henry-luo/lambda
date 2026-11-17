// Physical packs and legacy loose envelopes resolve to the same logical authenticated objects.
import pack: .pack
import wire: .object_wire
import codec: .codec
import repo: .repository
import transport: .cloud_transport
pub fn decode(keys, bytes) any^ {
    let raw = binary(bytes)^
    if (pack.is_pack(raw)^) { return pack.decode(keys,raw)^ }
    if (len(raw)^ >= 4 and slice(raw,0,4)^ == binary("LRP1")^)
        { return [wire.decode(raw)^] }
    return [codec.decode(bytes)^]
}
pub fn select(keys, bytes, id) any^ {
    let found = [for (record in decode(keys,bytes)^ where record.id == id) record]
    if (len(found)^ != 1) raise error("sync cloud: pack omits logical object")
    repo.validate_record(keys,found[0])^
    return found[0]
}
pub pn read(config, credentials, locations) any^ {
    let keys = repo.keys(credentials.secret,credentials.space)^
    let paths = unique([for (entry in locations) entry.key])^
    let physical = transport.read_many(config,credentials,paths)^
    var records = []
    for (item in physical) {
        let decoded = decode(keys,item.bytes)^
        let wanted = [for (entry in locations where entry.key == item.key) entry.id]
        let selected = [for (record in decoded where contains(wanted,record.id)^) record]
        if (sort([for (record in selected) record.id])^ != sort(wanted)^)
            raise error("sync cloud: pack logical identities mismatch")
        records = [*records,*selected]
    }
    let indexed = map([for (record in records) (string(record.id),record)])
    return [for (entry in locations) indexed[string(entry.id)]]
}
