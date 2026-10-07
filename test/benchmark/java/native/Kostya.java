import java.util.Arrays;

final class Kostya {
    static final String TABLE="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static final String BF="++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.";
    static long next(long seed){return(seed*1664525+1013904223)%1000000;}
    static int primes(int n){
        boolean[] flags=new boolean[n+1];Arrays.fill(flags,true);flags[0]=flags[1]=false;
        for(int i=2;i*i<=n;i++)if(flags[i])for(int j=i*i;j<=n;j+=i)flags[j]=false;
        int count=0;for(boolean v:flags)if(v)count++;return count;
    }
    static long collatz(){
        int longest=0,start=0;
        for(int candidate=1;candidate<1000000;candidate++){
            long value=candidate;int length=1;
            while(value!=1){value=(value&1)==0?value/2:value*3+1;length++;}
            if(length>longest){longest=length;start=candidate;}
        }
        return start;
    }
    static String base64(byte[] bytes){
        char[] out=new char[(bytes.length+2)/3*4];int i=0,p=0;
        while(i+2<bytes.length){int a=bytes[i++]&255,b=bytes[i++]&255,c=bytes[i++]&255;
            out[p++]=TABLE.charAt(a>>2);out[p++]=TABLE.charAt((a&3)<<4|b>>4);
            out[p++]=TABLE.charAt((b&15)<<2|c>>6);out[p++]=TABLE.charAt(c&63);}
        if(i<bytes.length){int a=bytes[i++]&255;out[p++]=TABLE.charAt(a>>2);
            if(i<bytes.length){int b=bytes[i]&255;out[p++]=TABLE.charAt((a&3)<<4|b>>4);out[p++]=TABLE.charAt((b&15)<<2);}
            else{out[p++]=TABLE.charAt((a&3)<<4);out[p++]='=';}out[p]='=';}
        return new String(out);
    }
    static int[] base64Work(){
        byte[] input=new byte[10000];Arrays.fill(input,(byte)97);String encoded="";int decoded=0;
        for(int i=0;i<100;i++){encoded=base64(input);decoded=encoded.length()/4*3;
            if(encoded.endsWith("="))decoded--;if(encoded.endsWith("=="))decoded--;NativeBench.observed=encoded;}
        return new int[]{encoded.length(),decoded};
    }
    static int distance(String left,String right){
        int[] previous=new int[right.length()+1],current=new int[right.length()+1];
        for(int j=0;j<previous.length;j++)previous[j]=j;
        for(int i=1;i<=left.length();i++){
            current[0]=i;char c=left.charAt(i-1);
            for(int j=1;j<=right.length();j++)current[j]=Math.min(Math.min(previous[j]+1,current[j-1]+1),previous[j-1]+(c==right.charAt(j-1)?0:1));
            int[] swap=previous;previous=current;current=swap;
        }
        return previous[right.length()];
    }
    static long matmul(){
        int n=200;double[] a=new double[n*n],b=new double[n*n],c=new double[n*n];long seed=42;
        for(int i=0;i<a.length;i++){seed=next(seed);a[i]=(seed%2000)/1000.0-1;seed=next(seed);b[i]=(seed%2000)/1000.0-1;}
        for(int i=0;i<n;i++)for(int j=0;j<n;j++){double total=0;for(int k=0;k<n;k++)total+=a[i*n+k]*b[k*n+j];c[i*n+j]=total;}
        double total=0;for(double v:c)total+=v;NativeBench.observed=c;return(long)Math.floor(total);
    }
    static int[] jumps(String program){
        int[] jumps=new int[program.length()],stack=new int[program.length()];int depth=0;
        for(int i=0;i<program.length();i++)if(program.charAt(i)=='[')stack[depth++]=i;
            else if(program.charAt(i)==']'){int open=stack[--depth];jumps[i]=open;jumps[open]=i;}
        if(depth!=0)throw new IllegalArgumentException("unbalanced brainfuck program");return jumps;
    }
    static String brainfuck(String program,int[] jumps){
        byte[] tape=new byte[30000];int pointer=0;StringBuilder out=new StringBuilder();
        for(int ip=0;ip<program.length();ip++)switch(program.charAt(ip)){
            case '+'->tape[pointer]++;case '-'->tape[pointer]--;case '>'->pointer++;case '<'->pointer--;
            case '.'->out.append((char)(tape[pointer]&255));case '['->{if(tape[pointer]==0)ip=jumps[ip];}
            case ']'->{if(tape[pointer]!=0)ip=jumps[ip];}default->{}
        }
        return out.toString();
    }
    static String brainfuckWork(){int[] jumps=jumps(BF);String out="";
        for(int i=0;i<10000;i++){out=brainfuck(BF,jumps);NativeBench.observed=out;}return out;}
    static int json(){
        int length=0;
        for(int repeat=0;repeat<10;repeat++){
            long seed=42;StringBuilder out=new StringBuilder(65000);out.append('[');
            for(int i=0;i<1000;i++){
                if(i>0)out.append(',');seed=next(seed);long id=seed%10000;
                seed=next(seed);long x=(seed%20000-10000)/100;
                seed=next(seed);long y=(seed%20000-10000)/100;
                seed=next(seed);long score=seed%100;
                out.append("{\"id\":").append(id).append(",\"score\":").append(score)
                    .append(",\"coord\":{\"x\":").append(x).append(",\"y\":").append(y).append("},\"active\":true}");
            }
            out.append(']');length=out.length();NativeBench.observed=out.toString();
        }
        return length;
    }
    static void run(String name){
        switch(name){
            case "primes"->NativeBench.run(()->primes(1000000),v->NativeBench.check(v==78498),v->System.out.println("primes: PASS ("+v+")"),NativeBench.warmup());
            case "collatz"->NativeBench.run(Kostya::collatz,v->NativeBench.check(v==837799),v->System.out.println("collatz: PASS (start="+v+")"),NativeBench.warmup());
            case "base64"->NativeBench.run(Kostya::base64Work,v->NativeBench.check(Arrays.equals(v,new int[]{13336,10000})),v->System.out.println("base64: encoded_len="+v[0]+" decoded_len="+v[1]+"\nbase64: PASS"),NativeBench.warmup());
            case "levenshtein"->NativeBench.run(()->new int[]{distance("kitten","sitting"),distance("saturday","sunday"),distance("a".repeat(500),"b".repeat(500)),distance("ab".repeat(200),"ba".repeat(200))},
                v->NativeBench.check(Arrays.equals(v,new int[]{3,3,500,2})),v->System.out.println("levenshtein: d(kitten,sitting)="+v[0]+"\nlevenshtein: d(saturday,sunday)="+v[1]+"\nlevenshtein: d(aaa...,bbb...)="+v[2]+"\nlevenshtein: d(ababab...,babab...)="+v[3]+"\nlevenshtein: PASS"),NativeBench.warmup());
            case "matmul"->NativeBench.run(Kostya::matmul,v->NativeBench.check(v==-29562),v->System.out.println("matmul: sum="+v+"\nmatmul: DONE"),NativeBench.warmup());
            case "brainfuck"->NativeBench.run(Kostya::brainfuckWork,v->NativeBench.check(v.equals("Hello World!\n")),System.out::print,NativeBench.warmup());
            case "json_gen"->NativeBench.run(Kostya::json,v->NativeBench.check(v==61626),v->System.out.println("json_gen: length="+v+"\njson_gen: PASS"),NativeBench.warmup());
            default->throw new IllegalArgumentException(name);
        }
    }
}
