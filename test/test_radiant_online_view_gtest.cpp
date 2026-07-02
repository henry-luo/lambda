#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>

#ifdef _WIN32
TEST(RadiantOnlineViewTest, SkippedOnWindows) {
    GTEST_SKIP() << "online view smoke tests use fork/select/kill on Unix";
}
#else

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LAMBDA_EXE "./lambda.exe"
#define ONLINE_VIEW_DEFAULT_TIMEOUT_SECONDS 90
#define ONLINE_VIEW_OUTPUT_LIMIT (1024 * 1024)

struct RadiantOnlineViewCase {
    const char* label;
    const char* url;
    bool expect_linked_resources;
};

struct RadiantOnlineViewBuffer {
    char* data;
    size_t len;
    size_t cap;
    bool truncated;
};

struct RadiantOnlineViewResult {
    int exit_code;
    bool timed_out;
    bool exec_failed;
    bool output_alloc_failed;
    uint64_t memtrack_live_bytes;
    uint64_t memtrack_live_count;
    char output_path[256];
    char lambda_log_path[256];
    RadiantOnlineViewBuffer output;
};

// Radiant online view tests are real-world browser-compatibility smoke tests.
// The long-term goal is that `lambda view URL` can stably load arbitrary online
// pages like a browser: download the top-level document and linked resources,
// parse HTML/CSS/JS, execute supported JS, gracefully ignore or degrade
// unsupported JS/DOM/CSS features, perform layout, load images and fonts,
// render, and shut down without crashes, hangs, swallowed runtime errors, or
// memtrack leaks. URLs in this table should accumulate over time; a page that
// exposes a crash, leak, timeout, resource error, parser error, script error,
// layout failure, render failure, or shutdown issue should drive a Radiant or
// runtime fix rather than being removed from coverage.
static const RadiantOnlineViewCase g_online_view_cases[] = {
    {"example", "https://example.com/", false},
    {"wikipedia", "https://www.wikipedia.org/", true},
    {"google", "https://www.google.com/", true},
    {"reactos", "https://reactos.org/", true},
    {"firefox", "https://www.firefox.com/en-US/", true},
    {"wikipedia_main_page", "https://en.wikipedia.org/wiki/Main_Page", true},
    {"apache_foundation", "https://www.apache.org/foundation/", true},
    {"perl", "https://www.perl.org/", true},
    {"ruby_lang", "https://www.ruby-lang.org/en/", true},
    {"curl", "https://curl.se/", true},
    {"openssl", "https://www.openssl.org/", true},
    {"nginx", "https://nginx.org/", true},
    {"apache", "https://www.apache.org/", true},
    {"libpng", "https://www.libpng.org/pub/png/", false},
    {"lua", "https://www.lua.org/", true},
    {"zlib", "https://www.zlib.net/", false},
    {"example_org", "https://example.org/", false},
    {"iana_reserved", "https://www.iana.org/domains/reserved", false},
    {"netlib", "https://www.netlib.org/", false},
    {"sqlite", "https://www.sqlite.org/index.html", true},
    {"scheme", "https://www.scheme.org/", false},
    {"mercurial_scm", "https://www.mercurial-scm.org/", true},
    {"subversion", "https://subversion.apache.org/", true},
    {"busybox", "https://www.busybox.net/", false},
    {"musl_libc", "https://www.musl-libc.org/", false},
    {"linux_from_scratch", "https://www.linuxfromscratch.org/", true},
    {"example_net", "https://example.net/", false},
    {"curl_manpage", "https://curl.se/docs/manpage.html", true},
    {"curl_sslcerts", "https://curl.se/docs/sslcerts.html", true},
    {"nginx_core_module", "https://nginx.org/en/docs/http/ngx_http_core_module.html", true},
    {"curl_faq", "https://curl.se/docs/faq.html", true},
    {"sqlite_datatypes", "https://www.sqlite.org/datatype3.html", true},
    {"tomcat_docs", "https://tomcat.apache.org/tomcat-10.1-doc/", true},
    {"ant_manual", "https://ant.apache.org/manual/", true},
    {"curl_docs", "https://curl.se/docs/", true},
    {"curl_libcurl", "https://curl.se/libcurl/", true},
    {"nginx_docs", "https://nginx.org/en/docs/", true},
    {"apache_httpd_docs", "https://httpd.apache.org/docs/", true},
    {"apache_apr", "https://apr.apache.org/", true},
    {"apache_tomcat", "https://tomcat.apache.org/", true},
    {"apache_ant", "https://ant.apache.org/", true},
    {"apache_maven", "https://maven.apache.org/", true},
    {"openssl_docs", "https://www.openssl.org/docs/", true},
    {"maven_guides", "https://maven.apache.org/guides/", true},
    {"lua_pil", "https://www.lua.org/pil/contents.html", false},
    {"sqlite_docs", "https://www.sqlite.org/docs.html", true},
    {"sqlite_lang", "https://www.sqlite.org/lang.html", true},
    {"sqlite_cli", "https://www.sqlite.org/cli.html", true},
    {"zlib_manual", "https://www.zlib.net/manual.html", false},
    {"libpng_intro", "https://www.libpng.org/pub/png/pngintro.html", false},
    {"sqlite_pragma", "https://www.sqlite.org/pragma.html", true},
    {"busybox_about", "https://www.busybox.net/about.html", false},
    {"netlib_lapack", "https://www.netlib.org/lapack/", false},
    {"netlib_blas", "https://www.netlib.org/blas/", false},
    {"iana_about", "https://www.iana.org/about", true},
    {"iana_numbers", "https://www.iana.org/numbers", false},
    {"openstd_c", "https://www.open-std.org/jtc1/sc22/wg14/", false},
    {"openstd_cpp", "https://www.open-std.org/jtc1/sc22/wg21/", false},
    {"tcl_lang", "https://www.tcl-lang.org/", true},
    {"cpan", "https://www.cpan.org/", true},
    {"pcre", "https://www.pcre.org/", false},
    {"sqlite_capi_intro", "https://www.sqlite.org/c3ref/intro.html", true},
    {"sqlite_result_codes", "https://www.sqlite.org/rescode.html", true},
    {"cairographics", "https://www.cairographics.org/", true},
    {"httpd_home", "https://httpd.apache.org/", true},
    {"x_org", "https://www.x.org/wiki/", false},
    {"gnu_home", "https://www.gnu.org/", true},
    {"python", "https://www.python.org/", true},
    {"kernel", "https://www.kernel.org/", true},
    {"w3c", "https://www.w3.org/", true},
    {"iana_home", "https://www.iana.org/", true},
    {"debian", "https://www.debian.org/", true},
    {"freebsd", "https://www.freebsd.org/", true},
    {"postgresql", "https://www.postgresql.org/", true},
    {"rust_lang", "https://www.rust-lang.org/", true},
    {"php", "https://www.php.net/", true},
    {"openbsd", "https://www.openbsd.org/", true},
    {"vim", "https://www.vim.org/", true},
    {"r_project", "https://www.r-project.org/", true},
    {"cmake", "https://cmake.org/", true},
    {"llvm", "https://llvm.org/", true},
    {"rfc_editor", "https://www.rfc-editor.org/", true},
    {"unicode", "https://www.unicode.org/", true},
    {"git_scm", "https://git-scm.com/", true},
    {"valgrind", "https://www.valgrind.org/", true},
    {"gnuplot", "http://www.gnuplot.info/", true},
    {"gnu_bash", "https://www.gnu.org/software/bash/", true},
    {"gnu_make", "https://www.gnu.org/software/make/", true},
    {"gnu_coreutils", "https://www.gnu.org/software/coreutils/", true},
    {"gnu_grep", "https://www.gnu.org/software/grep/", true},
    {"gnu_sed", "https://www.gnu.org/software/sed/", true},
    {"gnu_gawk", "https://www.gnu.org/software/gawk/", true},
    {"gnu_gdb", "https://www.gnu.org/software/gdb/", true},
    {"lua_manual", "https://www.lua.org/manual/5.4/", true},
    {"musl_manual", "https://www.musl-libc.org/manual.html", false},
    {"tcpdump", "https://www.tcpdump.org/", false},
    {"libressl", "https://www.libressl.org/", false},
    {"freedesktop", "https://www.freedesktop.org/wiki/", false},
    {"httpd_24_docs", "https://httpd.apache.org/docs/2.4/", true},
    {"apr_generated_docs", "https://apr.apache.org/docs/apr/1.7/", true},
    {"iana_protocols", "https://www.iana.org/protocols", true},
    {"rfc_ascii_20", "https://www.rfc-editor.org/rfc/rfc20.html", false},
    {"rfc_ipv4_791", "https://www.rfc-editor.org/rfc/rfc791.html", false},
    {"rfc_tcp_793", "https://www.rfc-editor.org/rfc/rfc793.html", false},
    {"rfc_uri_3986", "https://www.rfc-editor.org/rfc/rfc3986.html", false},
    {"rfc_tls13_8446", "https://www.rfc-editor.org/rfc/rfc8446.html", false},
    {"w3c_html52", "https://www.w3.org/TR/html52/", true},
    {"w3c_css_cascade", "https://www.w3.org/TR/css-cascade-5/", true},
    {"w3c_css_flexbox", "https://www.w3.org/TR/css-flexbox-1/", true},
    {"w3c_wcag22", "https://www.w3.org/TR/WCAG22/", true},
    {"w3c_xml", "https://www.w3.org/TR/xml/", true},
    {"python_docs", "https://docs.python.org/3/", true},
    {"python_tutorial", "https://docs.python.org/3/tutorial/", true},
    {"python_library", "https://docs.python.org/3/library/index.html", true},
    {"python_whatsnew", "https://docs.python.org/3/whatsnew/3.13.html", true},
    {"python_capi", "https://docs.python.org/3/c-api/index.html", true},
    {"postgres_docs", "https://www.postgresql.org/docs/current/", true},
    {"postgres_sql_select", "https://www.postgresql.org/docs/current/sql-select.html", true},
    {"postgres_datatypes", "https://www.postgresql.org/docs/current/datatype.html", true},
    {"sqlite_quickstart", "https://www.sqlite.org/quickstart.html", true},
    {"sqlite_json1", "https://www.sqlite.org/json1.html", true},
    {"sqlite_select", "https://www.sqlite.org/lang_select.html", true},
    {"curl_libcurl_tutorial", "https://curl.se/libcurl/c/libcurl-tutorial.html", true},
    {"curl_easy_setopt", "https://curl.se/libcurl/c/curl_easy_setopt.html", true},
    {"curl_easy_perform", "https://curl.se/libcurl/c/curl_easy_perform.html", true},
    {"nginx_beginners", "https://nginx.org/en/docs/beginners_guide.html", true},
    {"nginx_https", "https://nginx.org/en/docs/http/configuring_https_servers.html", true},
    {"httpd_getting_started", "https://httpd.apache.org/docs/2.4/getting-started.html", true},
    {"httpd_core", "https://httpd.apache.org/docs/2.4/mod/core.html", true},
    {"cmake_help", "https://cmake.org/cmake/help/latest/", true},
    {"cmake_add_executable", "https://cmake.org/cmake/help/latest/command/add_executable.html", true},
    {"llvm_getting_started", "https://llvm.org/docs/GettingStarted.html", true},
    {"llvm_langref", "https://llvm.org/docs/LangRef.html", true},
    {"gcc_manual", "https://gcc.gnu.org/onlinedocs/gcc/", true},
    {"gcc_option_summary", "https://gcc.gnu.org/onlinedocs/gcc/Option-Summary.html", true},
    {"gnu_emacs", "https://www.gnu.org/software/emacs/", true},
    {"gnu_gcc", "https://www.gnu.org/software/gcc/", true},
    {"gnu_libc", "https://www.gnu.org/software/libc/", true},
    {"gnu_guile", "https://www.gnu.org/software/guile/", true},
    {"git_docs_git", "https://git-scm.com/docs/git", true},
    {"git_book_v2", "https://git-scm.com/book/en/v2", true},
    {"kernel_docs", "https://www.kernel.org/doc/html/latest/", true},
    {"kernel_process_docs", "https://www.kernel.org/doc/html/latest/process/", true},
    {"man7_ls", "https://man7.org/linux/man-pages/man1/ls.1.html", true},
    {"man7_open", "https://man7.org/linux/man-pages/man2/open.2.html", true},
    {"openbsd_man_ls", "https://man.openbsd.org/ls", true},
    {"rust_book", "https://doc.rust-lang.org/book/", true},
    {"rust_std", "https://doc.rust-lang.org/std/", true},
    {"go_doc", "https://go.dev/doc/", true},
    {"go_spec", "https://go.dev/ref/spec", true},
    {"php_manual", "https://www.php.net/manual/en/", true},
    {"mdn_html", "https://developer.mozilla.org/en-US/docs/Web/HTML", true},
    {"mdn_css", "https://developer.mozilla.org/en-US/docs/Web/CSS", true},
    {"mdn_js_reference", "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference", true},
    {"whatwg_html", "https://html.spec.whatwg.org/multipage/", true},
    {"whatwg_dom", "https://dom.spec.whatwg.org/", true},
    {"whatwg_fetch", "https://fetch.spec.whatwg.org/", true},
    {"tc39_ecma262", "https://tc39.es/ecma262/", true},
    {"tc39_ecma402", "https://tc39.es/ecma402/", true},
    {"nodejs_api", "https://nodejs.org/api/", true},
    {"nodejs_fs", "https://nodejs.org/api/fs.html", true},
    {"typescript_docs", "https://www.typescriptlang.org/docs/", true},
    {"typescript_handbook", "https://www.typescriptlang.org/docs/handbook/intro.html", true},
    {"go_effective", "https://go.dev/doc/effective_go", true},
    {"go_pkg_std", "https://pkg.go.dev/std", true},
    {"rust_cargo_book", "https://doc.rust-lang.org/cargo/", true},
    {"rust_reference", "https://doc.rust-lang.org/reference/", true},
    {"boost_docs", "https://www.boost.org/doc/libs/", true},
    {"eigen_docs", "https://eigen.tuxfamily.org/dox/", true},
    {"qt_docs", "https://doc.qt.io/qt-6/", true},
    {"gtk_docs", "https://docs.gtk.org/gtk4/", true},
    {"webkit", "https://webkit.org/", true},
    {"mesa3d", "https://www.mesa3d.org/", true},
    {"ninja_manual", "https://ninja-build.org/manual.html", false},
    {"meson_docs", "https://mesonbuild.com/", true},
    {"bazel_docs", "https://bazel.build/docs", true},
    {"clang_docs", "https://clang.llvm.org/docs/", true},
    {"lld_docs", "https://lld.llvm.org/", true},
    {"gdb_docs", "https://sourceware.org/gdb/documentation/", true},
    {"binutils_docs", "https://sourceware.org/binutils/docs/", true},
    {"gitlab_docs", "https://docs.gitlab.com/", true},
    {"github_docs", "https://docs.github.com/en", true},
    {"docker_docs", "https://docs.docker.com/", true},
    {"kubernetes_docs", "https://kubernetes.io/docs/home/", true},
    {"prometheus_docs", "https://prometheus.io/docs/introduction/overview/", true},
    {"grafana_docs", "https://grafana.com/docs/", true},
    {"terraform_docs", "https://developer.hashicorp.com/terraform/docs", true},
    {"ansible_docs", "https://docs.ansible.com/ansible/latest/index.html", true},
    {"cmake_buildsystem", "https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html", true},
    {"python_pep8", "https://peps.python.org/pep-0008/", true},
    {"python_pep3333", "https://peps.python.org/pep-3333/", true},
    {"java_jls", "https://docs.oracle.com/javase/specs/jls/se21/html/index.html", true},
    {"java_api", "https://docs.oracle.com/en/java/javase/21/docs/api/index.html", true},
    {"kotlin_docs", "https://kotlinlang.org/docs/home.html", true},
    {"scala_docs", "https://docs.scala-lang.org/", true},
    {"clojure", "https://clojure.org/", true},
    {"erlang_docs", "https://www.erlang.org/doc/", true},
    {"elixir_docs", "https://hexdocs.pm/elixir/", true},
    {"ocaml_docs", "https://ocaml.org/docs", true},
    {"haskell_docs", "https://www.haskell.org/documentation/", true},
    {"racket_docs", "https://docs.racket-lang.org/", true},
    {"w3c_css_snapshot", "https://www.w3.org/TR/CSS/", true},
    {"w3c_webgpu", "https://www.w3.org/TR/webgpu/", true},
    {"w3c_websockets", "https://www.w3.org/TR/websockets/", true},
    {"w3c_wai_aria", "https://www.w3.org/TR/wai-aria-1.2/", true},
    {"chromium_docs", "https://www.chromium.org/developers/", true},
    {"v8_docs", "https://v8.dev/docs", true},
    {"webkit_blog", "https://webkit.org/blog/", true},
    {"servo_docs", "https://servo.org/", true},
    {"electron_docs", "https://www.electronjs.org/docs/latest/", true},
    {"deno_docs", "https://docs.deno.com/", true},
    {"bun_docs", "https://bun.sh/docs", true},
    {"npm_docs", "https://docs.npmjs.com/", true},
    {"yarn_docs", "https://yarnpkg.com/getting-started", true},
    {"pnpm_docs", "https://pnpm.io/", true},
    {"vite_docs", "https://vite.dev/guide/", true},
    {"webpack_docs", "https://webpack.js.org/concepts/", true},
    {"rollup_docs", "https://rollupjs.org/introduction/", true},
    {"eslint_docs", "https://eslint.org/docs/latest/", true},
    {"prettier_docs", "https://prettier.io/docs/", true},
    {"react_docs", "https://react.dev/learn", true},
    {"vue_docs", "https://vuejs.org/guide/introduction.html", true},
    {"svelte_docs", "https://svelte.dev/docs", true},
    {"angular_docs", "https://angular.dev/overview", true},
    {"solid_docs", "https://docs.solidjs.com/", true},
    {"nextjs_docs", "https://nextjs.org/docs", true},
    {"nuxt_docs", "https://nuxt.com/docs/getting-started/introduction", true},
    {"astro_docs", "https://docs.astro.build/en/getting-started/", true},
    {"remix_docs", "https://remix.run/docs/en/main", true},
    {"tailwind_docs", "https://tailwindcss.com/docs", true},
    {"bootstrap_docs", "https://getbootstrap.com/docs/5.3/getting-started/introduction/", true},
    {"bulma_docs", "https://bulma.io/documentation/", true},
    {"material_ui_docs", "https://mui.com/material-ui/getting-started/", true},
    {"ant_design_docs", "https://ant.design/docs/react/introduce", true},
    {"chakra_docs", "https://chakra-ui.com/docs/get-started/installation", true},
    {"cloudflare_docs", "https://developers.cloudflare.com/", true},
    {"aws_docs", "https://docs.aws.amazon.com/", true},
    {"azure_docs", "https://learn.microsoft.com/en-us/azure/", true},
    {"gcp_docs", "https://cloud.google.com/docs", true},
    {"digitalocean_docs", "https://docs.digitalocean.com/", true},
    {"supabase_docs", "https://supabase.com/docs", true},
    {"firebase_docs", "https://firebase.google.com/docs", true},
    {"vercel_docs", "https://vercel.com/docs", true},
    {"netlify_docs", "https://docs.netlify.com/", true},
    {"nginx_unit_docs", "https://unit.nginx.org/", true},
    {"redis_docs", "https://redis.io/docs/latest/", true},
    {"mongodb_docs", "https://www.mongodb.com/docs/", true},
    {"mysql_docs", "https://dev.mysql.com/doc/", true},
    {"mariadb_docs", "https://mariadb.com/kb/en/documentation/", true},
    {"duckdb_docs", "https://duckdb.org/docs/", true},
    {"grafana_loki_docs", "https://grafana.com/docs/loki/latest/", true},
    {"cern_www_project", "http://info.cern.ch/hypertext/WWW/TheProject.html", false},
    {"w3c_www_history", "https://www.w3.org/History/19921103-hypertext/hypertext/WWW/TheProject.html", false},
    {"web_dev", "https://web.dev/", true},
    {"chrome_developers", "https://developer.chrome.com/docs/", true},
    {"webassembly", "https://webassembly.org/", true},
    {"emscripten_docs", "https://emscripten.org/docs/", true},
    {"wasm_core_spec", "https://webassembly.github.io/spec/core/", true},
    {"wasi_docs", "https://wasi.dev/", true},
    {"wasmtime_docs", "https://docs.wasmtime.dev/", true},
    {"mlir_docs", "https://mlir.llvm.org/docs/", true},
    {"zig_docs", "https://ziglang.org/documentation/master/", true},
    {"nim_docs", "https://nim-lang.org/documentation.html", true},
    {"crystal_docs", "https://crystal-lang.org/reference/latest/", true},
    {"dart_docs", "https://dart.dev/guides", true},
    {"flutter_docs", "https://docs.flutter.dev/", true},
    {"ruby_docs", "https://docs.ruby-lang.org/en/master/", true},
    {"rails_guides", "https://guides.rubyonrails.org/", true},
    {"django_docs", "https://docs.djangoproject.com/en/stable/", true},
    {"flask_docs", "https://flask.palletsprojects.com/en/stable/", true},
    {"fastapi_docs", "https://fastapi.tiangolo.com/", true},
    {"sqlalchemy_docs", "https://docs.sqlalchemy.org/en/20/", true},
    {"pandas_docs", "https://pandas.pydata.org/docs/", true},
    {"numpy_docs", "https://numpy.org/doc/stable/", true},
    {"scipy_docs", "https://docs.scipy.org/doc/scipy/", true},
    {"matplotlib_docs", "https://matplotlib.org/stable/", true},
    {"jupyter_docs", "https://docs.jupyter.org/en/latest/", true},
    {"pytorch_docs", "https://pytorch.org/docs/stable/index.html", true},
    {"tensorflow_guide", "https://www.tensorflow.org/guide", true},
    {"sklearn_docs", "https://scikit-learn.org/stable/", true},
    {"huggingface_docs", "https://huggingface.co/docs", true},
    {"opencv_docs", "https://docs.opencv.org/4.x/", true},
    {"ffmpeg_docs", "https://ffmpeg.org/documentation.html", true},
    {"imagemagick_docs", "https://imagemagick.org/script/command-line-processing.php", true},
    {"blender_manual", "https://docs.blender.org/manual/en/latest/", true},
    {"godot_docs", "https://docs.godotengine.org/en/stable/", true},
    {"unity_manual", "https://docs.unity3d.com/Manual/index.html", true},
    {"unreal_docs", "https://dev.epicgames.com/documentation/en-us/unreal-engine", true},
    {"pygame_docs", "https://www.pygame.org/docs/", true},
    {"mdn_canvas", "https://developer.mozilla.org/en-US/docs/Web/API/Canvas_API", true},
    {"caniuse", "https://caniuse.com/", true},
    {"css_tricks_flexbox", "https://css-tricks.com/snippets/css/a-guide-to-flexbox/", true},
    {"ietf_datatracker", "https://datatracker.ietf.org/", true},
    {"cve_mitre", "https://cve.mitre.org/", true},
    {"nvd_nist", "https://nvd.nist.gov/", true},
    {"owasp_top_ten", "https://owasp.org/www-project-top-ten/", true},
    {"letsencrypt_docs", "https://letsencrypt.org/docs/", true},
    {"certbot_docs", "https://eff-certbot.readthedocs.io/en/stable/", true},
    {"tor_support", "https://support.torproject.org/", true},
    {"wireshark_docs", "https://www.wireshark.org/docs/", true},
    {"nmap_book", "https://nmap.org/book/man.html", true},
    {"rsync_samba", "https://rsync.samba.org/", true},
    {"samba_docs", "https://www.samba.org/samba/docs/", true},
    {"systemd_docs", "https://systemd.io/", true},
    {"archwiki_main", "https://wiki.archlinux.org/title/Main_page", true},
    {"gentoo_wiki_main", "https://wiki.gentoo.org/wiki/Main_Page", true},
    {"ubuntu_docs", "https://documentation.ubuntu.com/", true},
    {"redhat_docs", "https://docs.redhat.com/", true},
    {"fedora_docs", "https://docs.fedoraproject.org/en-US/docs/", true},
    {"alpine_wiki_main", "https://wiki.alpinelinux.org/wiki/Main_Page", true},
    {"nixos_manual", "https://nixos.org/manual/nixos/stable/", true},
    {"homebrew_docs", "https://docs.brew.sh/", true},
    {"macports_guide", "https://guide.macports.org/", true},
    {"freebsd_handbook", "https://docs.freebsd.org/en/books/handbook/", true},
    {"openbsd_faq", "https://www.openbsd.org/faq/", true},
    {"netbsd_docs", "https://www.netbsd.org/docs/", true},
    {"qemu_docs", "https://www.qemu.org/docs/master/", true},
    {"virtualbox_manual", "https://www.virtualbox.org/manual/", true},
    {"podman_docs", "https://docs.podman.io/en/latest/", true},
    {"helm_docs", "https://helm.sh/docs/", true},
    {"envoy_docs", "https://www.envoyproxy.io/docs/envoy/latest/", true},
};

