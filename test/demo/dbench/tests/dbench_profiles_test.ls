import profiles: ~~.mod_profiles

let sqlite={version:1,name:"Local",engine:"sqlite",path:"data/book.sqlite",password:"do-not-save"}
let network={version:1,name:"Production",engine:"postgresql",host:"db.example",port:5432,
    database:"sales / west",user:"a@b",tls:"verify-full",ca:"certs/root CA.pem",password:"do-not-save"}
let mysql={*:network,engine:"mysql",host:"[::1]",port:3306,ca:""}
let encoded=profiles.encode(network)^
let decoded=profiles.decode(encoded)^
let bad_host=profiles.validated({*:network,host:"example@evil"}) ^ {null}
let bad_encoded_host=profiles.validated({*:network,host:"example%2fevil"}) ^ {null}
let bad_port=profiles.validated({*:network,port:70000}) ^ {null}
let bad_tls=profiles.validated({*:network,tls:"unknown"}) ^ {null}
let bad_version=profiles.validated({*:network,version:2}) ^ {null}
let missing_user=profiles.target({*:network,user:""},"secret") ^ {null}
let form=profiles.to_form(network)^
let invalid_form=profiles.from_form({*:form,port:"not-a-port"}) ^ {null};
[
    profiles.target(sqlite)=="data/book.sqlite",
    not contains(encoded,"do-not-save") and decoded.password==null,
    decoded.name=="Production" and decoded.tls=="verify-full",
    profiles.target(network,"p@ss:/") == "postgresql://a%40b:p%40ss%3A%2F@db.example:5432/sales%20%2F%20west?sslrootcert=certs%2Froot%20CA.pem",
    profiles.target(mysql,"")=="mysql://a%40b@[::1]:3306/sales%20%2F%20west",
    bad_host==null and bad_encoded_host==null and bad_port==null and bad_tls==null and bad_version==null and missing_user==null,
    contains(profiles.target({*:mysql,host:"[fe80::1%en0]"},"")^,"[fe80::1%25en0]"),
    form.port=="5432" and profiles.from_form(form)==decoded and invalid_form==null
]
