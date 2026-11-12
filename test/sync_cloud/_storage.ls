// Qualify the selected S3 dialect; object identity checks are independent of ETags.
import s3: lambda.sync.s3
import schema: lambda.sync.cloud_schema
pn main() {
    let environment = sys.proc.self.env#
    let config = parse(environment.LAMBDA_CLOUD_STORAGE,'json')^
    let key = "qualification/" ++ schema.digest(uuid()^)^
    let first = s3.write(config,key,"abc")^
    let duplicate = s3.write(config,key,"changed")^
    let value = s3.get(config,key)^
    let stale = s3.write(config,key,"bad","\"stale\"")^
    let next = s3.write(config,key,"next",value.etag)^
    let observed = s3.get(config,key)^
    return [first.written,not duplicate.written,hash(value.body,'sha256')^ == hash("abc",'sha256')^,
        not stale.written,next.written,hash(observed.body,'sha256')^ == hash("next",'sha256')^]
}
