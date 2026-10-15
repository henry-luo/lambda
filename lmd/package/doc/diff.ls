// lambda.doc.diff is an explicit source-package facade (D7.2.4/D7.2.6).
import parser: .diff.parse
import git: .diff.git
import presentation: .diff.presentation
import surface: .diff.surface

pub fn parse(source,options = null) map^ => parser.parse(source,options)^
pub fn to_html(document,options = null) element^ => presentation.to_html(document,options)^
pub fn viewer(document,options = null) element^ => surface.viewer(document,options)^
pub fn repository(path,options = null) element^ => surface.repository(path,options)^
pub pn read(path,options = null) map^ {
    let source = input(path,"binary")^;
    return parse(source,options)^
}
pub pn open_repo(path,options = null) map^ { return git.open_repo(path,options)^ }
pub pn compare(repo,comparison = "unstaged",options = null) map^ { return git.compare(repo,comparison,options)^ }
pub pn load_file(repo,snapshot,id,options = null) map^ { return git.load_file(repo,snapshot,id,options)^ }
