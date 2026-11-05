// S13.3.1v2/S13.3.2: one document-owned task owns each session and result.
import rdb: lambda.io.rdb
import exports: .mod_export
import model: .mod_model

pn read_window(session,request) array^ {
    if ((request.count<1 or request.count>model.MAX_ROWS)^) { raise error("DBench: invalid result window") }
    var rows=[]
    while ((len(rows)<request.count)^) {
        let batch=rdb.spool_read(session,request.first+len(rows),min(256,request.count-len(rows)))^
        if ((len(batch)==0)^) { raise error("DBench: incomplete result window") }
        rows=[*rows,*batch]
        if ((model.result_size(rows)>model.MAX_BYTES)^) { raise error("DBench: result window exceeds the cache limit") }
        if ((len(rows)<request.count)^) sleep(1)^
    }
    return rows
}

pn copy_spooled(session,request) string^ {
    let cells=(request.bottom-request.top+1)*(request.right-request.left+1)
    if ((cells>100000 or cells<=0)^) { raise error("DBench: selection is too large to copy") }
    var offset=request.top
    var chunks=[]
    var bytes=0
    while ((offset<=request.bottom)^) {
        let rows=rdb.spool_read(session,offset,min(64,request.bottom-offset+1))^
        let chunk=model.copy_rows(rows,request.left,request.right)^
        bytes=bytes+4*len(chunk)+1
        if ((bytes>1024*1024)^) { raise error("DBench: selection is too large to copy") }
        chunks=[*chunks,chunk]
        offset=offset+len(rows)
        if ((offset<=request.bottom)^) sleep(1)^
    }
    return join(chunks,"\n")^
}

// Keep writes bounded even when JSON escaping expands a single large value.
pn write_text(session,text) any^ {
    var offset=0
    while (offset<len(text)) {
        let end=min(len(text),offset+65536)
        rdb.export_write(session,slice(text,offset,end)^)^
        offset=end
        if (offset<len(text)) sleep(1)^
    }
    return null
}

pn stream_export(session,request,generation,report) any^ {
    rdb.export_begin(session,request.path)^
    let header=exports.stream_header(request.columns,request.kind)^
    write_text(session,header)^
    let bounded=request.bounded==true
    var rows=if (request.spooled) [] else request.rows
    var reading_prefix=request.spooled
    var prefix_offset=if (bounded) request.first else 0
    let prefix_end=if (bounded) request.last+1 else request.total_rows
    var done=bounded or request.done
    var count=0
    var bytes=4*len(header)
    while (true) {
        if (reading_prefix) {
            if ((prefix_offset<prefix_end)^) {
                rows=rdb.spool_read(session,prefix_offset,min(64,prefix_end-prefix_offset))^
                prefix_offset=prefix_offset+len(rows)
            } else {
                if (not bounded) rdb.spool_seal(session)^
                reading_prefix=false
                rows=[]
            }
        }
        for (row in rows) {
            let cells=if (bounded) [for (c in request.left to request.right) row[c]] else row
            let encoded=exports.stream_row(cells,request.kind,count==0)^
            bytes=bytes+4*len(encoded)
            if ((bounded and bytes>exports.MAX_BYTES)^) { raise error("DBench: export exceeds the size limit") }
            write_text(session,encoded)^
            count=count+1
            // Cancellation is cooperative; yield even while exporting the cached prefix.
            if ((count%64==0)^) {
                report({type:"export_progress",generation:generation,token:request.token,rows:count})^
                sleep(1)^
            }
        }
        report({type:"export_progress",generation:generation,token:request.token,rows:count})^
        if (reading_prefix) { continue }
        if (done) { break }
        let batch=rdb.fetch(session,64)^
        rows=batch[1]
        done=batch[2]
    }
    write_text(session,exports.stream_footer(request.kind))^
    if (not bounded) rdb.finish(session)^
    rdb.export_commit(session)^
    return count
}

