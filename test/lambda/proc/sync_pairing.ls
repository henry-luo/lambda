import pair: lambda.sync.pairing
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import connectivity: lambda.sync.connectivity
pn main() {
    let a = identity.create(pair.space("demo-user")^,crypto.key()^)^
    let b = identity.create(pair.space("demo-user")^,crypto.key()^)^
    let c = identity.create(pair.space("demo-user")^,crypto.key()^)^
    let ticket = pair.create("demo-user",1000)^
    let request = pair.hello("demo-user",b.device)^
    let challenge = pair.challenge(ticket,a.device,request,1001)^
    let response = pair.confirm("demo-user",pair.display(ticket.code)^,request,challenge)^
    let admitted = pair.admit(ticket,request,challenge,{proof:response.proof},1002)^
    let offer = identity.invite(a,admitted)^
    let enrolled = identity.enroll(b,offer,response.expected)^
    let other_ticket = pair.create("demo-user",1000)^
    let wrong_code = pair.confirm("demo-user",other_ticket.code,request,challenge) ^ { ^ }
    let wrong_user = pair.challenge(ticket,a.device,pair.hello("another-user",b.device)^,1001) ^ { ^ }
    let reflected = pair.admit(ticket,request,challenge,{proof:challenge.proof},1002) ^ { ^ }
    let fresh = pair.hello("demo-user",b.device)^
    let replay = pair.admit(ticket,fresh,challenge,{proof:response.proof},1002) ^ { ^ }
    let substituted = pair.confirm("demo-user",ticket.code,request,{*:challenge,device:c.device}) ^ { ^ }
    let expired = pair.admit(ticket,request,challenge,{proof:response.proof},ticket.deadline) ^ { ^ }
    let early = pair.challenge(ticket,a.device,request,999) ^ { ^ }
    let malformed = pair.confirm("demo-user",ticket.code,request,{*:challenge,nonce:b'00'}) ^ { ^ }
    let self_link = pair.challenge(ticket,a.device,pair.hello("demo-user",a.device)^,1001) ^ { ^ }
    let bad_id = pair.user("  ") ^ { ^ }
    let bad_code = pair.code("123456") ^ { ^ }
    let bad_hex = pair.code("zzzz-zzzz-zzzz-zzzz-zzzz") ^ { ^ }
    let overlong = pair.create("demo-user",1000,120001) ^ { ^ }
    return [admitted == b.device,enrolled.space == a.space,enrolled.secret == a.secret,
        identity.member(enrolled,a.device.id)^ == a.device,response.expected == a.device.id,
        pair.account(" demo-user ")^ == pair.account("demo-user")^,
        pair.account("Demo-user")^ != pair.account("demo-user")^,
        pair.code(upper(pair.display(ticket.code)^)^)^ == ticket.code,
        len(ticket.code)^ == 20,len(pair.name("demo-user",a.device)^)^ <= 63,
        starts_with(pair.name("demo-user",a.device)^,pair.prefix("demo-user")^)^,
        wrong_code is error,wrong_user is error,reflected is error,replay is error,
        substituted is error,expired is error,early is error,malformed is error,self_link is error,
        bad_id is error,bad_code is error,bad_hex is error,overlong is error,
        connectivity.hint_url({host:"127.0.0.1",port:1234})^ == "ws://127.0.0.1:1234/sync",
        connectivity.hint_url({host:"evil/other",port:1234})^ == null,
        connectivity.hint_url({host:"",port:1234})^ == null,
        connectivity.hint_url({host:"host.local.",port:0})^ == null]
}
