// both launch modes read the same environment snapshot and history location.
let ENV = sys.proc.self.env#
pub fn setting(setting_name, fallback) => (let value = ENV[setting_name],
    if (value != null and value != "") value else fallback)
pub let root = setting("CHAT_DEMO_ROOT", "temp/chat")
pub let endpoint = setting("CHAT_DEMO_ENDPOINT", null)
