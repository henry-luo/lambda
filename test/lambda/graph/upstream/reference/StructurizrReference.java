import com.structurizr.dsl.StructurizrDslParser;
import com.structurizr.export.dot.DOTExporter;
import java.nio.file.Files;
import java.nio.file.Path;

class StructurizrReference {
    public static void main(String[] args) throws Exception {
        Path output = Path.of(args[0]);
        var exporter = new DOTExporter();
        for (int i = 1; i < args.length; i++) {
            var source = Path.of(args[i]);
            var parser = new StructurizrDslParser();
            parser.parse(source.toFile());
            // Use the official parser and exporter without CLI remote theme retrieval.
            for (var diagram : exporter.export(parser.getWorkspace())) {
                String stem = source.getFileName().toString().replace(".dsl", "");
                Files.writeString(output.resolve(stem + "--" + diagram.getKey() + ".gv"),
                    diagram.getDefinition());
            }
        }
    }
}
