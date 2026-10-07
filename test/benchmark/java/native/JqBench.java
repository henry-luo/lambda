import java.nio.file.Files;
import java.nio.file.Path;

final class JqBench {
    record State(JqCompiler.Program program,Object input){}
    static State prepare(String name){try{Path base=Path.of("test/benchmark/text/jq");JqCompiler.Program program=JqCompiler.program(Files.readString(base.resolve(name.substring(3)+".jq")));
        Object input=switch(name){case "jq_records"->JsonData.parse(Files.readString(base.resolve("orders.json")));case "jq_bf"->Files.readString(base.resolve("fib.bf"));default->null;};return new State(program,input);
    }catch(Exception e){throw new RuntimeException(e);}}
    static void run(String name){long expected=switch(name){case "jq_mix"->98172625;case "jq_records"->878885883;case "jq_bf"->478890292;case "jq_tree"->313746104;default->throw new IllegalArgumentException(name);};
        NativeBench.prepared(()->prepare(name),s->JqVM.run(s.program,s.input),v->NativeBench.check(v.size()==1&&JqValues.num(v.get(0))==expected),v->System.out.println(name+": CHECKSUM:"+JqValues.number(v.get(0))));}
}
