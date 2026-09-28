# walmes/Tikz rendering samples

The `.pgf` fragments are copied unchanged from
[walmes/Tikz](https://github.com/walmes/Tikz) at commit
`3b873c32dc19938136c8215909d165a25622d0d7`. The repository does not
contain a license file; its README notes that some figures were adapted from
other web sources. Keep the fragments intact. Lambda opens them directly through
the TikZ/PGF document transform.

| Fragment | Upstream source | Feature exercised |
| --- | --- | --- |
| `segmentada.pgf` | [`segmentada.pgf`](https://github.com/walmes/Tikz/blob/3b873c32dc19938136c8215909d165a25622d0d7/src/segmentada.pgf) | piecewise function |
| `polar.pgf` | [`polar.pgf`](https://github.com/walmes/Tikz/blob/3b873c32dc19938136c8215909d165a25622d0d7/src/polar.pgf) | polar axis |
| `plot_logaxis.pgf` | [`plot_logaxis.pgf`](https://github.com/walmes/Tikz/blob/3b873c32dc19938136c8215909d165a25622d0d7/src/plot_logaxis.pgf) | logarithmic and linear coordinate plots |

Use `./lambda.exe render test/input/tikz/<name>.pgf -o ./temp/<name>.png`.
