-module(jetstream).
-export([run/2]).
newmap()->{array:new(1,{default,[]}),0,0}.
put(Key,Value,{Buckets,Size,Threshold})->At=Key band(array:size(Buckets)-1),Head=array:get(At,Buckets),
    case lists:keymember(Key,1,Head)of true->{array:set(At,lists:keyreplace(Key,1,Head,{Key,Value}),Buckets),Size,Threshold};false->
        B=array:set(At,[{Key,Value}|Head],Buckets),N=Size+1,
        case N>Threshold of false->{B,N,Threshold};true->Count=array:size(B)*2,Grown=array:foldl(fun(_,Entries,Acc)->lists:foldl(fun({K,_}=E,A)->I=K band(Count-1),array:set(I,[E|array:get(I,A)],A)end,Acc,Entries)end,array:new(Count,{default,[]}),B),{Grown,N,Count*3 div 4}end end.
get(Key,Buckets)->{Key,V}=lists:keyfind(Key,1,array:get(Key band(array:size(Buckets)-1),Buckets)),V.
hashmap()->{Buckets,Size,_}=lists:foldl(fun(K,T)->put(K,42,T)end,newmap(),lists:seq(0,89999)),
    Total=lists:foldl(fun(_,S)->lists:foldl(fun(K,A)->A+get(K,Buckets)end,S,lists:seq(0,89999))end,0,lists:seq(1,5)),
    {Keys,Values}=array:foldl(fun(_,Entries,Acc)->lists:foldl(fun({K,V},{X,Y})->{X+K,Y+V}end,Acc,Entries)end,{0,0},Buckets),
    Size=:=90000 andalso Total=:=42*90000*5 andalso Keys=:=90000*89999 div 2 andalso Values=:=42*90000.
once("hashmap",_)->hashmap();
once("nbody",_)->Energy=lists:foldl(fun(N,S)->Bodies=nbody:new(),S+nbody:energy(Bodies)+nbody:energy(nbody:steps(N,Bodies))end,0.0,[300,600,1200,2400]),Energy=:= -1.3524862408537381;
once("richards",_)->richards:run(1000,2322,928);
once("deltablue",_)->deltablue:chain(100,false,0),deltablue:projection(100,0),true;
once("crypto_sha1",Plain)->Text=lists:foldl(fun(_,B)-> <<B/binary,B/binary>>end,Plain,lists:seq(1,4)),binary:encode_hex(crypto:hash(sha,Text),lowercase)=:= <<"2524d264def74cce2498bf112bedf00e6c0b796d">>;
once("cube3d",_)->cube:run();
once("raytrace3d",_)->raytrace:run().
run("splay",Repeats)->splay:run(Repeats);
run("navier_stokes",_)->fluid:run();
run(Name,Repeats)->Fixture=case Name of "crypto_sha1"->{ok,B}=file:read_file("test/benchmark/native_ports/fixtures/sha1.txt"),crypto:start(),B;_->nil end,
    native_bench:run(fun()->lists:foreach(fun(_)->native_bench:check(once(Name,Fixture))end,lists:seq(1,Repeats)),true end,fun native_bench:check/1,fun(_)->ok end,native_bench:warmup()).
