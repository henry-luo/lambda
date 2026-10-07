-module(raytrace).
-export([run/0]).
-record(triangle,{axis,normal,nu,nv,nd,eu,ev,nu1,nv1,nu2,nv2,floor=false}).
add({X,Y,Z},{A,B,C})->{X+A,Y+B,Z+C}.
sub({X,Y,Z},{A,B,C})->{X-A,Y-B,Z-C}.
scale({X,Y,Z},S)->{X*S,Y*S,Z*S}.
dot({X,Y,Z},{A,B,C})->X*A+Y*B+Z*C.
cross({X,Y,Z},{A,B,C})->{Y*C-Z*B,Z*A-X*C,X*B-Y*A}.
length_v(V)->math:sqrt(dot(V,V)).
normalize({X,Y,Z}=V)->L=length_v(V),{X/L,Y/L,Z/L}.
triangle(P1,P2,P3,Floor)->E1=sub(P3,P1),E2=sub(P2,P1),N=cross(E1,E2),{X,Y,Z}=N,Ax=abs(X),Ay=abs(Y),Az=abs(Z),
    Axis=case Ax>Ay of true->case Ax>Az of true->1;false->3 end;false->case Ay>Az of true->2;false->3 end end,U=Axis rem 3+1,V=(Axis+1)rem 3+1,
    U1=element(U,E1),V1=element(V,E1),U2=element(U,E2),V2=element(V,E2),Det=U1*V2-V1*U2,Na=element(Axis,N),
    #triangle{axis=Axis,normal=normalize(N),nu=element(U,N)/Na,nv=element(V,N)/Na,nd=dot(N,P1)/Na,eu=element(U,P1),ev=element(V,P1),nu1=U1/Det,nv1= -V1/Det,nu2=V2/Det,nv2= -U2/Det,floor=Floor}.
