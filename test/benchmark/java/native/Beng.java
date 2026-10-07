import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;

final class Beng {
    static final class Tree { final Tree left,right;Tree(Tree l,Tree r){left=l;right=r;} }
    static Tree tree(int depth){return depth==0?new Tree(null,null):new Tree(tree(depth-1),tree(depth-1));}
    static int checkTree(Tree t){return t.left==null?1:1+checkTree(t.left)+checkTree(t.right);}
    static String binarytrees(){
        StringBuilder out=new StringBuilder();out.append("stretch tree of depth 11\t check: ").append(checkTree(tree(11))).append('\n');
        Tree longLived=tree(10);
        for(int depth=4;depth<=10;depth+=2){int count=1<<(14-depth),total=0;
            for(int i=0;i<count;i++)total+=checkTree(tree(depth));
            out.append(count).append("\t trees of depth ").append(depth).append("\t check: ").append(total).append('\n');}
        out.append("long lived tree of depth 10\t check: ").append(checkTree(longLived)).append('\n');return out.toString();
    }
    static String fannkuch(){
        int n=7,r=n,checksum=0,max=0,count=0;int[] p=new int[n],work=new int[n],rotations=new int[n];
        for(int i=0;i<n;i++)p[i]=i;
        while(true){
            while(r!=1){rotations[r-1]=r;r--;}
            System.arraycopy(p,0,work,0,n);int flips=0;
            for(int k=work[0];k!=0;k=work[0]){for(int a=0,b=k;a<b;a++,b--){int t=work[a];work[a]=work[b];work[b]=t;}flips++;}
            max=Math.max(max,flips);checksum+=(count++&1)==0?flips:-flips;r=1;
            while(r<n){int first=p[0];System.arraycopy(p,1,p,0,r);p[r]=first;if(--rotations[r]>0)break;r++;}
            if(r==n)return checksum+"\nPfannkuchen(7) = "+max+"\n";
        }
    }
    static void multiply(double[] input,double[] output,boolean transpose){
        int n=input.length;for(int i=0;i<n;i++){double total=0;for(int j=0;j<n;j++){
            int row=transpose?j:i,col=transpose?i:j,sum=row+col;
            total+=(1.0/(sum*(sum+1)/2+row+1))*input[j]; }output[i]=total;}
    }
    static void ata(double[] a,double[] b,double[] tmp){multiply(a,tmp,false);multiply(tmp,b,true);}
    static String spectral(){double[] u=new double[100],v=new double[100],tmp=new double[100];Arrays.fill(u,1);
        for(int i=0;i<10;i++){ata(u,v,tmp);ata(v,u,tmp);}double uv=0,vv=0;for(int i=0;i<100;i++){uv+=u[i]*v[i];vv+=v[i]*v[i];}
        return String.format(Locale.ROOT,"%.9f\n",Math.sqrt(uv/vv));}
    static String pidigits(){
        BigInteger q=BigInteger.ONE,r=BigInteger.ZERO,s=BigInteger.ZERO,t=BigInteger.ONE;
        BigInteger two=BigInteger.TWO,three=BigInteger.valueOf(3),four=BigInteger.valueOf(4),ten=BigInteger.TEN;
        int k=0,index=0;StringBuilder out=new StringBuilder(),digits=new StringBuilder();
        while(index<30){k++;BigInteger kk=BigInteger.valueOf(k),k2=BigInteger.valueOf(2L*k+1);
            BigInteger nq=q.multiply(kk),nr=q.multiply(two).add(r).multiply(k2),ns=s.multiply(kk),nt=s.multiply(two).add(t).multiply(k2);
            q=nq;r=nr;s=ns;t=nt;if(q.compareTo(r)>0)continue;
            BigInteger d3=q.multiply(three).add(r).divide(s.multiply(three).add(t)),d4=q.multiply(four).add(r).divide(s.multiply(four).add(t));
            if(!d3.equals(d4))continue;digits.append(d3);index++;
            if(index%10==0){out.append(digits).append("\t:").append(index).append('\n');digits.setLength(0);}
            r=r.subtract(d3.multiply(t)).multiply(ten);q=q.multiply(ten);
        }
        return out.toString();
    }
    static final String ALU="GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";
    static int randomFasta(StringBuilder out,String header,String letters,double[] probabilities,int count,int seed){
        out.append(header).append('\n');double[] cumulative=probabilities.clone();for(int i=1;i<cumulative.length;i++)cumulative[i]+=cumulative[i-1];
        for(int i=0;i<count;i++){seed=(seed*3877+29573)%139968;double value=(double)seed/139968;int choice=0;
            while(choice<letters.length()-1&&cumulative[choice]<value)choice++;out.append(letters.charAt(choice));
            if(i%60==59||i==count-1)out.append('\n');}
        return seed;
    }
    static String fasta(){StringBuilder out=new StringBuilder(11000);out.append(">ONE Homo sapiens alu\n");
        for(int i=0;i<2000;i++){out.append(ALU.charAt(i%ALU.length()));if(i%60==59||i==1999)out.append('\n');}
        int seed=randomFasta(out,">TWO IUB ambiguity codes","acgtBDHKMNRSVWY",new double[]{.27,.12,.12,.27,.02,.02,.02,.02,.02,.02,.02,.02,.02,.02,.02},3000,42);
        randomFasta(out,">THREE Homo sapiens frequency","acgt",new double[]{.3029549426680,.1979883004921,.1975473066391,.3015094502008},5000,seed);return out.toString();}
    record Record(String header,String sequence){}
    static java.util.List<Record> records(String raw){ArrayList<Record> out=new ArrayList<>();String header=null;StringBuilder seq=new StringBuilder();
        for(String line:raw.split("\\R")){if(line.startsWith(">")){if(header!=null)out.add(new Record(header,seq.toString()));header=line.substring(1);seq.setLength(0);}else if(header!=null)seq.append(line);}
        if(header!=null)out.add(new Record(header,seq.toString()));return out;}
    static String knucleotide(String raw){java.util.List<Record> records=records(raw);String seq=records.get(records.size()-1).sequence.toUpperCase(Locale.ROOT);StringBuilder out=new StringBuilder();
        for(int width:new int[]{1,2}){HashMap<String,Integer> counts=new HashMap<>();for(int i=0;i+width<=seq.length();i++)counts.merge(seq.substring(i,i+width),1,Integer::sum);
            ArrayList<Map.Entry<String,Integer>> entries=new ArrayList<>(counts.entrySet());entries.sort((a,b)->a.getValue().equals(b.getValue())?a.getKey().compareTo(b.getKey()):Integer.compare(b.getValue(),a.getValue()));
            for(var e:entries)out.append(String.format(Locale.ROOT,"%s %.3f\n",e.getKey(),100.0*e.getValue()/(seq.length()-width+1)));out.append('\n');}
        for(String needle:new String[]{"GGT","GGTA","GGTATT","GGTATTTTAATT","GGTATTTTAATTTATAGT"}){int count=0;for(int i=0;i+needle.length()<=seq.length();i++)if(seq.regionMatches(i,needle,0,needle.length()))count++;out.append(count).append('\t').append(needle).append('\n');}
        return out.toString();}
    static final String[] PATTERNS={"agggtaaa|tttaccct","[cgt]gggtaaa|tttaccc[acg]","a[act]ggtaaa|tttacc[agt]t","ag[act]gtaaa|tttac[agt]ct","agg[act]taaa|ttta[agt]cct","aggg[acg]aaa|ttt[cgt]ccct","agggt[cgt]aa|tt[acg]taccct","agggta[cgt]a|t[acg]ataccct","agggtaa[cgt]|[acg]aataccct"};
    static String regex(String raw){StringBuilder bare=new StringBuilder();for(Record r:records(raw))bare.append(r.sequence);String seq=bare.toString().toLowerCase(Locale.ROOT);StringBuilder out=new StringBuilder();
        for(String pattern:PATTERNS){var m=Pattern.compile(pattern).matcher(seq);int count=0;while(m.find())count++;out.append(pattern).append(' ').append(count).append('\n');}
        String expanded=bare.toString();String[] from={"B","D","H","K","M","N","R","S","V","W","Y"},to={"(c|g|t)","(a|g|t)","(a|c|t)","(g|t)","(a|c)","(a|c|g|t)","(a|g)","(c|g)","(a|c|g)","(a|t)","(c|t)"};
        for(int i=0;i<from.length;i++)expanded=expanded.replace(from[i],to[i]);out.append('\n').append(raw.length()).append('\n').append(seq.length()).append('\n').append(expanded.length()).append('\n');return out.toString();}
    static char complement(char c){return switch(Character.toUpperCase(c)){case 'A'->'T';case 'T'->'A';case 'C'->'G';case 'G'->'C';case 'M'->'K';case 'K'->'M';case 'R'->'Y';case 'Y'->'R';case 'V'->'B';case 'B'->'V';case 'H'->'D';case 'D'->'H';default->Character.toUpperCase(c);};}
    static String mandelbrot(){int checksum=0,accumulator=0,bits=0;
        for(int y=0;y<500;y++)for(int x=0;x<500;x++){
            double cr=2.0*x/500-1.5,ci=2.0*y/500-1,zr=0,zi=0,rr=0,ii=0;boolean escaped=false;
            for(int i=0;i<50&&!escaped;i++){double next=rr-ii+cr;zi=2*zr*zi+ci;zr=next;rr=zr*zr;ii=zi*zi;escaped=rr+ii>4;}
            accumulator=(accumulator<<1)|(escaped?0:1);bits++;
            if(bits==8||x==499){checksum^=accumulator<<(8-bits);accumulator=bits=0;}
        }return checksum+"\n";}
    static String revcomp(String raw){StringBuilder out=new StringBuilder(raw.length());for(Record r:records(raw)){out.append('>').append(r.header).append('\n');int n=r.sequence.length();
        for(int i=0;i<n;i++){out.append(complement(r.sequence.charAt(n-1-i)));if(i%60==59||i==n-1)out.append('\n');}}return out.toString();}
    static void run(String name)throws Exception{
        String expected=Files.readString(Path.of("test/benchmark/beng/"+name+".txt"));
        String raw=java.util.Set.of("knucleotide","regexredux","revcomp").contains(name)?Files.readString(Path.of("test/benchmark/beng/input/fasta_1000.txt")):null;
        java.util.function.Supplier<String> work=switch(name){case "binarytrees"->Beng::binarytrees;case "fannkuch"->Beng::fannkuch;
            case "spectralnorm"->Beng::spectral;case "pidigits"->Beng::pidigits;case "fasta"->Beng::fasta;
            case "knucleotide"->()->knucleotide(raw);case "regexredux"->()->regex(raw);case "revcomp"->()->revcomp(raw);
            case "mandelbrot"->Beng::mandelbrot;
            case "nbody"->()->{nbody.NBodySystem s=new nbody.NBodySystem();double before=s.energy();for(int i=0;i<36000;i++)s.advance(.01);return String.format(Locale.ROOT,"%.9f\n%.9f\n",before,s.energy());};
            default->throw new IllegalArgumentException(name);};
        NativeBench.run(work,v->NativeBench.check(v.strip().equals(expected.strip())),System.out::print,NativeBench.warmup());
    }
}
