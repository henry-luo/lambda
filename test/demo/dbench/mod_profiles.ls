// S1.4v2 / D11.1.2: persist an explicit allowlist, never a connection URI or password.
fn text_field(value,limit,empty=false) bool^ => value is string and len(value)<=limit and
    (empty or value!="") and not contains(value,"\u0000")

fn valid_host(host) bool^ => text_field(host,255)^ and not contains(host,"/") and
    not contains(host,"@") and not contains(host,"?") and not contains(host,"#") and
    (if (contains(host,":")) starts_with(host,"[") and ends_with(host,"]") else
        not contains(host,"[") and not contains(host,"]") and not contains(host,"%"))

pub fn form(engine="sqlite",tls="verify-full",path="") => {name:"",engine:engine,path:path,
    host:"localhost",port:if (engine=="mysql") "3306" else "5432",database:"",user:"",tls:tls,ca:""}

pub fn from_form(value) map^ {
    let port=if (value.engine=="sqlite") 0 else (int(value.port) ^ {null});
    validated({*:value,version:1,port:port})^
}

pub fn to_form(value) map^ {
    let profile=validated(value)^;
    {*:form(profile.engine,profile.tls or "verify-full",profile.path or ""),*:profile,
        port:if (profile.engine=="sqlite") "5432" else string(profile.port)}
}

// Unknown fields are discarded so credentials cannot enter the stored record.
pub fn validated(raw) map^ {
    if (not (raw is map) or raw.version!=1 or not text_field(raw.name,128)^ or
            not (raw.engine in ["sqlite","mysql","postgresql"]))
        raise error("DBench: invalid connection profile")
    else if (raw.engine=="sqlite") {
        if (not text_field(raw.path,4096)^) raise error("DBench: invalid SQLite profile path")
        else {version:1,name:raw.name,engine:raw.engine,path:raw.path}
    } else {
        let user=raw.user or ""
        let ca=raw.ca or ""
        if (not valid_host(raw.host)^ or not (raw.port is int) or raw.port<1 or raw.port>65535 or
                not text_field(raw.database,1024)^ or not text_field(user,1024,true)^ or
                not text_field(ca,4096,true)^ or not (raw.tls in ["verify-full","require","disable"]))
            raise error("DBench: invalid network profile")
        else {version:1,name:raw.name,engine:raw.engine,host:raw.host,port:raw.port,
            database:raw.database,user:user,tls:raw.tls,ca:ca}
    }
}

pub fn encode(raw) string^ => format(validated(raw)^,'json')^
pub fn decode(text) map^ => validated(parse(text,'json')^)^

// Password is a transient input and is never copied into the profile record.
pub fn target(raw,password="") string^ {
    let profile=validated(raw)^
    if (profile.engine=="sqlite") profile.path
    else if (not text_field(password,4096,true)^ or (profile.user=="" and password!=""))
        raise error("DBench: a password requires a profile user")
    else {
        let auth=if (profile.user=="") "" else url_encode(profile.user)^ ++
            (if (password=="") "" else ":" ++ url_encode(password)^) ++ "@"
        let ca=if (profile.ca=="") "" else "?" ++
            (if (profile.engine=="mysql") "ssl-ca=" else "sslrootcert=") ++ url_encode(profile.ca)^;
        // IPv6 zone identifiers use a literal percent in the form and an escaped one in a URI.
        profile.engine ++ "://" ++ auth ++ replace(profile.host,"%","%25")^ ++ ":" ++ string(profile.port) ++ "/" ++
            url_encode(profile.database)^ ++ ca
    }
}
