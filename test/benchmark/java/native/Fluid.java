import java.util.Arrays;

final class Fluid {
    static final int W=128,R=130,SIZE=16900;
    static final class State {double[] d=new double[SIZE],dp=new double[SIZE],u=new double[SIZE],up=new double[SIZE],v=new double[SIZE],vp=new double[SIZE];int till,between=5;}
    static void add(double[] x,double[] s){for(int i=0;i<SIZE;i++)x[i]+=.1*s[i];}
    static void boundary(int b,double[] x){for(int i=1;i<=W;i++){
        x[i]=b==2?-x[i+R]:x[i+R];x[i+129*R]=b==2?-x[i+128*R]:x[i+128*R];
        x[i*R]=b==1?-x[1+i*R]:x[1+i*R];x[129+i*R]=b==1?-x[128+i*R]:x[128+i*R];}
        x[0]=.5*(x[1]+x[R]);x[129*R]=.5*(x[1+129*R]+x[128*R]);
        x[129]=.5*(x[128]+x[129+R]);x[129+129*R]=.5*(x[128+129*R]+x[129+128*R]);}
    static void solve(int b,double[] x,double[] x0,double a,double c){
        if(a==0&&c==1){for(int j=1;j<=W;j++)for(int i=0;i<W;i++)x[j*R+1+i]=x0[j*R+1+i];boundary(b,x);return;}
        double inv=1/c;for(int n=0;n<20;n++){for(int j=1;j<=W;j++){int last=(j-1)*R,at=j*R+1,next=(j+1)*R;double left=x[j*R];
            for(int i=1;i<=W;i++,at++,last++,next++){left=(x0[at]+a*(left+x[at+1]+x[last+1]+x[next+1]))*inv;x[at]=left;}}boundary(b,x);}}
    static void advect(int b,double[] d,double[] d0,double[] u,double[] v){for(int j=1;j<=W;j++)for(int i=1;i<=W;i++){
        int pos=j*R+i;double fx=Math.max(.5,Math.min(128.5,i-12.8*u[pos])),fy=Math.max(.5,Math.min(128.5,j-12.8*v[pos]));
        int i0=(int)Math.floor(fx),j0=(int)Math.floor(fy);double s1=fx-i0,s0=1-s1,t1=fy-j0,t0=1-t1;
        d[pos]=s0*(t0*d0[i0+j0*R]+t1*d0[i0+(j0+1)*R])+s1*(t0*d0[i0+1+j0*R]+t1*d0[i0+1+(j0+1)*R]);}boundary(b,d);}
    static void project(double[] u,double[] v,double[] p,double[] div){double h=-.5/Math.sqrt(W*W);
        for(int j=1;j<=W;j++)for(int i=1;i<=W;i++){int at=j*R+i;div[at]=h*(u[at+1]-u[at-1]+v[at+R]-v[at-R]);p[at]=0;}
        boundary(0,div);boundary(0,p);solve(0,p,div,1,4);
        for(int j=1;j<=W;j++)for(int i=1;i<=W;i++){int at=j*R+i;u[at]-=64*(p[at+1]-p[at-1]);v[at]-=64*(p[at+R]-p[at-R]);}boundary(1,u);boundary(2,v);}
    static void points(State s){for(int i=1;i<=64;i++){
        int[] indices={i+1+(i+1)*R,i+1+(65-i)*R,129-i+(65+i)*R};
        for(int k=0;k<3;k++){int at=indices[k];s.up[at]=s.vp[at]=k==0?64:-64;s.dp[at]=k==0?5:k==1?20:30;}}}
    static State update(State s){Arrays.fill(s.up,0);Arrays.fill(s.vp,0);Arrays.fill(s.dp,0);
        if(s.till==0){points(s);s.till=s.between++;}else s.till--;
        add(s.u,s.up);add(s.v,s.vp);
        // Zero diffusion copies both velocity components in the same grid walk.
        for(int j=1;j<=W;j++)for(int i=1;i<=W;i++){int at=j*R+i;s.up[at]=s.u[at];s.vp[at]=s.v[at];}
        boundary(1,s.up);boundary(2,s.vp);project(s.up,s.vp,s.u,s.v);
        advect(1,s.u,s.up,s.up,s.vp);advect(2,s.v,s.vp,s.up,s.vp);project(s.u,s.v,s.up,s.vp);
        add(s.d,s.dp);solve(0,s.dp,s.d,0,1);advect(0,s.d,s.dp,s.u,s.v);return s;}
    static int digest(State s){int digest=(int)2166136261L;for(double value:s.d){digest^=(int)Math.floor(value*1000);digest=(digest<<5)-digest+(digest>>>7);}return digest;}
    static void verify(State s){for(int i=1;i<15;i++)update(s);int sum=0;for(int i=7000;i<7100;i++)sum+=(int)(s.d[i]*10);NativeBench.check(sum==77&&digest(s)==-257786486);}
    static void run(){NativeBench.prepared(State::new,Fluid::update,Fluid::verify,s->System.out.println("__NAVIER_DENSITY_DIGEST__:"+digest(s)));}
}
