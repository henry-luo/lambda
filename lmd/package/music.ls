// Native MusicXML package; public values and raised errors follow D7.2.4/S7.4.2.
import xml: .music.musicxml
import engraving: .music.layout
import paint: .music.engrave
import output: .music.svg

pub fn load(source,options = {}) map^ => xml.load(source,options)^
pub fn parse(source,options = {}) map^ => xml.parse(source,options)^
pub fn from_xml(document,options = {}) map^ => xml.from_xml(document,options)^
pub fn layout(score,options = {}) map^ => paint.finish(engraving.layout(score,options)^)^
pub fn svg(layout,page = 0) element^ => output.svg(layout,page)^
pub fn render(score,options = {}) element^ => output.document(layout(score,options)^)^