static bool online_view_file_readable(const char* path) {
    return access(path, R_OK) == 0;
}

static bool online_view_file_executable(const char* path) {
    return access(path, X_OK) == 0;
}

static void online_view_ensure_temp_dir() {
    mkdir("./temp", 0755);
}

static bool online_view_write_noop_events(const char* path) {
    static const char content[] =
        "{\n"
        "  \"name\": \"radiant online URL smoke\",\n"
        "  \"viewport\": {\"width\": 1200, \"height\": 800},\n"
        "  \"events\": [\n"
        "    {\"type\": \"wait\", \"ms\": 200}\n"
        "  ]\n"
        "}\n";
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    size_t written = fwrite(content, 1, sizeof(content) - 1, file);
    fclose(file);
    return written == sizeof(content) - 1;
}

static bool online_view_buffer_append(RadiantOnlineViewBuffer* buffer,
                                      const char* data, size_t len) {
    if (!buffer || !data || len == 0) return true;
    if (buffer->len >= ONLINE_VIEW_OUTPUT_LIMIT) {
        buffer->truncated = true;
        return true;
    }
    size_t allowed = ONLINE_VIEW_OUTPUT_LIMIT - buffer->len;
    if (len > allowed) {
        len = allowed;
        buffer->truncated = true;
    }
    if (buffer->len + len + 1 > buffer->cap) {
        size_t new_cap = buffer->cap ? buffer->cap * 2 : 4096;
        while (new_cap < buffer->len + len + 1) new_cap *= 2;
        char* next = (char*)realloc(buffer->data, new_cap);
        if (!next) return false;
        buffer->data = next;
        buffer->cap = new_cap;
    }
    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';
    return true;
}

