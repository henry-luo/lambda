// common native host envelope; public requests and wrapping keys use separate channels.
import codec: .codec
import hex: lambda.security.tuf.metadata
pub pn invoke(action, result_variable=null) any^ {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let result = action(request,hex.unhex(environment.LAMBDA_SYNC_KEY)^,environment)^
    let destination = if (result_variable == null) null else environment[result_variable]
    if (destination != null) {
        output(codec.encode(result)^,destination,{format:'text',atomic:true})^
        return null
    }
    return result
}
