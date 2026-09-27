// S16.8.6v3: the open count is `{n+}`; inside a pattern `{2,}` had taken
// regex's meaning, where type position rejects it.
type bad_open = \(d{2,})