static void online_view_buffer_free(RadiantOnlineViewBuffer* buffer) {
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static void online_view_save_buffer(const char* path, const RadiantOnlineViewBuffer* buffer) {
    if (!path || !buffer) return;
    FILE* file = fopen(path, "wb");
    if (!file) return;
    if (buffer->data && buffer->len > 0) {
        fwrite(buffer->data, 1, buffer->len, file);
    }
    fclose(file);
}

static int online_view_timeout_seconds() {
    const char* env = getenv("LAMBDA_RADIANT_ONLINE_VIEW_TIMEOUT");
    if (env && env[0]) {
        int parsed = atoi(env);
        if (parsed > 0) return parsed;
    }
    return ONLINE_VIEW_DEFAULT_TIMEOUT_SECONDS;
}

static int online_view_exit_code(int status) {
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
}

static bool online_view_file_contains(const char* path, const char* needle) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    bool found = false;
    char chunk[4096];
    size_t needle_len = strlen(needle);
    char carry[128];
    size_t carry_len = 0;
    carry[0] = '\0';

    while (!found) {
        size_t n = fread(chunk, 1, sizeof(chunk) - 1, file);
        if (n == 0) break;
        chunk[n] = '\0';

        char scan[sizeof(carry) + sizeof(chunk)];
        size_t scan_len = carry_len;
        memcpy(scan, carry, carry_len);
        memcpy(scan + scan_len, chunk, n + 1);
        if (strstr(scan, needle)) {
            found = true;
            break;
        }
        if (needle_len > 1) {
            carry_len = needle_len - 1;
            if (carry_len >= sizeof(carry)) carry_len = sizeof(carry) - 1;
            if (n >= carry_len) {
                memcpy(carry, chunk + n - carry_len, carry_len);
            } else {
                carry_len = n;
                memcpy(carry, chunk, carry_len);
            }
            carry[carry_len] = '\0';
        }
    }
    fclose(file);
    return found;
}

