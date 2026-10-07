final class TextBench {
    static <S> void checksum(String prefix,java.util.function.Supplier<S> prepare,java.util.function.Function<S,Long> work,long expected){
        NativeBench.prepared(prepare,work,v->NativeBench.check(v==expected),v->System.out.println(prefix+v));
    }
    static void run(String name){switch(name){
        case "text_search"->TextSearch.run();
        case "jq_mix","jq_records","jq_bf","jq_tree"->JqBench.run(name);
        case "prettier_ast"->Pretty.run();
        case "fast_diff"->checksum("CHECKSUM:",FastDiff::prepare,FastDiff::work,390912);
        case "hyphen"->checksum("CHECKSUM:",Hyphen::prepare,Hyphen::work,1183296);
        case "microdiff"->checksum("CHECKSUM:",MicroDiff::prepare,MicroDiff::work,3278848);
        case "log_pipeline"->checksum("log_pipeline: CHECKSUM:",LogPipeline::prepare,LogPipeline::work,292634526);
        case "three_way_merge"->checksum("three_way_merge: CHECKSUM:",ThreeWayMerge::prepare,ThreeWayMerge::work,342313356);
        default->throw new IllegalArgumentException("native text port missing: "+name);
    }}
}
