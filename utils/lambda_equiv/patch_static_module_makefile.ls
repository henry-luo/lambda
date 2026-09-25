// Native Lambda entry point for utils/patch_static_module_makefile.py.
import .patch_static_module_makefile_core

pn main() {
    if (sys.os.platform# == "darwin") {
        let root = string(sys.proc.self.cwd#)
        let written = patch_makefile("build_lambda_config.json",
                                     "build/premake/lambda-exe.make", root)
    }
    return
}
