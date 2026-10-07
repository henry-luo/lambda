final class ThreeWayMerge {
    static String[][] prepare(){String[][] state=new String[3][768];for(int i=0;i<768;i++){String base="section "+i+" records the base document with stable words for merging and review";state[0][i]=base;
        for(int side=1;side<=2;side++){String line=base,name=side==1?"left":"right";if(i%17==0)line+=" "+name+" edit "+(i%31)+" keeps the paragraph useful";else if(side==1&&i%23==0)line+=" left-only annotation";else if(side==2&&i%29==0)line+=" right-only annotation";state[side][i]=line;}}return state;}
    static String word(String[] a,int i){return i<a.length?a[i]:"";}
    static String merge(String base,String left,String right){if(left.equals(right))return left;if(left.equals(base))return right;if(right.equals(base))return left;
        String[] b=base.split(" ",-1),l=left.split(" ",-1),r=right.split(" ",-1);java.util.ArrayList<String> out=new java.util.ArrayList<>();
        for(int i=0;i<Math.max(b.length,Math.max(l.length,r.length));i++){String bv=word(b,i),lv=word(l,i),rv=word(r,i);if(lv.equals(rv))out.add(lv);else if(lv.equals(bv))out.add(rv);else if(rv.equals(bv))out.add(lv);else{out.add("<<<<<<< LEFT");out.add(lv);out.add("=======");out.add(rv);out.add(">>>>>>> RIGHT");}}return String.join(" ",out);}
    static String lines(String[][] state){String[] out=new String[768];for(int i=0;i<out.length;i++)out[i]=merge(state[0][i],state[1][i],state[2][i]);return String.join("\n",out);}
    static long work(String[][] state){long checksum=0;for(int r=0;r<11000;r++){String s=lines(state);checksum=(checksum+s.length()*31L+s.charAt(r*37%s.length()))%1000000007;}return checksum;}
}
