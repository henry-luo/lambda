# ZIP and package fixtures

`generate.py` regenerates the ZIP32/ZIP64 and Open XML fixtures with Python's
standard library. `sample.jar` is built separately with the JDK's `javac` and
`jar` tools. Lambda tests consume the checked-in archive without requiring Java.

The executable JAR contains a Java 8 class compiled from
[`jar-src/example/Hello.java`](jar-src/example/Hello.java), a `Main-Class`
manifest, JSON/text resources, an empty file, a Unicode filename and a binary
resource with embedded NUL bytes. The JDK emits its standard JAR extra field
and DEFLATE data descriptors. Timestamps and manifest attributes are fixed;
regeneration is repeatable with the same JDK. Scratch files stay under `temp/`.

Regenerate with a JDK 17 or newer (`--java-home` is optional when `JAVA_HOME`
or the tools on `PATH` select the JDK):

```sh
python3 test/input/zip/generate_jar.py --java-home /path/to/jdk
```

The native ZIP suite checks that indexing and class metadata leave all payloads
unexpanded, individual reads decode only the selected member, and repeated reads
use the cache (**S12.4.1v2/S14.3.1v2**). `test/lambda/zip_jar.ls` covers automatic
and explicit ZIP input, filesystem navigation, manifest text, class binary
content, structured resources, Unicode, empty files and raw archive reads.
`test/lambda/proc/zip_jar_output.ls` checks member-byte preservation through ZIP
output; `.jar` output uses an explicit `format: 'zip'`.

Check independent JVM interoperability from the repository root:

```sh
java -jar test/input/zip/sample.jar
./lambda.exe run test/lambda/proc/zip_jar_output.ls --no-log
java -jar temp/zip-copy.jar
```

Both Java commands print `Hello from JAR!`. The program also checks the binary
resource byte by byte, so execution verifies class loading, manifest dispatch
and resource integrity in both archives.
