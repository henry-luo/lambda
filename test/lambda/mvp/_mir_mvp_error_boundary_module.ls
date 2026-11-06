// S11.1.7/S11.4.2v3: this export has no non-Error return value.
pub fn failure(message: string) error => error({code: 7005, message: message})
