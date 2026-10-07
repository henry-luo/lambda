import java.util.ArrayList;
import java.util.Arrays;

/** Myers bisect, half matches and semantic cleanup from fast-diff.js. */
final class FastDiff {
    static final class Part {int op;String text;Part(int o,String s){op=o;text=s;}}
    static ArrayList<Part> parts(Part... values){return new ArrayList<>(Arrays.asList(values));}
    static int prefix(String a,String b){int n=0;while(n<a.length()&&n<b.length()&&a.charAt(n)==b.charAt(n))n++;return n;}
    static int suffix(String a,String b){int n=0;while(n<a.length()&&n<b.length()&&a.charAt(a.length()-1-n)==b.charAt(b.length()-1-n))n++;return n;}
    static int overlap(String a,String b){for(int n=Math.min(a.length(),b.length());n>0;n--)if(a.endsWith(b.substring(0,n)))return n;return 0;}
    static String[] seed(String longer,String shorter,int start){String seed=longer.substring(start,start+longer.length()/4);String[] best=null;
        for(int pos=shorter.indexOf(seed);pos>=0;pos=shorter.indexOf(seed,pos+1)){int pre=prefix(longer.substring(start),shorter.substring(pos)),suf=suffix(longer.substring(0,start),shorter.substring(0,pos));String middle=shorter.substring(pos-suf,pos+pre);
            if(best==null||middle.length()>best[4].length())best=new String[]{longer.substring(0,start-suf),longer.substring(start+pre),shorter.substring(0,pos-suf),shorter.substring(pos+pre),middle};}
        return best!=null&&best[4].length()*2>=longer.length()?best:null;}
    static String[] half(String a,String b){String longer=a.length()>b.length()?a:b,shorter=a.length()>b.length()?b:a;
        if(longer.length()<4||shorter.length()*2<longer.length())return null;String[] one=seed(longer,shorter,(longer.length()+3)/4),two=seed(longer,shorter,(longer.length()+1)/2);
        String[] best=two==null||one!=null&&one[4].length()>two[4].length()?one:two;
        if(best==null||a.length()>b.length())return best;return new String[]{best[2],best[3],best[0],best[1],best[4]};}
    static ArrayList<Part> split(String a,String b,int x,int y){ArrayList<Part> out=main(a.substring(0,x),b.substring(0,y),false);out.addAll(main(a.substring(x),b.substring(y),false));return out;}
    static ArrayList<Part> bisect(String a,String b){int n=a.length(),m=b.length(),max=(n+m+1)/2,offset=max,len=2*max,delta=n-m;int[] f=new int[len],r=new int[len];Arrays.fill(f,-1);Arrays.fill(r,-1);f[offset+1]=r[offset+1]=0;boolean front=delta%2!=0;int fs=0,fe=0,rs=0,re=0;
        for(int d=0;d<max;d++){for(int k=-d+fs;k<=d-fe;k+=2){int p=offset+k,x=k==-d||k!=d&&f[p-1]<f[p+1]?f[p+1]:f[p-1]+1,y=x-k;
                while(x<n&&y<m&&a.charAt(x)==b.charAt(y)){x++;y++;}f[p]=x;
                if(x>n)fe+=2;else if(y>m)fs+=2;else if(front){int rp=offset+delta-k;if(rp>=0&&rp<len&&r[rp]!=-1&&x>=n-r[rp])return split(a,b,x,y);}}
            for(int k=-d+rs;k<=d-re;k+=2){int p=offset+k,x=k==-d||k!=d&&r[p-1]<r[p+1]?r[p+1]:r[p-1]+1,y=x-k;
                while(x<n&&y<m&&a.charAt(n-x-1)==b.charAt(m-y-1)){x++;y++;}r[p]=x;
                if(x>n)re+=2;else if(y>m)rs+=2;else if(!front){int fp=offset+delta-k;if(fp>=0&&fp<len&&f[fp]!=-1){int sx=f[fp],sy=offset+sx-fp;if(sx>=n-x)return split(a,b,sx,sy);}}}}
        return parts(new Part(-1,a),new Part(1,b));}
    static ArrayList<Part> compute(String a,String b){if(a.isEmpty())return parts(new Part(1,b));if(b.isEmpty())return parts(new Part(-1,a));String longer=a.length()>b.length()?a:b,shorter=a.length()>b.length()?b:a;int pos=longer.indexOf(shorter),kind=a.length()>b.length()?-1:1;
        if(pos>=0)return parts(new Part(kind,longer.substring(0,pos)),new Part(0,shorter),new Part(kind,longer.substring(pos+shorter.length())));
        if(shorter.length()==1)return parts(new Part(-1,a),new Part(1,b));String[] h=half(a,b);if(h!=null){ArrayList<Part> out=main(h[0],h[2],false);out.add(new Part(0,h[4]));out.addAll(main(h[1],h[3],false));return out;}return bisect(a,b);}
    static void splice(ArrayList<Part> p,int start,int count,Part... replacements){p.subList(start,start+count).clear();p.addAll(start,Arrays.asList(replacements));}
    static void merge(ArrayList<Part> p){p.add(new Part(0,""));int at=0,inserts=0,deletes=0;String inserted="",removed="";
        while(at<p.size()){if(at<p.size()-1&&p.get(at).text.isEmpty()){p.remove(at);continue;}Part part=p.get(at);
            if(part.op==1){inserts++;inserted+=part.text;at++;}else if(part.op==-1){deletes++;removed+=part.text;at++;}else{
                int previous=at-inserts-deletes-1;if(!removed.isEmpty()||!inserted.isEmpty()){
                    if(!removed.isEmpty()&&!inserted.isEmpty()){int pre=prefix(inserted,removed);if(pre>0){if(previous>=0)p.get(previous).text+=inserted.substring(0,pre);else{p.add(0,new Part(0,inserted.substring(0,pre)));at++;}inserted=inserted.substring(pre);removed=removed.substring(pre);}
                        int suf=suffix(inserted,removed);if(suf>0){p.get(at).text=inserted.substring(inserted.length()-suf)+p.get(at).text;inserted=inserted.substring(0,inserted.length()-suf);removed=removed.substring(0,removed.length()-suf);}}
                    ArrayList<Part> replacement=new ArrayList<>();if(!removed.isEmpty())replacement.add(new Part(-1,removed));if(!inserted.isEmpty())replacement.add(new Part(1,inserted));int count=inserts+deletes;splice(p,at-count,count,replacement.toArray(Part[]::new));at=at-count+replacement.size();}
                if(at>0&&p.get(at-1).op==0){p.get(at-1).text+=p.get(at).text;p.remove(at);}else at++;inserts=deletes=0;inserted=removed="";}}
        if(!p.isEmpty()&&p.get(p.size()-1).text.isEmpty())p.remove(p.size()-1);boolean changed=false;
        for(at=1;at<p.size()-1;at++)if(p.get(at-1).op==0&&p.get(at+1).op==0){String before=p.get(at-1).text,edit=p.get(at).text,after=p.get(at+1).text;
            if(edit.endsWith(before)){p.get(at).text=before+edit.substring(0,edit.length()-before.length());p.get(at+1).text=before+after;p.remove(at-1);changed=true;}
            else if(edit.startsWith(after)){p.get(at-1).text+=after;p.get(at).text=edit.substring(after.length())+after;p.remove(at+1);changed=true;}}
        if(changed)merge(p);}
    static int score(String left,String right){if(left.isEmpty()||right.isEmpty())return 6;char a=left.charAt(left.length()-1),b=right.charAt(0);boolean na=!Character.isLetterOrDigit(a),nb=!Character.isLetterOrDigit(b),wa=na&&" \t\n\r".indexOf(a)>=0,wb=nb&&" \t\n\r".indexOf(b)>=0,la=wa&&(a=='\n'||a=='\r'),lb=wb&&(b=='\n'||b=='\r');
        if(la&&(left.endsWith("\n\n")||left.endsWith("\n\r\n"))||lb&&(right.startsWith("\n\n")||right.startsWith("\r\n\r\n")))return 5;if(la||lb)return 4;if(na&&!wa&&wb)return 3;if(wa||wb)return 2;if(na||nb)return 1;return 0;}
    static void lossless(ArrayList<Part> p){for(int at=1;at<p.size()-1;at++)if(p.get(at-1).op==0&&p.get(at+1).op==0){String left=p.get(at-1).text,edit=p.get(at).text,right=p.get(at+1).text;int suf=suffix(left,edit);
            if(suf>0){String common=edit.substring(edit.length()-suf);left=left.substring(0,left.length()-suf);edit=common+edit.substring(0,edit.length()-suf);right=common+right;}
            String bl=left,be=edit,br=right;int best=score(left,edit)+score(edit,right);while(!edit.isEmpty()&&!right.isEmpty()&&edit.charAt(0)==right.charAt(0)){left+=edit.substring(0,1);edit=edit.substring(1)+right.substring(0,1);right=right.substring(1);int current=score(left,edit)+score(edit,right);if(current>=best){bl=left;be=edit;br=right;best=current;}}
            if(!p.get(at-1).text.equals(bl)){if(!bl.isEmpty())p.get(at-1).text=bl;else{p.remove(at-1);at--;}p.get(at).text=be;if(!br.isEmpty())p.get(at+1).text=br;else{p.remove(at+1);at--;}}}}
    static void semantic(ArrayList<Part> p){ArrayList<Integer> equalities=new ArrayList<>();String last="";int bi=0,bd=0,ai=0,ad=0;boolean changed=false;
        for(int at=0;at<p.size();at++){Part part=p.get(at);if(part.op==0){equalities.add(at);bi=ai;bd=ad;ai=ad=0;last=part.text;}else{if(part.op==1)ai+=part.text.length();else ad+=part.text.length();
            if(!last.isEmpty()&&last.length()<=Math.max(bi,bd)&&last.length()<=Math.max(ai,ad)){int pos=equalities.get(equalities.size()-1);p.add(pos,new Part(-1,last));p.get(pos+1).op=1;equalities.remove(equalities.size()-1);if(!equalities.isEmpty())equalities.remove(equalities.size()-1);at=equalities.isEmpty()?-1:equalities.get(equalities.size()-1);bi=bd=ai=ad=0;last="";changed=true;}}}
        if(changed)merge(p);lossless(p);for(int at=1;at<p.size();at++){if(p.get(at-1).op!=-1||p.get(at).op!=1)continue;String removed=p.get(at-1).text,inserted=p.get(at).text;int forward=overlap(removed,inserted),reverse=overlap(inserted,removed);
            if(forward>=reverse&&(forward*2>=removed.length()||forward*2>=inserted.length())){p.add(at,new Part(0,inserted.substring(0,forward)));p.get(at-1).text=removed.substring(0,removed.length()-forward);p.get(at+1).text=inserted.substring(forward);at++;}
            else if(reverse>forward&&(reverse*2>=removed.length()||reverse*2>=inserted.length())){p.add(at,new Part(0,removed.substring(0,reverse)));p.set(at-1,new Part(1,inserted.substring(0,inserted.length()-reverse)));p.set(at+1,new Part(-1,removed.substring(reverse)));at++;}at++;}}
    static ArrayList<Part> main(String a,String b,boolean cleanup){if(a.equals(b))return a.isEmpty()?parts():parts(new Part(0,a));int pre=prefix(a,b);String leading=a.substring(0,pre);a=a.substring(pre);b=b.substring(pre);int suf=suffix(a,b);String trailing=a.substring(a.length()-suf);a=a.substring(0,a.length()-suf);b=b.substring(0,b.length()-suf);
        ArrayList<Part> out=compute(a,b);if(!leading.isEmpty())out.add(0,new Part(0,leading));if(!trailing.isEmpty())out.add(new Part(0,trailing));merge(out);if(cleanup)semantic(out);return out;}
    static java.util.List<Object> prepare(){return JsonData.array(JsonData.load("fast_diff_pairs.json"));}
    static long work(java.util.List<Object> pairs){long sum=0;for(int r=0;r<256;r++)for(Object value:pairs){java.util.List<Object> pair=JsonData.array(value);ArrayList<Part> p=main((String)pair.get(0),(String)pair.get(1),true);sum=(sum+p.size()*17)%1000000007;for(Part part:p)sum=(sum+part.op*31+part.text.length())%1000000007;}return sum;}
}
