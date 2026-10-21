// Build the photo album's grid thumbnails and placeholder colours.
// Run from the repository root after _fetch.py has downloaded photos/:
//     ./lambda.exe run test/demo/photo-album/_thumbs.ls
// Writes thumbs/<file> (512 px JPEG) for every downloaded photo and records each
// photo's average colour as "#rrggbb" in photos.json. Reruns skip finished work.
// PHOTO_ALBUM_DIR selects another corpus directory, such as the test fixture.
import corpus: .mod_corpus

pn main() {
    let result = corpus.build(corpus.directory("test/demo/photo-album/"))^
    print("thumbnails made " ++ string(result.made) ++ ", colours set " ++ string(result.coloured) ++
        ", photos missing " ++ string(result.missing) ++ "\n")
}
