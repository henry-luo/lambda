import java.util.Arrays;

final class Larceny {
    record Expr(int tag,int value,Expr left,Expr right){}
    static Expr constant(int v){return new Expr(0,v,null,null);}
    static Expr variable(){return new Expr(1,0,null,null);}
    static Expr binary(int kind,Expr l,Expr r){return new Expr(kind,0,l,r);}
    static Expr expression(){Expr m=binary(3,constant(3),variable());m=binary(3,m,variable());m=binary(3,m,variable());
        Expr n=binary(3,binary(3,constant(2),variable()),variable());return binary(2,binary(2,binary(2,m,n),variable()),constant(5));}
    static Expr derivative(Expr e){return switch(e.tag){case 0->constant(0);case 1->constant(1);case 2->binary(2,derivative(e.left),derivative(e.right));
        default->{Expr dl=derivative(e.left),dr=derivative(e.right);yield binary(2,binary(3,e.left,dr),binary(3,dl,e.right));}};}
    static int count(Expr e){return e.tag<2?1:1+count(e.left)+count(e.right);}
    static long divide(long x,long y){long q=0;while(x>=y){x-=y;q++;}return q;}
    static long modulus(long x,long y){while(x>=y)x-=y;return x;}
    static int divideRec(int x,int y){return x<y?0:1+divideRec(x-y,y);}
    static int modulusRec(int x,int y){return x<y?x:modulusRec(x-y,y);}
    static void quicksort(int[] a,int lo,int hi){if(lo>=hi)return;int pivot=a[hi],at=lo;
        for(int i=lo;i<hi;i++)if(a[i]<=pivot){int t=a[i];a[i]=a[at];a[at++]=t;}
        int t=a[at];a[at]=a[hi];a[hi]=t;quicksort(a,lo,at-1);quicksort(a,at+1,hi);}
    static int puzzle(int row,boolean[] columns,boolean[] forward,boolean[] back){if(row==10)return 1;int count=0;
        for(int col=0;col<10;col++){int a=row+col,b=row-col+9;if(!columns[col]&&!forward[a]&&!back[b]){
            columns[col]=forward[a]=back[b]=true;count+=puzzle(row+1,columns,forward,back);columns[col]=forward[a]=back[b]=false;}}
        return count;}
    static long ms2(long n){return n*(n+1)/2;}static long ms3(long n){return n*(n+1)*(n+2)/6;}static long ms4(long n){return n*(n+1)*(n+2)*(n+3)/24;}
    static long radicals(long[] counts,int size){int target=size-1;long total=0;
        for(int i=0;i*3<=target;i++)for(int j=i;i+j*2<=target;j++){int k=target-i-j;if(k<j)continue;long a=counts[i],b=counts[j],c=counts[k];
            total+=i==j&&j==k?ms3(a):i==j?ms2(a)*c:j==k?a*ms2(b):a*b*c;}return total;}
    static long ccp(long[] counts,int size){int sum=size-1,max=(size-1)/2;long total=0;
        for(int i=0;i*4<=sum;i++)for(int j=i;i+j*3<=sum;j++)for(int k=j;2*k<=sum-i-j;k++){
            int l=sum-i-j-k;if(l<k||l>max)continue;long a=counts[i],b=counts[j],c=counts[k],d=counts[l];
            total+=i==j&&j==k&&k==l?ms4(a):i==j&&j==k?ms3(a)*d:j==k&&k==l?a*ms3(b):i==j&&k==l?ms2(a)*ms2(c):i==j?ms2(a)*c*d:j==k?a*ms2(b)*d:k==l?a*b*ms2(c):a*b*c*d;
        }return total;}
    static long paraffins(int size){int half=size/2;long[] counts=new long[half+1];counts[0]=1;for(int i=1;i<=half;i++)counts[i]=radicals(counts,i);return(size%2==0?ms2(counts[half]):0)+ccp(counts,size);}
    static final double[] XS={0,1,1,0,0,1,-.5,-1,-1,-2,-2.5,-2,-1.5,-.5,.5,1,.5,0,-.5,-1};
    static final double[] YS={0,0,1,1,2,3,2,3,0,-.5,.5,1.5,2,3,3,2,1,.5,-1,-1};
    static boolean inside(double x,double y){boolean inside=false;for(int i=0,j=19;i<20;j=i++)
        if((YS[i]>y)!=(YS[j]>y)&&x<(XS[j]-XS[i])*(y-YS[i])/(YS[j]-YS[i])+XS[i])inside=!inside;return inside;}
    static int pnpoly(){int count=0;for(int ix=0;ix<500;ix++)for(int iy=0;iy<200;iy++)if(inside(-2.5+ix*.008,-1.5+iy*.025))count++;return count;}
    static int ray(){double[][] spheres={{0,0,5},{-2,0,5},{2,0,5},{0,2,5}};int hits=0;
        for(int py=0;py<100;py++)for(int px=0;px<100;px++){double dx=(px-50)/50.0,dy=(py-50)/50.0,dz=1,length=Math.sqrt(dx*dx+dy*dy+dz*dz);dx/=length;dy/=length;dz/=length;boolean hit=false;
            for(double[] s:spheres){double ex=-s[0],ey=-s[1],ez=-s[2],b=2*(ex*dx+ey*dy+ez*dz),disc=b*b-4*(ex*ex+ey*ey+ez*ez-1);if(disc>=0&&(-b-Math.sqrt(disc))/2>.001)hit=true;}if(hit)hits++;}return hits;}
    static final int[] FROM={0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14};
    static final int[] OVER={1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13};
    static final int[] TO={3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12};
    static int triangl(){boolean[] board=new boolean[15];Arrays.fill(board,true);board[0]=false;int[] stack=new int[14];int depth=0,pegs=14,solutions=0;
        while(depth>=0){
            if(pegs==1){solutions++;depth--;if(depth<0)break;int last=stack[depth];board[FROM[last]]=board[OVER[last]]=true;board[TO[last]]=false;pegs++;stack[depth]=last+1;continue;}
            boolean found=false;for(int m=stack[depth];m<36;m++)if(board[FROM[m]]&&board[OVER[m]]&&!board[TO[m]]){
                board[FROM[m]]=board[OVER[m]]=false;board[TO[m]]=true;pegs--;stack[depth++]=m;if(depth<14)stack[depth]=0;found=true;break;}
            if(!found){depth--;if(depth>=0){int last=stack[depth];board[FROM[last]]=board[OVER[last]]=true;board[TO[last]]=false;pegs++;stack[depth]=last+1;}}
        }return solutions;}
    record GcResult(String output,Beng.Tree longLived){}
    static GcResult gcbench(){StringBuilder out=new StringBuilder();out.append("stretch tree of depth 15 check: ").append(Beng.checkTree(Beng.tree(15))).append('\n');Beng.Tree longLived=Beng.tree(14);
        for(int d=4;d<=14;d+=2){int n=1<<(18-d),total=0;for(int i=0;i<n;i++)total+=Beng.checkTree(Beng.tree(d));out.append(n).append(" trees of depth ").append(d).append(" check: ").append(total).append('\n');}
        return new GcResult(out.toString(),longLived);}
    static void run(String name){
        switch(name){
            case "deriv"->NativeBench.scalar(name,()->{int n=0;for(int i=0;i<5000;i++)n=count(derivative(expression()));return n;},45);
            case "array1"->NativeBench.scalar(name,()->{int[] a=new int[10000];for(int i=0;i<a.length;i++)a[i]=i;long total=0;for(int r=0;r<100;r++){total=0;for(int v:a)total+=v;NativeBench.observed=total;}return total;},49995000);
            case "diviter"->NativeBench.scalar(name,()->{long result=0;for(int i=0;i<1000;i++)result+=divide(1000000,2)-modulus(1000000,2);return result;},500000000);
            case "divrec"->NativeBench.scalar(name,()->{long result=0;for(int i=0;i<1000;i++)result+=divideRec(1000,2)-modulusRec(1000,2);return result;},500000);
            case "primes"->NativeBench.scalar(name,()->Kostya.primes(1000000),78498);
            case "puzzle"->NativeBench.scalar(name,()->puzzle(0,new boolean[10],new boolean[20],new boolean[20]),724);
            case "quicksort"->NativeBench.run(()->{int[] a=new int[5000];long seed=42;for(int i=0;i<a.length;i++){seed=Kostya.next(seed);a[i]=(int)seed;}quicksort(a,0,a.length-1);return a;},v->{for(int i=1;i<v.length;i++)NativeBench.check(v[i]>=v[i-1]);},v->System.out.println("quicksort: PASS"),NativeBench.warmup());
            case "paraffins"->NativeBench.run(()->{long result=0;for(int r=0;r<10;r++)for(int n=1;n<=23;n++)result=paraffins(n);return result;},v->NativeBench.check(v==5731580),v->System.out.println("paraffins: nb(23) = "+v+"\nparaffins: PASS"),NativeBench.warmup());
            case "pnpoly"->NativeBench.run(Larceny::pnpoly,v->NativeBench.check(v==29415),v->System.out.println("pnpoly: total=100000 inside="+v+"\npnpoly: DONE"),NativeBench.warmup());
            case "ray"->NativeBench.run(Larceny::ray,v->NativeBench.check(v==1392),v->System.out.println("ray: hits="+v+"\nray: PASS"),NativeBench.warmup());
            case "triangl"->NativeBench.run(Larceny::triangl,v->NativeBench.check(v==29760),v->System.out.println("triangl: solutions="+v+"\ntriangl: PASS"),NativeBench.warmup());
            case "gcbench"->NativeBench.run(Larceny::gcbench,v->NativeBench.check(Beng.checkTree(v.longLived)==32767),v->System.out.print(v.output+"long lived tree of depth 14 check: "+Beng.checkTree(v.longLived)+"\n"),NativeBench.warmup());
            default->throw new IllegalArgumentException(name);
        }
    }
}
