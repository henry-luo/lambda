package example;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;

public final class Hello {
    public static void main(String[] args) throws Exception {
        // verify resource bytes as well as class loading when running the JAR.
        byte[] expected = {0, (byte) 0xff, 'P', 'K', 0};
        try (InputStream bytes = Hello.class.getResourceAsStream("/resources/opaque.bin")) {
            if (bytes == null) throw new IllegalStateException("missing binary resource");
            for (byte value : expected) {
                if (bytes.read() != (value & 0xff)) {
                    throw new IllegalStateException("binary resource differs");
                }
            }
            if (bytes.read() != -1) throw new IllegalStateException("binary resource has trailing bytes");
        }
        try (BufferedReader text = new BufferedReader(new InputStreamReader(
                Hello.class.getResourceAsStream("/resources/message.txt"), "UTF-8"))) {
            System.out.println(text.readLine());
        }
    }
}
