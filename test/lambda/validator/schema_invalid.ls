// Schema for testing invalid cases - strict validation
type StrictSchema = {
    wrong_type: int,
    missing_required: string,
    invalid_array: int*,
    bad_format: bool
}
