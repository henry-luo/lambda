import java.util.HashMap;

final class Hyphen {
    static final class Tables {
        int root;int[] nodeFirst,nodeCount,edgeCode,edgeChild,nodeLevel,levelOffsets,levelLengths,levelValues;
        java.util.Map<String,int[]> exceptions=new HashMap<>();
        static int[] ints(Object v){return JsonData.array(v).stream().mapToInt(x->((Number)x).intValue()).toArray();}
        Tables(java.util.Map<String,Object> m){root=((Number)m.get("root")).intValue();nodeFirst=ints(m.get("node_first"));nodeCount=ints(m.get("node_count"));edgeCode=ints(m.get("edge_code"));edgeChild=ints(m.get("edge_child"));nodeLevel=ints(m.get("node_level"));levelOffsets=ints(m.get("level_offsets"));levelLengths=ints(m.get("level_lengths"));levelValues=ints(m.get("level_values"));
            int[] markers=ints(m.get("exception_markers")),offsets=ints(m.get("exception_offsets")),counts=ints(m.get("exception_counts"));java.util.List<Object> words=JsonData.array(m.get("exception_words"));
            for(int i=0;i<words.size();i++)exceptions.put((String)words.get(i),java.util.Arrays.copyOfRange(markers,offsets[i],offsets[i]+counts[i]));}
        int child(int node,int code){int first=nodeFirst[node];for(int edge=first;edge<first+nodeCount[node];edge++)if(edgeCode[edge]==code)return edgeChild[edge];return -1;}
    }
    static final class Hyphenator {
        final Tables t;final HashMap<String,int[]> markerCache=new HashMap<>();final HashMap<String,String> wordCache=new HashMap<>();
        Hyphenator(Tables tables){t=tables;}
        int[] markers(String word){String lower=word.toLowerCase(java.util.Locale.ROOT);int[] found=t.exceptions.get(lower);if(found!=null)return found;found=markerCache.get(lower);if(found!=null)return found;
            int len=word.length();int[] levels=new int[len+1];String ext="."+lower+".";
            for(int start=0;start<len;start++){int node=t.root,pos=start==0?0:start-1;for(int cursor=start;cursor<len+2;cursor++){
                node=t.child(node,ext.charAt(cursor));if(node<0)break;int level=t.nodeLevel[node];if(level>=0)for(int i=0;i<t.levelLengths[level];i++){int target=pos+i,value=t.levelValues[t.levelOffsets[level]+i];if(target>=0&&target<=len&&value>levels[target])levels[target]=value;}}}
            levels[0]=levels[1]=levels[len-1]=levels[len]=0;java.util.ArrayList<Integer> out=new java.util.ArrayList<>();for(int i=0;i<levels.length;i++)if((levels[i]&1)!=0)out.add(i);
            found=out.stream().mapToInt(Integer::intValue).toArray();markerCache.put(lower,found);return found;}
        String word(String word){String found=wordCache.get(word);if(found!=null)return found;String result=word;
            if(word.length()>=5&&!word.contains("-")){int[] marks=markers(word);int mi=0;StringBuilder out=new StringBuilder();for(int i=0;i<word.length();i++){if(mi<marks.length&&marks[mi]==i){out.append('-');mi++;}out.append(word.charAt(i));}while(mi++<marks.length)out.append('-');result=out.toString();}
            wordCache.put(word,result);return result;}
        String text(String source){StringBuilder out=new StringBuilder();int i=0;while(i<source.length()){char c=source.charAt(i);
            if(c=='<'&&i+1<source.length()&&(letter(source.charAt(i+1))||source.charAt(i+1)=='/')){int end=source.indexOf('>',i);if(end<0)end=source.length()-1;out.append(source,i,end+1);i=end+1;}
            else if(wordChar(c)){int start=i;while(i<source.length()){c=source.charAt(i);if(wordChar(c)||(c=='-'&&i>start&&i+1<source.length()&&letter(source.charAt(i+1))))i++;else break;}out.append(word(source.substring(start,i)));}
            else{out.append(c);i++;}}return out.toString();}
    }
    static boolean letter(char c){return c>='A'&&c<='Z'||c>='a'&&c<='z';}
    static boolean wordChar(char c){return letter(c)||c=='\'';}
    record State(Tables tables,java.util.List<Object> cases){}
    static State prepare(){State s=new State(new Tables(JsonData.object(JsonData.load("hyphen_tables.json"))),JsonData.array(JsonData.load("hyphen_cases.json")));Hyphenator h=new Hyphenator(s.tables);for(Object value:s.cases){java.util.List<Object> c=JsonData.array(value);NativeBench.check(h.text((String)c.get(0)).equals(c.get(1)));}return s;}
    static long work(State s){long checksum=0;for(int r=0;r<32;r++){Hyphenator h=new Hyphenator(s.tables);for(int i=0;i<s.cases.size();i++){String result=h.text((String)JsonData.array(s.cases.get(i)).get(0));checksum=(checksum+result.length()*29L)%1000000007;if(!result.isEmpty())checksum=(checksum+result.charAt(i%result.length()))%1000000007;}}return checksum;}
}
