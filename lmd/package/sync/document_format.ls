// conversion is an open/save boundary; all structured edits use the Mark model.
import mark: .mark
import session: .session
import codec: .codec

pub fn validate(profile, encoding) any^ {
    if ((profile == 'text' and encoding != 'text') or
        (profile == 'mark' and not contains(['json','xml','mark'],encoding)) or
        not contains(['text','mark'],profile)) raise error("sync document: incompatible editing profile/format")
    return true
}
pub fn parse_raw(raw, profile, encoding) any^ {
    validate(profile,encoding)^
    if (not (raw is string)) raise error("sync document: expected UTF-8 source")
    return if (profile == 'text') raw else parse(raw,encoding)^
}
pub fn verify_raw(replica, encoding, raw) any^ {
    let parsed = parse_raw(raw,replica.profile,encoding)^
    let projected = if (replica.profile == 'text') parsed else mark.normalize(parsed)^
    if (codec.digest(projected)^ != codec.digest(session.value(replica)^)^)
        raise error("sync document: format conversion loses editing data")
    return true
}
pub fn export_raw(replica, encoding) any^ {
    let raw = if (replica.profile == 'text') session.value(replica)^
        else format(mark.export_value(replica)^,encoding)^
    verify_raw(replica,encoding,raw)^
    return raw
}