static bool online_view_output_contains(const RadiantOnlineViewResult* result,
                                        const char* needle) {
    return result && result->output.data && strstr(result->output.data, needle) != NULL;
}

static bool online_view_result_contains(const RadiantOnlineViewResult* result,
                                        const char* needle) {
    if (online_view_output_contains(result, needle)) return true;
    if (result && online_view_file_contains(result->lambda_log_path, needle)) return true;
    return false;
}

static uint64_t online_view_parse_tagged_uint64(const char* output,
                                                const char* tag,
                                                const char* key) {
    if (!output || !tag || !key) return 0;
    const char* pos = strstr(output, tag);
    if (!pos) return 0;
    pos = strstr(pos, key);
    if (!pos) return 0;
    pos += strlen(key);
    while (*pos && (*pos < '0' || *pos > '9')) pos++;
    if (!*pos) return 0;
    return strtoull(pos, NULL, 10);
}

static bool online_view_run_case(const RadiantOnlineViewCase* view_case,
                                 RadiantOnlineViewResult* result) {
    if (!view_case || !result) return false;
    memset(result, 0, sizeof(*result));
    result->exit_code = -1;
    online_view_ensure_temp_dir();

    int log_written = snprintf(result->lambda_log_path, sizeof(result->lambda_log_path),
                               "./temp/test_radiant_online_view_%s_lambda.log",
                               view_case->label);
    int out_written = snprintf(result->output_path, sizeof(result->output_path),
                               "./temp/test_radiant_online_view_%s_output.log",
                               view_case->label);
    if (log_written <= 0 || log_written >= (int)sizeof(result->lambda_log_path) ||
        out_written <= 0 || out_written >= (int)sizeof(result->output_path)) {
        result->exec_failed = true;
        return false;
    }

    const char* event_path = "./temp/test_radiant_online_view_events.json";
    if (!online_view_write_noop_events(event_path)) {
        result->exec_failed = true;
        return false;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result->exec_failed = true;
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result->exec_failed = true;
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        setpgid(0, 0);
        setenv("VIEW_MEM_STAGES", "1", 1);
        setenv("LAMBDA_LOG_FILE", result->lambda_log_path, 1);
        // Keep smoke-test diagnostics at warning/error level; per-node debug
        // traces can make large real pages time out before layout finishes.
        setenv("LAMBDA_LOG_LEVEL", "INFO", 1);
        execl(LAMBDA_EXE, LAMBDA_EXE, "view", view_case->url,
              "--event-file", event_path, "--headless", (char*)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    int timeout_seconds = online_view_timeout_seconds();
    time_t deadline = time(NULL) + timeout_seconds;
    bool child_done = false;
    while (!child_done) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pipefd[0], &fds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int selected = select(pipefd[0] + 1, &fds, NULL, NULL, &tv);
        if (selected > 0 && FD_ISSET(pipefd[0], &fds)) {
            char chunk[2048];
            while (true) {
                ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
                if (n > 0) {
                    if (!online_view_buffer_append(&result->output, chunk, (size_t)n)) {
                        result->output_alloc_failed = true;
                    }
                } else if (n == 0) {
                    child_done = true;
                    break;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    child_done = true;
                    break;
                }
            }
        }
        if (!child_done && time(NULL) >= deadline) {
            result->timed_out = true;
            kill(-pid, SIGKILL);
            child_done = true;
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!result->timed_out) result->exit_code = online_view_exit_code(status);

    result->memtrack_live_bytes = online_view_parse_tagged_uint64(
        result->output.data, "[MEMTRACK_LIVE]", "bytes=");
    result->memtrack_live_count = online_view_parse_tagged_uint64(
        result->output.data, "[MEMTRACK_LIVE]", "count=");
    online_view_save_buffer(result->output_path, &result->output);
    return true;
}

static const char* online_view_first_runtime_error(const RadiantOnlineViewResult* result) {
    static const char* patterns[] = {
        // LLVM pages legitimately mention sanitizer names; match sanitizer report prefixes instead.
        "ERROR: AddressSanitizer",
        "AddressSanitizer:DEADLYSIGNAL",
        "ERROR: LeakSanitizer",
        "Segmentation fault",
        "Assertion failed",
        "Fatal error",
        "Failed to load document",
        "failed to create resource manager",
        "view: network resource failures detected",
        "network: download failed",
        "network: failed to load image",
        "Unsupported image format",
        "[BG-IMAGE] Failed",
        "resource_loaders: failed",
        "curl-multi: failed",
        "curl-multi: HTTP",
        "script_runner: failed to download external script",
        "execute_document_scripts: JS execution timed out",
        "execute_document_scripts: recovered from crash",
        "js-mir: unsupported",
        "memtrack: LEAK",
        "Uncaught"
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        if (online_view_result_contains(result, patterns[i])) return patterns[i];
    }
    return NULL;
}

static void online_view_expect_case(size_t index) {
    ASSERT_TRUE(online_view_file_readable(LAMBDA_EXE)) << "lambda.exe not found";
    ASSERT_TRUE(online_view_file_executable(LAMBDA_EXE)) << "lambda.exe is not executable";
    ASSERT_LT(index, sizeof(g_online_view_cases) / sizeof(g_online_view_cases[0]));

    RadiantOnlineViewResult result;
    const RadiantOnlineViewCase* view_case = &g_online_view_cases[index];
    ASSERT_TRUE(online_view_run_case(view_case, &result))
        << "failed to launch online view case " << view_case->label;

    EXPECT_FALSE(result.exec_failed) << "launch failed for " << view_case->url;
    EXPECT_FALSE(result.output_alloc_failed) << "failed to capture output for " << view_case->url;
    EXPECT_FALSE(result.timed_out)
        << view_case->url << " timed out; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;
    EXPECT_EQ(0, result.exit_code)
        << view_case->url << " exited with " << result.exit_code
        << "; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;

    // Top-level HTTP pages are staged through ./temp; the invariant is that
    // the original document URL still drives resource discovery and downloads.
    EXPECT_TRUE(online_view_result_contains(&result, "view: network support initialized for HTTP document"))
        << view_case->url << " did not initialize network support; see "
        << result.lambda_log_path;
    EXPECT_TRUE(online_view_result_contains(&result, "view: network resource discovery complete"))
        << view_case->url << " did not finish resource discovery; see "
        << result.lambda_log_path;
    if (view_case->expect_linked_resources) {
        EXPECT_TRUE(online_view_result_contains(&result, "view: network resource stats total="))
            << view_case->url << " did not report linked resource stats; see "
            << result.lambda_log_path;
    }

    EXPECT_TRUE(online_view_result_contains(&result, "view: document loaded, starting layout"))
        << view_case->url << " did not report document load completion";
    EXPECT_TRUE(online_view_result_contains(&result, "view: layout complete, rendering"))
        << view_case->url << " did not report layout completion";
    EXPECT_TRUE(online_view_result_contains(&result, "view: render complete"))
        << view_case->url << " did not report render completion";
    EXPECT_TRUE(online_view_result_contains(&result, "view command completed with result: 0"))
        << view_case->url << " did not report clean view command shutdown";

    // Runtime errors used to be easy to swallow in logs while the process still
    // exited zero; keep the smoke test sensitive to those reported failures.
    const char* runtime_error = online_view_first_runtime_error(&result);
    EXPECT_EQ((const char*)NULL, runtime_error)
        << view_case->url << " reported runtime/resource/script error pattern: "
        << (runtime_error ? runtime_error : "(none)") << "; stdout/stderr: "
        << result.output_path << ", lambda log: " << result.lambda_log_path;

    EXPECT_TRUE(online_view_output_contains(&result, "[MEMTRACK_LIVE]"))
        << view_case->url << " did not emit memtrack shutdown telemetry";
    EXPECT_EQ(0ULL, result.memtrack_live_bytes)
        << view_case->url << " retained " << (unsigned long long)result.memtrack_live_bytes
        << " bytes; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;
    EXPECT_EQ(0ULL, result.memtrack_live_count)
        << view_case->url << " retained " << (unsigned long long)result.memtrack_live_count
        << " allocations; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;

    online_view_buffer_free(&result.output);
}

static void online_view_expect_case_range(size_t first, size_t past_end) {
    for (size_t i = first; i < past_end; i++) {
        SCOPED_TRACE(i);
        online_view_expect_case(i);
        if (::testing::Test::HasFatalFailure()) return;
    }
}

TEST(RadiantOnlineViewTest, LoadsExampleDotCom) {
    online_view_expect_case(0);
}

TEST(RadiantOnlineViewTest, LoadsWikipediaOrg) {
    online_view_expect_case(1);
}

TEST(RadiantOnlineViewTest, LoadsGoogleCom) {
    online_view_expect_case(2);
}

TEST(RadiantOnlineViewTest, LoadsReactOSOrg) {
    online_view_expect_case(3);
}

TEST(RadiantOnlineViewTest, LoadsFirefoxCom) {
    online_view_expect_case(4);
}

TEST(RadiantOnlineViewTest, LoadsWikipediaMainPage) {
    online_view_expect_case(5);
}

TEST(RadiantOnlineViewTest, LoadsApacheFoundation) {
    online_view_expect_case(6);
}

TEST(RadiantOnlineViewTest, LoadsPerlOrg) {
    online_view_expect_case(7);
}

TEST(RadiantOnlineViewTest, LoadsRubyLangOrg) {
    online_view_expect_case(8);
}

TEST(RadiantOnlineViewTest, LoadsCurlSE) {
    online_view_expect_case(9);
}

TEST(RadiantOnlineViewTest, LoadsOpenSSLOrg) {
    online_view_expect_case(10);
}

TEST(RadiantOnlineViewTest, LoadsNginxOrg) {
    online_view_expect_case(11);
}

TEST(RadiantOnlineViewTest, LoadsApacheOrg) {
    online_view_expect_case(12);
}

TEST(RadiantOnlineViewTest, LoadsLibpngOrg) {
    online_view_expect_case(13);
}

TEST(RadiantOnlineViewTest, LoadsLuaOrg) {
    online_view_expect_case(14);
}

TEST(RadiantOnlineViewTest, LoadsZlibNet) {
    online_view_expect_case(15);
}

TEST(RadiantOnlineViewTest, LoadsExampleDotOrg) {
    online_view_expect_case(16);
}

TEST(RadiantOnlineViewTest, LoadsIANAReservedDomains) {
    online_view_expect_case(17);
}

TEST(RadiantOnlineViewTest, LoadsNetlibOrg) {
    online_view_expect_case(18);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteOrg) {
    online_view_expect_case(19);
}

TEST(RadiantOnlineViewTest, LoadsSchemeOrg) {
    online_view_expect_case(20);
}

TEST(RadiantOnlineViewTest, LoadsMercurialSCMOrg) {
    online_view_expect_case(21);
}

TEST(RadiantOnlineViewTest, LoadsSubversionApacheOrg) {
    online_view_expect_case(22);
}

TEST(RadiantOnlineViewTest, LoadsBusyBoxNet) {
    online_view_expect_case(23);
}

TEST(RadiantOnlineViewTest, LoadsMuslLibcOrg) {
    online_view_expect_case(24);
}

TEST(RadiantOnlineViewTest, LoadsLinuxFromScratchOrg) {
    online_view_expect_case(25);
}

TEST(RadiantOnlineViewTest, LoadsExampleDotNet) {
    online_view_expect_case(26);
}

TEST(RadiantOnlineViewTest, LoadsCurlManpage) {
    online_view_expect_case(27);
}

TEST(RadiantOnlineViewTest, LoadsCurlSSLCerts) {
    online_view_expect_case(28);
}

TEST(RadiantOnlineViewTest, LoadsNginxCoreModule) {
    online_view_expect_case(29);
}

TEST(RadiantOnlineViewTest, LoadsCurlFAQ) {
    online_view_expect_case(30);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteDatatypes) {
    online_view_expect_case(31);
}

TEST(RadiantOnlineViewTest, LoadsTomcatDocs) {
    online_view_expect_case(32);
}

TEST(RadiantOnlineViewTest, LoadsAntManual) {
    online_view_expect_case(33);
}

TEST(RadiantOnlineViewTest, LoadsCurlDocs) {
    online_view_expect_case(34);
}

TEST(RadiantOnlineViewTest, LoadsCurlLibcurl) {
    online_view_expect_case(35);
}

TEST(RadiantOnlineViewTest, LoadsNginxDocs) {
    online_view_expect_case(36);
}

TEST(RadiantOnlineViewTest, LoadsApacheHTTPDDocs) {
    online_view_expect_case(37);
}

TEST(RadiantOnlineViewTest, LoadsApacheAPR) {
    online_view_expect_case(38);
}

TEST(RadiantOnlineViewTest, LoadsApacheTomcat) {
    online_view_expect_case(39);
}

TEST(RadiantOnlineViewTest, LoadsApacheAnt) {
    online_view_expect_case(40);
}

TEST(RadiantOnlineViewTest, LoadsApacheMaven) {
    online_view_expect_case(41);
}

TEST(RadiantOnlineViewTest, LoadsOpenSSLDocs) {
    online_view_expect_case(42);
}

TEST(RadiantOnlineViewTest, LoadsMavenGuides) {
    online_view_expect_case(43);
}

TEST(RadiantOnlineViewTest, LoadsLuaPIL) {
    online_view_expect_case(44);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteDocs) {
    online_view_expect_case(45);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteLang) {
    online_view_expect_case(46);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteCLI) {
    online_view_expect_case(47);
}

TEST(RadiantOnlineViewTest, LoadsZlibManual) {
    online_view_expect_case(48);
}

TEST(RadiantOnlineViewTest, LoadsLibpngIntro) {
    online_view_expect_case(49);
}

TEST(RadiantOnlineViewTest, LoadsSQLitePragma) {
    online_view_expect_case(50);
}

TEST(RadiantOnlineViewTest, LoadsBusyBoxAbout) {
    online_view_expect_case(51);
}

TEST(RadiantOnlineViewTest, LoadsNetlibLAPACK) {
    online_view_expect_case(52);
}

TEST(RadiantOnlineViewTest, LoadsNetlibBLAS) {
    online_view_expect_case(53);
}

TEST(RadiantOnlineViewTest, LoadsIANAAbout) {
    online_view_expect_case(54);
}

TEST(RadiantOnlineViewTest, LoadsIANANumbers) {
    online_view_expect_case(55);
}

TEST(RadiantOnlineViewTest, LoadsOpenStdC) {
    online_view_expect_case(56);
}

TEST(RadiantOnlineViewTest, LoadsOpenStdCPP) {
    online_view_expect_case(57);
}

TEST(RadiantOnlineViewTest, LoadsTclLang) {
    online_view_expect_case(58);
}

TEST(RadiantOnlineViewTest, LoadsCPANOrg) {
    online_view_expect_case(59);
}

TEST(RadiantOnlineViewTest, LoadsPCREOrg) {
    online_view_expect_case(60);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteCAPIIntro) {
    online_view_expect_case(61);
}

TEST(RadiantOnlineViewTest, LoadsSQLiteResultCodes) {
    online_view_expect_case(62);
}

TEST(RadiantOnlineViewTest, LoadsCairoGraphicsOrg) {
    online_view_expect_case(63);
}

TEST(RadiantOnlineViewTest, LoadsHTTPDHome) {
    online_view_expect_case(64);
}

TEST(RadiantOnlineViewTest, LoadsXOrg) {
    online_view_expect_case(65);
}

TEST(RadiantOnlineViewTest, LoadsGNUHome) {
    online_view_expect_case(66);
}

TEST(RadiantOnlineViewTest, LoadsPythonOrg) {
    online_view_expect_case(67);
}

TEST(RadiantOnlineViewTest, LoadsKernelOrg) {
    online_view_expect_case(68);
}

TEST(RadiantOnlineViewTest, LoadsW3COrg) {
    online_view_expect_case(69);
}

TEST(RadiantOnlineViewTest, LoadsIANAHome) {
    online_view_expect_case(70);
}

TEST(RadiantOnlineViewTest, LoadsDebianOrg) {
    online_view_expect_case(71);
}

TEST(RadiantOnlineViewTest, LoadsFreeBSDOrg) {
    online_view_expect_case(72);
}

TEST(RadiantOnlineViewTest, LoadsPostgreSQLOrg) {
    online_view_expect_case(73);
}

TEST(RadiantOnlineViewTest, LoadsRustLangOrg) {
    online_view_expect_case(74);
}

TEST(RadiantOnlineViewTest, LoadsPHPNet) {
    online_view_expect_case(75);
}

TEST(RadiantOnlineViewTest, LoadsOpenBSDOrg) {
    online_view_expect_case(76);
}

TEST(RadiantOnlineViewTest, LoadsVimOrg) {
    online_view_expect_case(77);
}

TEST(RadiantOnlineViewTest, LoadsRProjectOrg) {
    online_view_expect_case(78);
}

TEST(RadiantOnlineViewTest, LoadsCMakeOrg) {
    online_view_expect_case(79);
}

TEST(RadiantOnlineViewTest, LoadsLLVMOrg) {
    online_view_expect_case(80);
}

TEST(RadiantOnlineViewTest, LoadsRFCEditorOrg) {
    online_view_expect_case(81);
}

TEST(RadiantOnlineViewTest, LoadsUnicodeOrg) {
    online_view_expect_case(82);
}

TEST(RadiantOnlineViewTest, LoadsGitSCMCom) {
    online_view_expect_case(83);
}

TEST(RadiantOnlineViewTest, LoadsValgrindOrg) {
    online_view_expect_case(84);
}

TEST(RadiantOnlineViewTest, LoadsGnuplotInfo) {
    online_view_expect_case(85);
}

TEST(RadiantOnlineViewTest, LoadsGNUBash) {
    online_view_expect_case(86);
}

TEST(RadiantOnlineViewTest, LoadsGNUMake) {
    online_view_expect_case(87);
}

TEST(RadiantOnlineViewTest, LoadsGNUCoreutils) {
    online_view_expect_case(88);
}

TEST(RadiantOnlineViewTest, LoadsGNUGrep) {
    online_view_expect_case(89);
}

TEST(RadiantOnlineViewTest, LoadsGNUSed) {
    online_view_expect_case(90);
}

TEST(RadiantOnlineViewTest, LoadsGNUGawk) {
    online_view_expect_case(91);
}

TEST(RadiantOnlineViewTest, LoadsGNUGDB) {
    online_view_expect_case(92);
}

TEST(RadiantOnlineViewTest, LoadsLuaManual) {
    online_view_expect_case(93);
}

TEST(RadiantOnlineViewTest, LoadsMuslManual) {
    online_view_expect_case(94);
}

TEST(RadiantOnlineViewTest, LoadsTcpdumpOrg) {
    online_view_expect_case(95);
}

TEST(RadiantOnlineViewTest, LoadsLibreSSLOrg) {
    online_view_expect_case(96);
}

TEST(RadiantOnlineViewTest, LoadsFreedesktopOrg) {
    online_view_expect_case(97);
}

TEST(RadiantOnlineViewTest, LoadsHTTPD24Docs) {
    online_view_expect_case(98);
}

TEST(RadiantOnlineViewTest, LoadsAPRGeneratedDocs) {
    online_view_expect_case(99);
}

TEST(RadiantOnlineViewTest, LoadsIANAProtocols) {
    online_view_expect_case(100);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages001To010) {
    online_view_expect_case_range(101, 111);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages011To020) {
    online_view_expect_case_range(111, 121);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages021To030) {
    online_view_expect_case_range(121, 131);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages031To040) {
    online_view_expect_case_range(131, 141);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages041To050) {
    online_view_expect_case_range(141, 151);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages051To060) {
    online_view_expect_case_range(151, 161);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages061To070) {
    online_view_expect_case_range(161, 171);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages071To080) {
    online_view_expect_case_range(171, 181);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages081To090) {
    online_view_expect_case_range(181, 191);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages091To100) {
    online_view_expect_case_range(191, 201);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages101To110) {
    online_view_expect_case_range(201, 211);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages111To120) {
    online_view_expect_case_range(211, 221);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages121To130) {
    online_view_expect_case_range(221, 231);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages131To140) {
    online_view_expect_case_range(231, 241);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages141To150) {
    online_view_expect_case_range(241, 251);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages151To160) {
    online_view_expect_case_range(251, 261);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages161To170) {
    online_view_expect_case_range(261, 271);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages171To180) {
    online_view_expect_case_range(271, 281);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages181To190) {
    online_view_expect_case_range(281, 291);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages191To200) {
    online_view_expect_case_range(291, 301);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages201To210) {
    online_view_expect_case_range(301, 311);
}

TEST(RadiantOnlineViewTest, LoadsNewOnlinePages211To220) {
    online_view_expect_case_range(311, 321);
}

#endif
