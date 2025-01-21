import XCTest
import SwiftTreeSitter
import TreeSitterLambda

final class TreeSitterLambdaTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_lambda())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Lambda grammar")
    }
}