outside(T,Near,Far)->(Near=/=undefined andalso T<Near)orelse(Far=/=undefined andalso T>Far).
intersect(T,Origin,Direction,Near,Far)->Axis=T#triangle.axis,U=Axis rem 3+1,V=(Axis+1)rem 3+1,D=element(Axis,Direction)+T#triangle.nu*element(U,Direction)+T#triangle.nv*element(V,Direction),
    case D==0 of true->none;false->Distance=(T#triangle.nd-element(Axis,Origin)-T#triangle.nu*element(U,Origin)-T#triangle.nv*element(V,Origin))/D,
        case outside(Distance,Near,Far)of true->none;false->Pu=element(U,Origin)+Distance*element(U,Direction)-T#triangle.eu,Pv=element(V,Origin)+Distance*element(V,Direction)-T#triangle.ev,
            A2=Pv*T#triangle.nu1+Pu*T#triangle.nv1,A3=Pu*T#triangle.nu2+Pv*T#triangle.nv2,
            case A2<0 orelse A3<0 orelse A2+A3>1 of true->none;false->Distance end end end.
blocked([],_,_,_)->false;
blocked([T|Ts],Origin,Direction,Far)->case intersect(T,Origin,Direction,0.0001,Far)of none->blocked(Ts,Origin,Direction,Far);_->true end.
trace(Ts,Lights,Origin,Direction,Near,Far)->{Closest,Distance}=lists:foldl(fun(T,{C,F})->case intersect(T,Origin,Direction,Near,F)of none->{C,F};D->{T,D}end end,{none,Far},Ts),
    case Closest of none->{0.8,0.8,1.0};_->Normal0=Closest#triangle.normal,Normal=case dot(Direction,Normal0)>0 of true->scale(Normal0,-1.0);false->Normal0 end,Hit=add(Origin,scale(Direction,Distance)),
        case Closest#triangle.floor of true->{X,_,Z}=Hit,Xm=positive_mod(X/32.0,2.0),Zm=positive_mod(Z/32.0+0.3,2.0),
            case (Xm<1)/=(Zm<1)of true->Reflection=add(scale(Normal,-2.0*dot(Direction,Normal)),Direction),trace(Ts,Lights,Hit,Reflection,0.0001,1000000.0);false->shade(Ts,Lights,Hit,Normal,{0.0,0.4,0.0})end;
            false->shade(Ts,Lights,Hit,Normal,{0.7,0.7,0.7})end end.
positive_mod(X,M)->R=math:fmod(X,M),math:fmod(R+M,M).
shade(Ts,Lights,Hit,Normal,{Cr,Cg,Cb})->{R,G,B}=lists:foldl(fun({Position,Colour},Sum)->To=sub(Position,Hit),Distance=length_v(To),Direction=scale(To,1.0/Distance),
    case blocked(Ts,Hit,Direction,Distance-0.0001)of true->Sum;false->Nl=dot(Normal,Direction),case Nl>0 of true->add(Sum,scale(Colour,Nl));false->Sum end end end,{0.1,0.1,0.1},Lights),{R*Cr,G*Cg,B*Cb}.
transform({Xx,Xy,Xz},{Yx,Yy,Yz},{Zx,Zy,Zz},{Vx,Vy,Vz})->{Xx*Vx+Yx*Vy+Zx*Vz,Xy*Vx+Yy*Vy+Zy*Vz,Xz*Vx+Yz*Vy+Zz*Vz}.
render()->Tfl={-10.0,10.0,-10.0},Tfr={10.0,10.0,-10.0},Tbl={-10.0,10.0,10.0},Tbr={10.0,10.0,10.0},Bfl={-10.0,-10.0,-10.0},Bfr={10.0,-10.0,-10.0},Bbl={-10.0,-10.0,10.0},Bbr={10.0,-10.0,10.0},
    Ffl={-1000.0,-30.0,-1000.0},Ffr={1000.0,-30.0,-1000.0},Fbl={-1000.0,-30.0,1000.0},Fbr={1000.0,-30.0,1000.0},
    Ts=[triangle(A,B,C,false)||{A,B,C}<-[{Tfl,Tfr,Bfr},{Tfl,Bfr,Bfl},{Tbl,Tbr,Bbr},{Tbl,Bbr,Bbl},{Tbl,Tfl,Bbl},{Tfl,Bfl,Bbl},{Tbr,Tfr,Bbr},{Tfr,Bfr,Bbr},{Tbl,Tbr,Tfr},{Tbl,Tfr,Tfl},{Bbl,Bbr,Bfr},{Bbl,Bfr,Bfl}]]++[triangle(Fbl,Fbr,Ffr,true),triangle(Fbl,Ffr,Ffl,true)],
    Lights=[{{20.0,38.0,-22.0},{0.7,0.3,0.3}},{{-23.0,40.0,17.0},{0.7,0.3,0.3}},{{23.0,20.0,17.0},{0.7,0.7,0.7}}],
    Origin={-40.0,40.0,40.0},Z=normalize(sub({0.0,0.0,0.0},Origin)),X=normalize(cross({0.0,1.0,0.0},Z)),Y=normalize(cross(X,scale(Z,-1.0))),
    [D0,D1,D2,D3]=[transform(X,Y,Z,normalize(V))||V<-[{-0.7,0.7,1.0},{0.7,0.7,1.0},{0.7,-0.7,1.0},{-0.7,-0.7,1.0}]],
    [begin Yf=Row/30.0,R0=add(scale(D0,Yf),scale(D3,1.0-Yf)),R1=add(scale(D1,Yf),scale(D2,1.0-Yf)),
        [begin Xf=Col/30.0,trace(Ts,Lights,add(scale(Origin,Xf),scale(Origin,1.0-Xf)),normalize(add(scale(R0,Xf),scale(R1,1.0-Xf))),undefined,undefined)end||Col<-lists:seq(0,29)]end||Row<-lists:seq(0,29)].
number(V)->case V==trunc(V)of true->integer_to_binary(trunc(V));false->float_to_binary(V,[short])end.
canvas(Pixels)->Prefix= <<"<canvas id=\"renderCanvas\" width=\"30px\" height=\"30px\"></canvas><script>\nvar pixels = [">>,Suffix= <<"];\n    var canvas = document.getElementById(\"renderCanvas\").getContext(\"2d\");\n\n\n    var size = 30;\n    canvas.fillStyle = \"red\";\n    canvas.fillRect(0, 0, size, size);\n    canvas.scale(1, -1);\n    canvas.translate(0, -size);\n\n    if (!canvas.setFillColor)\n        canvas.setFillColor = function(r, g, b, a) {\n            this.fillStyle = \"rgb(\"+[Math.floor(r * 255), Math.floor(g * 255), Math.floor(b * 255)]+\")\";\n    }\n\nfor (var y = 0; y < size; y++) {\n  for (var x = 0; x < size; x++) {\n    var l = pixels[y][x];\n    canvas.setFillColor(l[0], l[1], l[2], 1);\n    canvas.fillRect(x, y, 1, 1);\n  }\n}</script>">>,
    iolist_to_binary([Prefix,[["[",[["[",number(R),",",number(G),",",number(B),"],"]||{R,G,B}<-Row],"],"]||Row<-Pixels],Suffix]).
run()->byte_size(canvas(render()))=:=20970.
