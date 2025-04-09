// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "TreeSitterLambda",
    products: [
        .library(name: "TreeSitterLambda", targets: ["TreeSitterLambda"]),
    ],
    dependencies: [
        .package(url: "https://github.com/ChimeHQ/SwiftTreeSitter", from: "0.8.0"),
    ],
    targets: [
        .target(
            name: "TreeSitterLambda",
            dependencies: [],
            path: ".",
            sources: [
                "src/parser.c",
                // NOTE: if your language has an external scanner, add it here.
            ],
            resources: [
                .copy("queries")
            ],
            publicHeadersPath: "bindings/swift",
            cSettings: [.headerSearchPath("src")]
        ),
        .testTarget(
            name: "TreeSitterLambdaTests",
            dependencies: [
                "SwiftTreeSitter",
                "TreeSitterLambda",
            ],
            path: "bindings/swift/TreeSitterLambdaTests"
        )
    ],
    cLanguageStandard: .c11
)
