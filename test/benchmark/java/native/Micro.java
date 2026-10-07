import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Same scalar algorithms/counts as SUITE.md; primitive arrays and local scalars. */
final class Micro {
    static final long MOD=1000000007L;
    static String decimal(long value) {
        boolean negative=value<0;if(negative)value=-value;
        long divisor=1;while(value/divisor>=10)divisor*=10;
        StringBuilder out=new StringBuilder(12);if(negative)out.append('-');
        while(divisor>0){out.append((char)(48+value/divisor));value%=divisor;divisor/=10;}
        return out.toString();
    }
    static long parse(String text) {
        boolean negative=text.charAt(0)=='-';long value=0;
        for(int i=negative?1:0;i<text.length();i++)value=value*10+text.charAt(i)-48;
        return negative?-value:value;
    }
    static long[] integers() {
        long seed=42,checksum=0,size=0,errors=0;
        for(int i=0;i<100000;i++) {
            seed=seed*16807%2147483647;long value=i%8==0?0:i%8==1?-seed:seed;
            String text=decimal(value);long parsed=parse(text);
            if(parsed!=value)errors++;size+=text.length();checksum=(checksum*31+parsed+2147483647)%MOD;
        }
        return new long[]{checksum,size,seed,errors};
    }
    static double[] gram(double[] a,int rows,int cols) {
        double[] out=new double[cols*cols];
        for(int i=0;i<cols;i++)for(int j=0;j<cols;j++) {
            double total=0;for(int k=0;k<rows;k++)total+=a[k*cols+i]*a[k*cols+j];out[i*cols+j]=total;
        }
        return out;
    }
    static double[] square(double[] a,int n) {
        double[] out=new double[n*n];
        for(int i=0;i<n;i++)for(int j=0;j<n;j++) {
            double total=0;for(int k=0;k<n;k++)total+=a[i*n+k]*a[k*n+j];out[i*n+j]=total;
        }
        return out;
    }
    static double trace(double[] a,int rows,int cols) {
        double[] fourth=square(square(gram(a,rows,cols),cols),cols);
        double result=0;for(int i=0;i<cols;i++)result+=fourth[i*cols+i];return result;
    }
    static double variation(double[] values) {
        double total=0;for(double v:values)total+=v;double mean=total/values.length;total=0;
        for(double v:values){double d=v-mean;total+=d*d;}return Math.sqrt(total/(values.length-1))/mean;
    }
    static long[] matrices() {
        long seed=42,digest=0;double[] v=new double[1000],w=new double[1000];
        for(int iteration=0;iteration<1000;iteration++) {
            double[] blocks=new double[100],p=new double[100],q=new double[100];
            for(int i=0;i<100;i++){seed=seed*16807%2147483647;blocks[i]=seed/2147483647.0*2-1;}
            for(int b=0;b<4;b++)for(int r=0;r<5;r++)for(int c=0;c<5;c++) {
                double value=blocks[b*25+r*5+c];p[r*20+b*5+c]=value;q[(b/2*5+r)*10+b%2*5+c]=value;
            }
            v[iteration]=trace(p,5,20);w[iteration]=trace(q,10,10);
            digest=(digest*31+(long)Math.floor(v[iteration]*1000))%MOD;
            digest=(digest*31+(long)Math.floor(w[iteration]*1000))%MOD;
        }
        return new long[]{(long)Math.floor(variation(v)*1e9),(long)Math.floor(variation(w)*1e9),digest,seed};
    }
    static long[] pi() {
        double[] values=new double[500];
        for(int r=0;r<500;r++){double total=0;for(int k=1;k<=10000+r;k++)total+=1.0/((double)k*k);values[r]=total;}
        double digest=0;for(int i=0;i<500;i++)digest+=values[i]*(i+1);
        return new long[]{(long)Math.floor(values[0]*1e12),(long)Math.floor(values[499]*1e12),(long)Math.floor(digest*1e6),5124750};
    }
    static long[] formatted() {
        long size=0,digest=0,writes=0;StringBuilder buffer=new StringBuilder(4096);
        String sink=System.getProperty("os.name").startsWith("Windows")?"NUL":"/dev/null";
        try {
            for(int i=1;i<=100000;i++) {
                String line=decimal(i)+" "+decimal(i+1)+"\n";
                for(int j=0;j<line.length();j++)digest=(digest*31+line.charAt(j))%MOD;
                size+=line.length();buffer.append(line);
                if(i%256==0||i==100000) {
                    // Each batch retains the contract's synchronous open/write/close.
                    try(FileOutputStream out=new FileOutputStream(sink)){out.write(buffer.toString().getBytes(StandardCharsets.US_ASCII));}
                    writes++;buffer.setLength(0);
                }
            }
        } catch(java.io.IOException ex) { throw new java.io.UncheckedIOException(ex); }
        return new long[]{size,digest,writes,100000};
    }
    static void run(String name) {
        long[] expected=switch(name) {
            case "parse_integers" -> new long[]{592470661,854479,1966931148,0};
            case "matrix_statistics" -> new long[]{464726438,486656926,47509838,1966931148};
            case "iteration_pi_sum" -> new long[]{1644834071848L,1644838824217L,206015869118L,5124750};
            case "formatted_output" -> new long[]{1177795,584298900,391,100000};
            default -> throw new IllegalArgumentException(name);
        };
        java.util.function.Supplier<long[]> work=switch(name) {
            case "parse_integers" -> Micro::integers;case "matrix_statistics" -> Micro::matrices;
            case "iteration_pi_sum" -> Micro::pi;default -> Micro::formatted;
        };
        NativeBench.run(work,v->NativeBench.check(Arrays.equals(v,expected)),v->System.out.println(name+": PASS "+v[0]+" "+v[1]+" "+v[2]+" "+v[3]),true);
    }
}
