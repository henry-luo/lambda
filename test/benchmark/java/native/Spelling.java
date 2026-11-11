import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class Spelling {
    static final Pattern TOKEN = Pattern.compile("[A-Za-z]+(?:'[A-Za-z]+)*");
    record State(Set<String> words, String text, List<Object> expected) {}
    record Result(int checked, List<Object> issues, List<Object> expected) {}

    static State prepare() {
        try {
            Set<String> words = new HashSet<>();
            words.add("Lambda"); words.add("Radiant");
            for (String line : Files.readAllLines(Path.of("lmd/package/doc/spelling/data/en-US.dict"))) {
                for (String group : line.split("\\|")) {
                    String previous = "";
                    boolean first = true;
                    for (String token : group.split(";")) {
                        if (first) { previous = token; first = false; }
                        else {
                            int comma = token.indexOf(',');
                            if (comma < 0) previous += token;
                            else previous = previous.substring(0, previous.offsetByCodePoints(0, Integer.parseInt(token.substring(0, comma)))) + token.substring(comma + 1);
                        }
                        words.add(previous);
                    }
                }
            }
            return new State(words, Files.readString(Path.of("test/benchmark/text/spelling_document.txt")),
                             JsonData.array(JsonData.load("spelling_expected.json")));
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    static Result work(State state) {
        Matcher matcher = TOKEN.matcher(state.text());
        List<Object> issues = new ArrayList<>();
        int checked = 0;
        while (matcher.find()) {
            checked++;
            String word = matcher.group(), lower = word.toLowerCase(Locale.ROOT);
            boolean folded = word.equals(word.toUpperCase(Locale.ROOT)) ||
                word.equals(word.substring(0, 1).toUpperCase(Locale.ROOT) + lower.substring(1));
            if (!state.words().contains(word) && !(folded && state.words().contains(lower)))
                issues.add(JsonData.obj("word", word, "start", (long)matcher.start(), "end", (long)matcher.end()));
        }
        return new Result(checked, issues, state.expected());
    }

    static void run() {
        NativeBench.prepared(Spelling::prepare, Spelling::work,
            result -> NativeBench.check(result.checked() == 7110 && result.issues().equals(result.expected())),
            result -> System.out.println("spelling: CHECKED:7110 ISSUES:720"));
    }
}
