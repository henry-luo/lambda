import connectivity: lambda.sync.connectivity
import agent: lambda.sync.connectivity_agent
import schema: lambda.sync.cloud_schema
pn main() {
    let a = schema.digest("a")^
    let b = schema.digest("b")^
    let session = schema.digest("session")^
    let digest = schema.digest("offer")^
    let peer = schema.digest("peer")^
    let payload = {protocol:"lambda-peer-lan-1",sdp:"sdp",instance:a,target_instance:b,offer:null}
    let offer = {body:{session:session,actor:peer},payload:payload}
    let pending = {session:session,peer:peer,remote_instance:a,instance:b,offer:digest}
    let answer = {*:offer,payload:{*:payload,offer:digest}}
    let stale = connectivity.offer(offer,a) ^ { ^ }
    let wrong_offer = connectivity.offer(answer,b) ^ { ^ }
    let wrong_process = connectivity.answer({*:answer,payload:{*:answer.payload,target_instance:a}},pending) ^ { ^ }
    let wrong_digest = connectivity.answer({*:answer,payload:{*:answer.payload,offer:session}},pending) ^ { ^ }
    let wrong_session = connectivity.answer({*:answer,body:{*:answer.body,session:digest}},pending) ^ { ^ }
    let wrong_peer = connectivity.answer({*:answer,body:{*:answer.body,actor:b}},pending) ^ { ^ }
    let malformed = connectivity.description({*:payload,protocol:"unknown"}) ^ { ^ }
    let unknown = agent.run("",null,null,{unknown:true}) ^ { ^ }
    let zero = agent.run("",null,null,{establish_ms:0}) ^ { ^ }
    let schedule = agent.run("",null,null,{interval_ms:99}) ^ { ^ }
    return [connectivity.offer(offer,b)^ == "sdp",connectivity.answer(answer,pending)^ == "sdp",
        stale is error,wrong_offer is error,wrong_process is error,wrong_digest is error,
        wrong_session is error,wrong_peer is error,malformed is error,unknown is error,zero is error,schedule is error]
}
