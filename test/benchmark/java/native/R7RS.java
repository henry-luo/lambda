import java.util.Arrays;

final class R7RS {
    static long fib(long n) { return n < 2 ? n : fib(n-1) + fib(n-2); }
    static double fibfp(double n) { return n < 2 ? n : fibfp(n-1) + fibfp(n-2); }
    static int tak(int x,int y,int z) { return y>=x ? z : tak(tak(x-1,y,z),tak(y-1,z,x),tak(z-1,x,y)); }
    static int ack(int m,int n) { return m==0 ? n+1 : n==0 ? ack(m-1,1) : ack(m-1,ack(m,n-1)); }
    static long sum(int n) { long total=0; while(n>=0) total+=n--; return total; }
    static double sumfp(double n) { double total=0; while(n>=0) total+=n--; return total; }
    static boolean queensOk(int row,int dist,int[] placed,int length) {
        if(dist>length) return true;
        int p=placed[length-dist];
        return p!=row+dist && p!=row-dist && queensOk(row,dist+1,placed,length);
    }
    static int queens(int[] candidates,int[] rest,int[] placed,int length) {
        if(candidates.length==0) return rest.length==0 ? 1 : 0;
        int row=candidates[0], count=0;
        if(queensOk(row,1,placed,length)) {
            int[] next=new int[candidates.length-1+rest.length];
            System.arraycopy(candidates,1,next,0,candidates.length-1);
            System.arraycopy(rest,0,next,candidates.length-1,rest.length);
            placed[length]=row;
            count+=queens(next,new int[0],placed,length+1);
        }
        int[] nextRest=Arrays.copyOf(rest,rest.length+1); nextRest[rest.length]=row;
        return count+queens(Arrays.copyOfRange(candidates,1,candidates.length),nextRest,placed,length);
    }
    static void fft(double[] data) {
        int n=data.length,j=0;
        for(int i=0;i<n;i+=2) {
            if(i<j) { double t=data[i];data[i]=data[j];data[j]=t;t=data[i+1];data[i+1]=data[j+1];data[j+1]=t; }
            int m=n/2; while(m>=2&&j>=m){j-=m;m/=2;} j+=m;
        }
        for(int width=2;width<n;width*=2) {
            double theta=2*Math.PI/width,half=Math.sin(theta/2),wpr=-2*half*half,wpi=Math.sin(theta),wr=1,wi=0;
            for(int m=0;m<width;m+=2) {
                for(int i=m;i<n;i+=2*width) {
                    int k=i+width;double tr=wr*data[k]-wi*data[k+1],ti=wr*data[k+1]+wi*data[k];
                    data[k]=data[i]-tr;data[k+1]=data[i+1]-ti;data[i]+=tr;data[i+1]+=ti;
                }
                double next=wr*wpr-wi*wpi+wr;wi=wi*wpr+wr*wpi+wi;wr=next;
            }
        }
    }
    static int escape(int x,int y) {
        double cr=-1+x*.005,ci=-.5+y*.005,zr=cr,zi=ci;
        for(int count=0;count<64;count++) {
            double r=zr*zr,i=zi*zi;if(r+i>16)return count;
            zi=2*zr*zi+ci;zr=r-i+cr;
        }
        return 64;
    }
    static void run(String name) {
        switch(name) {
            case "fib" -> NativeBench.scalar(name,()->fib(27),196418);
            case "fibfp" -> NativeBench.scalar(name,()->fibfp(27),196418);
            case "tak" -> NativeBench.scalar(name,()->tak(18,12,6),7);
            case "cpstak" -> NativeBench.scalar(name,()->{NativeBench.observed=tak(18,12,6);return tak(18,12,6);},7);
            case "ack" -> NativeBench.scalar(name,()->ack(3,8),2045);
            case "sum" -> NativeBench.scalar(name,()->{long r=0;for(int i=0;i<100;i++){r=sum(10000);NativeBench.observed=r;}return r;},50005000);
            case "sumfp" -> NativeBench.scalar(name,()->sumfp(100000),5000050000.0);
            case "nqueens" -> NativeBench.scalar(name,()->queens(new int[]{1,2,3,4,5,6,7,8},new int[0],new int[8],0),92);
            case "fft" -> NativeBench.scalar(name,()->{double[] data=new double[4096];fft(data);NativeBench.observed=data;return data[0];},0);
            case "mbrot" -> NativeBench.scalar(name,()->{int[][] a=new int[75][75];for(int y=74;y>=0;y--)for(int x=74;x>=0;x--)a[x][y]=escape(x,y);NativeBench.observed=a;return a[0][0];},5);
            default -> throw new IllegalArgumentException(name);
        }
    }
}
