// Native indexed loops for text_search.js. Source notices: ../native_ports/LICENSE.md.
import java.util.*;

final class TextSearch extends PortRuntime {
    record State(int[] corpus, int[][] patterns) {}

    static State prepare() {
        StringJoiner corpus = new StringJoiner("\n");
        for (int i = 0; i < 512; i++)
            corpus.add("record-" + i + " alpha aaaaaaaaaaaaaaaaaaaaaaaa token-" + (i % 23) + " omega needle-" + (i % 11));
        String[] patterns = {"record-0 alpha", "record-2048 alpha", "token-22 omega", "needle-10", "omega needle-7",
            "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3", "missing-marker", "record-2047 omega"};
        return new State(corpus.toString().codePoints().toArray(),
            Arrays.stream(patterns).map(s -> s.codePoints().toArray()).toArray(int[][]::new));
    }

    static int naive(int[] text, int[] pattern, int start) {
        if (pattern.length == 0) return start;
        for (int position = start; position <= text.length - pattern.length; position++) {
            int offset = 0;
            while (offset < pattern.length && text[position + offset] == pattern[offset]) offset++;
            if (offset == pattern.length) return position;
        }
        return -1;
    }

    static int kmp(int[] text, int[] pattern, int start) {
        if (pattern.length == 0) return start;
        int[] table = new int[pattern.length];
        int length = 0, index = 1;
        while (index < pattern.length) {
            if (pattern[index] == pattern[length]) table[index++] = ++length;
            else if (length > 0) length = table[length - 1];
            else index++;
        }
        int textIndex = start, patternIndex = 0;
        while (textIndex < text.length) {
            if (text[textIndex] == pattern[patternIndex]) {
                textIndex++;
                if (++patternIndex == pattern.length) return textIndex - pattern.length;
            } else if (patternIndex > 0) patternIndex = table[patternIndex - 1];
            else textIndex++;
        }
        return -1;
    }

    static int boyerMoore(int[] text, int[] pattern, int start) {
        if (pattern.length == 0) return start;
        int[] last = new int[256];
        Arrays.fill(last, -1);
        for (int i = 0; i < pattern.length; i++) last[pattern[i]] = i;
        int position = start;
        while (position <= text.length - pattern.length) {
            int offset = pattern.length - 1;
            while (offset >= 0 && text[position + offset] == pattern[offset]) offset--;
            if (offset < 0) return position;
            position += Math.max(1, offset - last[text[position + offset]]);
        }
        return -1;
    }

    static long workload(State state) {
        long checksum = 0;
        for (int round = 0; round < 1536; round++) {
            for (int index = 0; index < state.patterns.length; index++) {
                int[] pattern = state.patterns[index];
                int start = (round * 17 + index * 13) % 97;
                int naive = naive(state.corpus, pattern, start);
                int kmp = kmp(state.corpus, pattern, start);
                int bm = boyerMoore(state.corpus, pattern, start);
                if (naive != kmp || kmp != bm) throw new AssertionError("search algorithms disagree");
                checksum = (checksum + (naive + 2L) * (index + 3) + (round + 1L) * 7) % 1000000007;
            }
        }
        return checksum;
    }

    static void run(Env e) {
        runBenchmark(e, new Object[]{
            new Fn(e, (env, args) -> prepare(), new String[]{}, 0, 0),
            new Fn(e, (env, args) -> workload((State) args[1]), new String[]{"Any", "Any"}, 2, 2),
            new Fn(e, (env, args) -> equal(args[0], 91395120L), new String[]{"Any"}, 1, 1),
            new Fn(e, (env, args) -> { System.out.println("text_search: CHECKSUM:" + args[0]); return null; }, new String[]{"Any"}, 1, 1)
        });
    }
}
