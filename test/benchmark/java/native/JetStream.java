import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HexFormat;

final class JetStream {
    static final class Entry {final int key;int value;Entry next;Entry(int k,int v,Entry n){key=k;value=v;next=n;}}
    static final class Table {
        Entry[] buckets=new Entry[1];int size,threshold;
        void put(int key,int value){int at=key&(buckets.length-1);
            for(Entry e=buckets[at];e!=null;e=e.next)if(e.key==key){e.value=value;return;}
            buckets[at]=new Entry(key,value,buckets[at]);
            if(++size>threshold){Entry[] grown=new Entry[buckets.length*2];
                for(Entry head:buckets)for(Entry e=head;e!=null;){Entry next=e.next;at=e.key&(grown.length-1);e.next=grown[at];grown[at]=e;e=next;}
                buckets=grown;threshold=grown.length*3/4;}
        }
        int get(int key){for(Entry e=buckets[key&(buckets.length-1)];e!=null;e=e.next)if(e.key==key)return e.value;throw new AssertionError("missing key");}
    }
    static boolean hashmap(){Table table=new Table();int count=90000;
        for(int i=0;i<count;i++)table.put(i,42);
        long total=0,keys=0,values=0;for(int r=0;r<5;r++)for(int i=0;i<count;i++)total+=table.get(i);
        for(Entry head:table.buckets)for(Entry e=head;e!=null;e=e.next){keys+=e.key;values+=e.value;}
        return table.size==count&&total==42L*count*5&&keys==(long)count*(count-1)/2&&values==42L*count;
    }
    static boolean nbody(){double energy=0;for(int count:new int[]{300,600,1200,2400}){
        nbody.NBodySystem system=new nbody.NBodySystem();energy+=system.energy();
        for(int i=0;i<count;i++)system.advance(.01);energy+=system.energy();}
        return energy==-1.3524862408537381;
    }
    static String plain(){try{return Files.readString(Path.of("test/benchmark/native_ports/fixtures/sha1.txt"));}catch(Exception e){throw new RuntimeException(e);}}
    static boolean sha1(String initial){String text=initial;for(int i=0;i<4;i++)text+=text;
        try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-1").digest(text.getBytes(StandardCharsets.US_ASCII)))
            .equals("2524d264def74cce2498bf112bedf00e6c0b796d");}catch(Exception e){throw new RuntimeException(e);}}
    static boolean once(String name,String fixture){return switch(name){
        case "hashmap"->hashmap();case "nbody"->nbody();case "richards"->new richards.JetRichards().run();
        case "deltablue"->deltablue.JetDeltaBlue.run();case "crypto_sha1"->sha1(fixture);
        case "cube3d"->Cube.run();case "raytrace3d"->RayTrace.run();
        default->throw new IllegalArgumentException("native JetStream port missing: "+name);};}
    static void run(String name,int repeats){
        if(name.equals("splay")){Splay.run(repeats);return;}
        if(name.equals("navier_stokes")){Fluid.run();return;}
        String fixture=name.equals("crypto_sha1")?plain():null;
        NativeBench.run(()->{for(int r=0;r<repeats;r++)NativeBench.check(once(name,fixture));return true;},
            NativeBench::check,v->{},NativeBench.warmup());
    }
}
