import repo: lambda.sync.repository
import history: lambda.sync.history
import model: lambda.sync.document_state
import session: lambda.sync.session
import protocol: lambda.sync.protocol
import text: lambda.sync.text
import schema: lambda.sync.schema
import crypto: lambda.io.crypto
import sqlite: lambda.io.sqlite

pn main() {
    let area = "temp/sync-retention-gc-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let path = area ++ "/repo.sqlite"
    let keys = repo.keys(crypto.key()^,"retention-gc",path)^
    let original = model.create(keys.space,["file",1],'text','text',"a")^
    let edited = session.save(session.update(original.session,text.insert(original.session.live,"editor",1,"!")^)^)^
    let intent = session.prepare_save(edited)^
    let stamped = session.stamp_version(intent.saved,1000,"save:once")^
    let saved = session.commit_save(edited,{*:intent,saved:stamped})^
    let record = {*:original,session:saved,endpoint:protocol.replace_replica(original.endpoint,saved.live)^}
    var result = []
    open tx = (sqlite.create(path)^) {
        repo.initialize(tx,keys)^
        let stored = history.flush(tx,keys,record,2000)^
        let reopened = history.expand(tx,keys,history.pack(stored))^
        let headers = history.headers(tx,keys,stored.history)^
        let endpoint = history.endpoint(tx,keys,schema.lookup(headers.entries,stamped.id)^)^
        let reference = repo.content(tx,keys,b'00FEFF017F80')^
        let manifest = repo.get(tx,keys,reference.manifest,"manifest")^
        let chunk = manifest.chunks[0].id
        let exported = repo.export_objects(tx,keys,[chunk])^
        repo.remove_object(tx,keys,chunk)^
        let deferred = repo.availability(tx,keys,reference,[chunk])^
        let missing = repo.payload(tx,keys,reference) ^ { ^ }
        repo.import_objects(tx,keys,exported)^
        let bytes = repo.payload(tx,keys,reference)^
        result = [history.pack(stored).session.versions == [],len(stored.history) == 1,
            len(reopened.session.versions) == 3,session.value(reopened.session.live)^ == "a!",
            session.heads(reopened.session)^ == [stamped.id],endpoint.raw == "a!",
            endpoint.save_metadata == stamped.save_metadata,not deferred.complete,deferred.bytes == null,
            deferred.missing == [chunk],missing is error,bytes == b'00FEFF017F80']
    }
    io.delete(area)^
    return result
}
