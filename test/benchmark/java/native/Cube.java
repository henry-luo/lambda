final class Cube {
    static final int[][] FACES={{0,1,2,3},{3,2,6,7},{7,6,5,4},{4,5,1,0},{4,0,3,7},{1,5,6,2}};
    static final int[][] EDGE_IDS={{0,1,2,3},{2,9,6,10},{6,5,4,7},{4,8,0,11},{11,3,10,7},{8,5,9,1}};
    static double[] identity(){return new double[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
    static double[] mul(double[] a,double[] b){double[] out=new double[16];for(int i=0;i<4;i++)for(int j=0;j<4;j++)out[i*4+j]=a[i*4]*b[j]+a[i*4+1]*b[4+j]+a[i*4+2]*b[8+j]+a[i*4+3]*b[12+j];return out;}
    static double[] vector(double[] m,double[] v,int n){double[] r=new double[n];for(int i=0;i<n;i++)r[i]=m[i*4]*v[0]+m[i*4+1]*v[1]+m[i*4+2]*v[2]+(n==4?m[i*4+3]*v[3]:0);return r;}
    static double[] translate(double[] m,double x,double y,double z){double[] t=identity();t[3]=x;t[7]=y;t[11]=z;return mul(t,m);}
    static double[] rotate(double[] m,int axis,double angle){double a=angle*Math.PI/180,c=Math.cos(a),s=Math.sin(a);double[] r=identity();
        if(axis==0){r[5]=c;r[6]=-s;r[9]=s;r[10]=c;}else if(axis==1){r[0]=c;r[2]=s;r[8]=-s;r[10]=c;}else{r[0]=c;r[1]=-s;r[4]=s;r[5]=c;}return mul(r,m);}
    static double[] normal(double[] v0,double[] v1,double[] v2){double[] a=new double[3],b=new double[3];for(int i=0;i<3;i++){a[i]=v0[i]-v1[i];b[i]=v2[i]-v1[i];}
        double[] r={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};double len=Math.sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);for(int i=0;i<3;i++)r[i]/=len;return r;}
    static int line(double[] from,double[] to,int last){double x=from[0],y=from[1],dx=Math.abs(to[0]-x),dy=Math.abs(to[1]-y);
        int ix1=to[0]>=x?1:-1,ix2=ix1,iy1=to[1]>=y?1:-1,iy2=iy1;double den,num,add,pixels;
        if(dx>=dy){ix1=0;iy2=0;den=dx;num=dx/2;add=dy;pixels=dx;}else{ix2=0;iy1=0;den=dy;num=dy/2;add=dx;pixels=dy;}
        int end=(int)Math.floor(last+pixels+.5);for(int i=last;i<end;i++){num+=add;if(num>=den){num-=den;x+=ix1;y+=iy1;}x+=ix2;y+=iy2;}return end;}
    static int draw(double[][] points,double[][] normals,double[] matrix){double[][] current=new double[6][];for(int i=0;i<6;i++)current[i]=vector(matrix,normals[i],3);
        boolean[] drawn=new boolean[12];int last=0;for(int i=0;i<6;i++)if(current[i][2]<0)for(int j=0;j<4;j++){int id=EDGE_IDS[i][j];if(!drawn[id]){last=line(points[FACES[i][j]],points[FACES[i][(j+1)%4]],last);drawn[id]=true;}}return last;}
    static double work(int size){double c=size;double[][] p={{-c,-c,c,1},{-c,c,c,1},{c,c,c,1},{c,-c,c,1},{-c,-c,-c,1},{-c,c,-c,1},{c,c,-c,1},{c,-c,-c,1},{0,0,0,1}};
        int[] edges={0,1,2,3,2,6,7,6,5,4,5,1,4,0,3,1,5,6};double[][] n=new double[6][];for(int i=0;i<6;i++)n[i]=normal(p[edges[i*3]],p[edges[i*3+1]],p[edges[i*3+2]]);
        double[] m=translate(identity(),150,150,20);for(int i=0;i<9;i++)p[i]=vector(m,p[i],4);
        double[][] pixels=new double[18*size][4];for(double[] pixel:pixels)pixel[3]=1;int last=draw(p,n,m);
        for(int k=0;k<51;k++){double[] center=p[8];double[] t=translate(identity(),-center[0],-center[1],-center[2]);t=rotate(t,0,1);t=rotate(t,1,3);t=rotate(t,2,5);t=translate(t,center[0],center[1],center[2]);m=mul(t,m);
            for(int i=8;i>=0;i--)p[i]=vector(t,p[i],4);last=draw(p,n,m);}
        NativeBench.observed=last;double total=0;for(double[] point:p)for(double value:point)total+=value;return total;}
    static boolean run(){for(int size=20;size<=160;size*=2)if(Math.abs(work(size)-2889)>5e-10)return false;return true;}
}
