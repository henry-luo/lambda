// Writes each standard-material case description as JSON for the oracle page:
// ./lambda.exe run test/scene3d/oracle/_dump.ls   (into PBR_ORACLE_DIR, default temp/pbr_oracle)
import P: ~~.mod_pbr

pn main() any^ {
    let dir = sys.proc.self.env.PBR_ORACLE_DIR# or "temp/pbr_oracle"
    if (not exists(dir)) io.mkdir(dir)^
    for (key, c in P.CASES) {
        output(P.description(string(key)), dir ++ "/" ++ string(key) ++ ".json", {format: 'json'})^
        print(string(key) ++ "\n")
    }
}
