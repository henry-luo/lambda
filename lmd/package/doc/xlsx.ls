// SpreadsheetML import builds on the shared OPC reader (D9.3.4, D11.1.2).
import model: .xlsx.model

pub fn convert(source) map^ => model.read(source)^
pub fn load(source) map^ => convert(source)^
