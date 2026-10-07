-module(r7rs).
-export([run/1,fib/1,fibfp/1,tak/3,ack/2]).
fib(N)when N<2->N;
fib(N)->fib(N-1)+fib(N-2).
fibfp(N)when N<2.0->N;
fibfp(N)->fibfp(N-1.0)+fibfp(N-2.0).
tak(X,Y,Z)when Y>=X->Z;
tak(X,Y,Z)->tak(tak(X-1,Y,Z),tak(Y-1,Z,X),tak(Z-1,X,Y)).
ack(0,N)->N+1;
ack(M,0)->ack(M-1,1);
ack(M,N)->ack(M-1,ack(M,N-1)).
sum(N,S)when N<0->S;
sum(N,S)->sum(N-1,S+N).
repeat_sum(0,Result)->Result;
repeat_sum(N,_)->repeat_sum(N-1,sum(10000,0)).
queens_ok(_,_,[])->true;
queens_ok(Row,D,[P|Ps])->P=/=Row+D andalso P=/=Row-D andalso queens_ok(Row,D+1,Ps).
queens([],Rest,_)->case Rest of []->1;_->0 end;
queens([Row|Rows],Rest,Placed)->
    Count=case queens_ok(Row,1,Placed)of true->queens(Rows++Rest,[],[Row|Placed]);false->0 end,
    Count+queens(Rows,Rest++[Row],Placed).
%% FFT writes native persistent array values; no emulated variable environment.
fft(A)->fft_width(2,4096,fft_permute(0,0,4096,A)).
fft_permute(I,_,N,A)when I>=N->A;
fft_permute(I,J,N,A)->
    B=case I<J of true->AI=array:get(I,A),AI1=array:get(I+1,A),AJ=array:get(J,A),AJ1=array:get(J+1,A),
        array:set(J+1,AI1,array:set(J,AI,array:set(I+1,AJ1,array:set(I,AJ,A))));false->A end,
    K=fft_next(J,N div 2),fft_permute(I+2,K,N,B).
fft_next(J,M)when M>=2,J>=M->fft_next(J-M,M div 2);
fft_next(J,M)->J+M.
fft_width(W,N,A)when W>=N->A;
fft_width(W,N,A)->Theta=2.0*math:pi()/W,Half=math:sin(Theta/2),
    B=fft_twiddle(0,W,N,1.0,0.0,-2.0*Half*Half,math:sin(Theta),A),fft_width(W*2,N,B).
fft_twiddle(M,W,_,_,_,_,_,A)when M>=W->A;
fft_twiddle(M,W,N,Wr,Wi,Wpr,Wpi,A)->
    B=fft_butterfly(M,W,N,Wr,Wi,A),
    fft_twiddle(M+2,W,N,Wr*Wpr-Wi*Wpi+Wr,Wi*Wpr+Wr*Wpi+Wi,Wpr,Wpi,B).
fft_butterfly(I,_,N,_,_,A)when I>=N->A;
fft_butterfly(I,W,N,Wr,Wi,A)->K=I+W,Ar=array:get(I,A),Ai=array:get(I+1,A),Br=array:get(K,A),Bi=array:get(K+1,A),
    Tr=Wr*Br-Wi*Bi,Ti=Wr*Bi+Wi*Br,
    B=array:set(K+1,Ai-Ti,array:set(K,Ar-Tr,array:set(I+1,Ai+Ti,array:set(I,Ar+Tr,A)))),
    fft_butterfly(I+2*W,W,N,Wr,Wi,B).
escape(X,Y)->Cr=-1.0+X*0.005,Ci=-0.5+Y*0.005,escape(Cr,Ci,Cr,Ci,0).
escape(_,_,_,_,64)->64;
escape(Cr,Ci,Zr,Zi,N)->R=Zr*Zr,I=Zi*Zi,case R+I>16.0 of true->N;false->escape(Cr,Ci,R-I+Cr,2.0*Zr*Zi+Ci,N+1)end.
work("fib")->fib(27);
work("fibfp")->fibfp(27.0);
work("tak")->tak(18,12,6);
work("cpstak")->tak(18,12,6),tak(18,12,6);
work("sum")->repeat_sum(100,0);
work("sumfp")->sum(100000.0,0.0);
work("ack")->ack(3,8);
work("nqueens")->queens(lists:seq(1,8),[],[]);
work("fft")->array:get(0,fft(array:new(4096,{default,0.0})));
work("mbrot")->Matrix=[[escape(X,Y)||Y<-lists:seq(74,0,-1)]||X<-lists:seq(74,0,-1)],lists:last(lists:last(Matrix)).
expected("fib")->196418;expected("fibfp")->196418.0;
expected("tak")->7;expected("cpstak")->7;expected("sum")->50005000;
expected("sumfp")->5000050000.0;expected("ack")->2045;expected("nqueens")->92;expected("fft")->0.0;expected("mbrot")->5.
run(Name)->Expected=expected(Name),native_bench:run(fun()->work(Name)end,
    fun(V)->native_bench:check(V=:=Expected)end,fun(_)->io:format("~s: PASS~n",[Name])end,native_bench:warmup()).
