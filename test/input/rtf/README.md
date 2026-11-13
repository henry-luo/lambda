# Native RTF reference outputs

`native_oracles.json` captures the complete `format(..., 'json')` output of
`parse(source, 'rtf')` before the native parser was retired. The sources are the
named cases, `test/input/test.rtf`, 511-level body and 1,500-level skipped
destination in `test/lambda/rtf_package.ls`. The test compares both script
readers against these saved results, including their element styles and text.

Capture provenance (2026-11-13):

- Lambda HEAD: `0a95bb4145bae64b5f9649a403ba21c3f8daf43c`.
- lambda-input HEAD: `1075914a6e3da80fd254399b7f1fa8a57a3b975d`.
- Native `input-rtf.cpp` SHA-256: `5716ffd9df6bde3da0d22b6e05d5605749e7808960e66d332335d49fc5a6cfbe`.
- Capture executable SHA-256: `49f27921b41f9c645ebab81bba9ba688de1f5f257b3387b12dc904209eecd6c0`.
- `test/input/test.rtf` SHA-256: `f6ea82fa5bcce91a4c206efe8a76e360bb5ad0001e640861476b9a715e9e5b13`.
- `native_oracles.json` SHA-256: `aa6e5b1a710282355a7312944212b0547d50d5c5bffab65e5674dcd833e9a359`.

The reference was captured from the native parser, not from either script
reader. It is intentionally retained after the C implementation is removed.
