// Native MusicXML package; public values and raised errors follow D7.2.4/S7.4.2.
import xml: .music.musicxml
import engraving: .music.layout
import paint: .music.engrave
import output: .music.svg
import performance_model: .music.performance
import midi_codec: .music.midi
import playback: .music.player

pub fn load(source,options = {}) map^ => xml.load(source,options)^
pub fn parse(source,options = {}) map^ => xml.parse(source,options)^
pub fn from_xml(document,options = {}) map^ => xml.from_xml(document,options)^
pub fn layout(score,options = {}) map^ => paint.finish(engraving.layout(score,options)^)^
pub fn svg(layout,page = 0) element^ => output.svg(layout,page)^
pub fn render(score,options = {}) element^ => output.document(layout(score,options)^)^
pub fn performance(score,options = {}) map^ => performance_model.build(score,options)^
pub fn read_midi(bytes,options = {}) map^ => midi_codec.read(bytes,options)^
pub fn midi(performance,options = {}) binary^ => midi_codec.write(performance,options)^
pub fn player(source,options = {}) element^ => playback.create(source,options)^
