import repo: lambda.sync.repository
import history: lambda.sync.history
import model: lambda.sync.document_state
import session: lambda.sync.session
import protocol: lambda.sync.protocol
import text: lambda.sync.text
import core: lambda.sync.core
import codec: lambda.sync.codec
import crypto: lambda.io.crypto
import sqlite: lambda.io.sqlite

pn main() {
    let area = "temp/sync-retention-history-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let path = area ++ "/repo.sqlite"
    let keys = repo.keys(crypto.key()^,"paged-history",path)^
    let initial = model.create(keys.space,["file-id",1],'text','text',"a")^
    var editing = initial.session
    var index = 0
    while (index < 33) {
        let replica = text.insert(editing.live,"writer",len(session.value(editing.live)^)^,"x")^
        editing = session.save(session.update(editing,replica)^)^
        index = index+1
    }
    let same_frontier = session.prepare_save(editing)^
    let first = session.stamp_version(same_frontier.saved,1000,"save:a")^
    let second = session.stamp_version(same_frontier.saved,2000,"save:b")^
    session.validate_version(first)^
    session.validate_version(second)^
    editing = session.receive_version(session.receive_version(editing,first)^,second)^
    let record = {*:initial,session:editing,endpoint:protocol.replace_replica(initial.endpoint,editing.live)^}
    var results = []
    open tx = (sqlite.create(path)^) {
        repo.initialize(tx,keys)^
        let stored = history.flush(tx,keys,record,3000)^
        let catalog = history.headers(tx,keys,stored.history)^
        let page = history.page(tx,keys,stored,[],0,16)^
        let next = history.page(tx,keys,stored,[],page.next,16)^
        let end = history.page(tx,keys,stored,[],next.next,16)^
        let reopened = history.expand(tx,keys,history.pack(stored))^
        let public = model.public_state(stored)^
        let expanded = history.expand_public(tx,keys,public)^
        let merge = history.flush(tx,keys,{*:stored,history:unique([*stored.history,*catalog.referenced])},3000)^
        let expired = [catalog.entries[0].id]
        let changed = history.page(tx,keys,stored,expired,0,16)^
        results = [len(catalog.entries) == 36,len(catalog.pages) == 3,
            len(page.versions) == 16,len(next.versions) == 16,len(end.versions) == 4,end.next == null,
            history.pack(stored)^.session.versions == [],public.version == 2,
            len(codec.encode(public)^)^ < len(codec.encode(record.session.versions)^)^,
            len(reopened.session.versions) == 36,len(expanded.versions) == 36,
            all([for (header in catalog.entries) session.validate_version(history.endpoint(tx,keys,header)^)^]),
            first.id != second.id,first.frontier == second.frontier,first.raw == second.raw,
            len(session.heads(reopened.session)^) == 2,merge.history == stored.history,changed.root != page.root]
    }
    io.delete(area)^
    return results
}