pn perform(session,request) any^ {
    var spooled=request.spooled==true
    var spool_error=null
    if (request.op=="page" or request.op=="query") {
        rdb.finish(session)^
        rdb.execute(session,request.sql,request.params or [])^
        spooled=false
        if (request.op=="query") {
            // A private child stores anonymous files; the cwd may not have temp yet.
            io.mkdir("temp") ^ { null }
            let opened: any^=rdb.spool_begin(session,"temp/dbench-spool")
            if (opened is error) { spool_error=string(opened.message) }
            else { spooled=true }
        }
    }
    let batch=rdb.fetch(session,request.limit or 256)^
    if (request.op=="page") rdb.finish(session)^
    return {batch:batch,spooled:spooled,spool_error:spool_error}
}

pn serve(uri,driver,generation,report) any^ {
    open session = (rdb.database(uri,driver)^) {
        let schema=rdb.schema(session)^
        report({type:"connected",generation:generation,schema:schema})^
        while (true) {
            let request=receive()^
            if (request.op=="close") { break }
            if (request.op=="finish") {
                rdb.finish(session) ^ { null }
                continue
            }
            if (request.op=="finish_for_write") {
                let outcome: any^=rdb.finish(session)
                if (outcome is error) report({type:"write_prepare_failed",generation:generation,token:request.token,message:string(outcome.message)})^
                else report({type:"write_ready",generation:generation,token:request.token})^
                continue
            }
            if (request.op=="stream_export" or request.op=="table_export") {
                // A table page has no active cursor; stream a fresh query on the same session.
                let ready: any^=if (request.op=="table_export") rdb.execute(session,request.sql,request.params) else null
                let outcome: any^=if (ready is error) ready else stream_export(session,request,generation,report)
                if (outcome is error) {
                    rdb.export_abort(session) ^ { null }
                    if (request.bounded!=true) rdb.finish(session) ^ { null }
                    report({type:"export_failed",generation:generation,token:request.token,message:string(outcome.message)})^
                } else report({type:"exported",generation:generation,token:request.token,path:request.path,rows:outcome})^
                continue
            }
            if (request.op=="schema") {
                rdb.finish(session)^
                let updated=rdb.schema(session)^
                report({type:"schema",generation:generation,token:request.token,schema:updated})^
                continue
            }
            if (request.op=="details") {
                let outcome: any^=rdb.details(session)
                if (outcome is error) report({type:"details_failed",generation:generation,message:string(outcome.message)})^
                else report({type:"details",generation:generation,details:outcome})^
                continue
            }
            if (request.op=="window") {
                let outcome: any^=read_window(session,request)
                if (outcome is error) report({type:"window_failed",generation:generation,token:request.token,
                    window_token:request.window_token,message:string(outcome.message)})^
                else report({type:"window",generation:generation,token:request.token,
                    window_token:request.window_token,first:request.first,rows:outcome})^
                continue
            }
            if (request.op=="copy") {
                let outcome: any^=copy_spooled(session,request)
                if (outcome is error) report({type:"copy_failed",generation:generation,token:request.token,
                    copy_token:request.copy_token,message:string(outcome.message)})^
                else report({type:"copy_ready",generation:generation,token:request.token,
                    copy_token:request.copy_token,text:outcome})^
                continue
            }
            let outcome: any^=perform(session,request)
            if (outcome is error) {
                rdb.finish(session) ^ { null }
                report({type:"failed",generation:generation,token:request.token,message:string(outcome.message)})^
            } else {
                report({type:"batch",generation:generation,token:request.token,op:request.op,
                    columns:outcome.batch[0],rows:outcome.batch[1],done:outcome.batch[2],offset:request.offset or 0,
                    spooled:outcome.spooled,spool_error:outcome.spool_error})^
            }
        }
    }
    return null
}

pub pn run(uri,driver,generation,report) {
    let outcome: any^=serve(uri,driver,generation,report)
    if (outcome is error) report({type:"disconnected",generation:generation,message:string(outcome.message)}) ^ {null}
}
