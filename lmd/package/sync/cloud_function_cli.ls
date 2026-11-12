// Generic function process entry: host translates HTTP only; grants/publication remain script policy.
import service: .cloud_function
import s3: .s3
import codec: .codec
pn main() {
    let environment = sys.proc.self.env#
    let config = parse(environment.LAMBDA_CLOUD_STORAGE,'json')^
    let request = codec.decode(input(environment.LAMBDA_CLOUD_EVENT,'text')^,131072)^
    let result = service.handle(s3.adapter(config),request,now().unix) ^ { ^ }
    let response = if (result is error) {ok:false} else {ok:true,body:result}
    output(codec.encode(response)^,environment.LAMBDA_CLOUD_RESULT,{format:'text',atomic:true})^
    return null
}
