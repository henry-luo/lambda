import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

fn trace(engine,s,keys) array^ {
    if (len(keys) == 0) []
    else {
        let result = k.press(engine,s,keys[0])^;
        [result.actions,*trace(engine,result.session,drop(keys,1))^]
    }
}

pn main() {
    let engine = f.engine()^
    let idle = ime.session(engine,{page_size:2})^
    let no_match = k.typed(engine,idle,"qq")^
    let partial = k.choose(engine,k.typed(engine,idle,"beijingdaxue")^,"北京")^;
    [trace(engine,idle,[".","\"","\"","'"])^,
        trace(engine,no_match,["9"," ","Escape"])^,
        trace(engine,partial.session,["Enter"])^,
        trace(engine,idle,["a","Backspace"])^]
}
