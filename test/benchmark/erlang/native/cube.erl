-module(cube).
-export([run/0]).
identity()->{1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0}.
mul(A,B)->list_to_tuple([element(I*4+1,A)*element(J+1,B)+element(I*4+2,A)*element(J+5,B)+element(I*4+3,A)*element(J+9,B)+element(I*4+4,A)*element(J+13,B)||I<-lists:seq(0,3),J<-lists:seq(0,3)]).
vector(M,V,4)->list_to_tuple([element(I*4+1,M)*element(1,V)+element(I*4+2,M)*element(2,V)+element(I*4+3,M)*element(3,V)+element(I*4+4,M)*element(4,V)||I<-lists:seq(0,3)]);
vector(M,V,3)->list_to_tuple([element(I*4+1,M)*element(1,V)+element(I*4+2,M)*element(2,V)+element(I*4+3,M)*element(3,V)||I<-lists:seq(0,2)]).
translate(M,X,Y,Z)->mul(setelement(12,setelement(8,setelement(4,identity(),X),Y),Z),M).
rotate(M,Axis,Angle)->A=Angle*math:pi()/180.0,C=math:cos(A),S=math:sin(A),
    R=case Axis of 0->{1.0,0.0,0.0,0.0,0.0,C,-S,0.0,0.0,S,C,0.0,0.0,0.0,0.0,1.0};1->{C,0.0,S,0.0,0.0,1.0,0.0,0.0,-S,0.0,C,0.0,0.0,0.0,0.0,1.0};2->{C,-S,0.0,0.0,S,C,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0}end,mul(R,M).
normal({Ax,Ay,Az,_},{Bx,By,Bz,_},{Cx,Cy,Cz,_})->X=(Ay-By)*(Cz-Bz)-(Az-Bz)*(Cy-By),Y=(Az-Bz)*(Cx-Bx)-(Ax-Bx)*(Cz-Bz),Z=(Ax-Bx)*(Cy-By)-(Ay-By)*(Cx-Bx),Len=math:sqrt(X*X+Y*Y+Z*Z),{X/Len,Y/Len,Z/Len}.
line({X,Y,_,_},{Tx,Ty,_,_},Last)->Dx=abs(Tx-X),Dy=abs(Ty-Y),Ix=case Tx>=X of true->1;false->-1 end,Iy=case Ty>=Y of true->1;false->-1 end,
    {Ix1,Ix2,Iy1,Iy2,Den,Num,Add,Pixels}=case Dx>=Dy of true->{0,Ix,Iy,0,Dx,Dx/2,Dy,Dx};false->{Ix,0,0,Iy,Dy,Dy/2,Dx,Dy}end,
    End=floor(Last+Pixels+0.5),raster(End-Last,X,Y,Ix1,Ix2,Iy1,Iy2,Den,Num,Add),End.
raster(0,_,_,_,_,_,_,_,_,_)->ok;
raster(N,X,Y,Ix1,Ix2,Iy1,Iy2,Den,Num,Add)->A=Num+Add,{B,X1,Y1}=case A>=Den of true->{A-Den,X+Ix1,Y+Iy1};false->{A,X,Y}end,raster(N-1,X1+Ix2,Y1+Iy2,Ix1,Ix2,Iy1,Iy2,Den,B,Add).
draw(P,Normals,M)->Faces=[{[0,1,2,3],[0,1,2,3]},{[3,2,6,7],[2,9,6,10]},{[7,6,5,4],[6,5,4,7]},{[4,5,1,0],[4,8,0,11]},{[4,0,3,7],[11,3,10,7]},{[1,5,6,2],[8,5,9,1]}],
    Current=[vector(M,V,3)||V<-Normals],{Last,_}=lists:foldl(fun({{Indices,Ids},{_,_,Z}},{L,Seen})->case Z<0 of false->{L,Seen};true->Targets=tl(Indices)++[hd(Indices)],lists:foldl(fun({A,B,Id},{At,Set})->Bit=1 bsl Id,case Set band Bit of 0->{line(element(A+1,P),element(B+1,P),At),Set bor Bit};_->{At,Set}end end,{L,Seen},lists:zip3(Indices,Targets,Ids))end end,{0,0},lists:zip(Faces,Current)),Last.
work(Size)->C=float(Size),P0={{-C,-C,C,1.0},{-C,C,C,1.0},{C,C,C,1.0},{C,-C,C,1.0},{-C,-C,-C,1.0},{-C,C,-C,1.0},{C,C,-C,1.0},{C,-C,-C,1.0},{0.0,0.0,0.0,1.0}},
    Normals=[normal(element(A+1,P0),element(B+1,P0),element(D+1,P0))||{A,B,D}<-[{0,1,2},{3,2,6},{7,6,5},{4,5,1},{4,0,3},{1,5,6}]],M0=translate(identity(),150.0,150.0,20.0),P1=list_to_tuple([vector(M0,V,4)||V<-tuple_to_list(P0)]),
    Pixels=[{0.0,0.0,0.0,1.0}||_<-lists:seq(1,18*Size)],native_bench:check(length(Pixels)=:=18*Size),draw(P1,Normals,M0),
    {P,_}=lists:foldl(fun(_,{Points,M})->{X,Y,Z,_}=element(9,Points),T0=translate(identity(),-X,-Y,-Z),T=translate(rotate(rotate(rotate(T0,0,1.0),1,3.0),2,5.0),X,Y,Z),Next=list_to_tuple([vector(T,V,4)||V<-tuple_to_list(Points)]),Matrix=mul(T,M),draw(Next,Normals,Matrix),{Next,Matrix}end,{P1,M0},lists:seq(1,51)),
    lists:foldl(fun(V,S)->lists:foldl(fun(X,A)->A+X end,S,tuple_to_list(V))end,0.0,tuple_to_list(P)).
run()->lists:all(fun(Size)->abs(work(Size)-2889.0)=<5.0e-10 end,[20,40,80,160]).
