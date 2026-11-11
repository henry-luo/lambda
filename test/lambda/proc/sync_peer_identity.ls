import identity: lambda.sync.peer_identity
import session: lambda.sync.peer_session
import crypto: lambda.io.crypto
pn main() {
    let alice = identity.create("space",crypto.key()^)^
    let bob = identity.create("unpaired",crypto.key()^)^
    let offer = identity.invite(alice,bob.device)^
    let enrolled = identity.enroll(bob,offer,alice.device.id)^
    let a = identity.trust(alice,bob.device)^
    let wrong_pin = identity.enroll(bob,offer,bob.device.id) ^ { ^ }
    let wrong_target = identity.enroll(alice,offer,alice.device.id) ^ { ^ }
    let altered = identity.enroll(bob,{*:offer,box:offer.box ++ b'00'},alice.device.id) ^ { ^ }
    let ah = session.start(a,'client')^
    let bh = session.start(enrolled,'server')^
    let ac = session.finish(a,ah,bh.hello,session.proof(enrolled,bh,ah.hello)^)^
    let bc = session.finish(enrolled,bh,ah.hello,session.proof(a,ah,bh.hello)^)^
    let encrypted = session.seal(ac,{operation:"hello",data:b'00ff'})^
    let read = session.unseal(bc,encrypted.bytes)^
    let replay = session.unseal(read.session,encrypted.bytes) ^ { ^ }
    let reflected = session.unseal(ac,encrypted.bytes) ^ { ^ }
    let tampered = session.unseal(bc,encrypted.bytes ++ b'00') ^ { ^ }
    let revoked = session.transcript(identity.revoke(a,bob.device.id),ah,bh.hello) ^ { ^ }
    let fake = session.finish(a,ah,{*:bh.hello,nonce:crypto.key()^},session.proof(enrolled,bh,ah.hello)^) ^ { ^ }
    let directory = "temp/sync-peer-identity-" ++ string(sys.proc.self.pid#)
    io.mkdir(directory)^
    let wrap = crypto.key()^
    identity.save(directory,wrap,a)^
    let restored = identity.load(directory,wrap)^
    let duplicate_create = identity.save(directory,wrap,alice) ^ { ^ }
    identity.update(directory,wrap,(current) any^ => identity.revoke(current,bob.device.id^))^
    identity.update(directory,wrap,(current) any^ => identity.trust(current,
        identity.descriptor(b'0000000000000000000000000000000000000000000000000000000000000001',
            b'0000000000000000000000000000000000000000000000000000000000000002')^)^)^
    let retained_revocation = identity.member(identity.load(directory,wrap)^,bob.device.id) ^ { ^ }
    let bad_wrap = identity.load(directory,crypto.key()^) ^ { ^ }
    io.delete(directory)^
    return [enrolled.space == a.space,enrolled.secret == a.secret,wrong_pin is error,wrong_target is error,
        altered is error,ac.id == bc.id,ac.send_key == bc.receive_key,ac.send_key != ac.receive_key,
        read.value == {operation:"hello",data:b'00ff'},replay is error,reflected is error,tampered is error,
        revoked is error,fake is error,restored == a,bad_wrap is error,
        duplicate_create is error,retained_revocation is error]
}
