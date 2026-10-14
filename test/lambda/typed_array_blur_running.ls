// even supports retain the stencil's floor(k/2) anchor, including strided views.
let image=[[1.0,2.0,3.0],[4.0,5.0,6.0]]
flatten(blur(image,2))==[1.0,1.5,2.5,2.5,3.0,4.0]
flatten(blur(transpose(image),2))==[1.0,2.5,1.5,3.0,2.5,4.0]
flatten(blur(image,0))==flatten(image)
flatten(blur([[7.0]],101))==[7.0]
flatten(blur([[1e308,-1e308,1e308]],1))==[1e308,-1e308,1e308]
let sparse=reshape([for (i in 0 to 80) if (i<30) (i%7+1)*0.1 else 0.0],[1,81])
all([for (i in 40 to 80) flatten(blur(sparse,9))[i]==0.0])
