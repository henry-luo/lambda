# zh-Hant candidate-quality baseline

**Ranking update, 2026-10-26 (IME7v2):** complete exact/fuzzy readings now
precede partial routes. A fresh qualification against the unchanged frozen
fixture gives full-pinyin **61/64 top 1, 64/64 top 3 and top 9**; all-fuzzy
results are identical. Initials remain **22/35/50** at top 1/3/9. All 64 words
remain found, no individual rank regresses, and both tiers agree on all 192
results. The original acceptance floors are unchanged. Before/after evidence:
`temp/ime_chinese/ranking/{before-hant,after-hant-jit,after-hant-interp}.json`
and `comparison.json`. The table and timing samples below retain the original
M4 snapshot; no performance improvement is claimed.

Recorded 2026-10-26 for M4 (IME11 / proposal §5.6). The fixture contains 64
repository-authored MIT cases with explicitly authored Traditional spellings
and the same readings as M1. It is a regression corpus; representative external
corpus quality is unqualified. No profile-specific ranking tuning was applied.

The first report froze [acceptance floors](baseline_zh_hant.json) before any
Traditional tuning. The [fixture](heldout_zh_hant.json) SHA-256 is
`4382e19e435e0e83ce923a66c8f9b49ad59d451d905c952140ab600d3a28917a`.

| Query | Found | Top 1 | Top 3 | Top 9 | Truncated | Simulated keys/character |
|---|---:|---:|---:|---:|---:|---:|
| Full pinyin | 64/64 | 52/64 | 63/64 | 64/64 | 44/64 | 3.645 |
| Orthographic initials | 64/64 | 22/64 | 35/64 | 50/64 | 64/64 | 1.837 |
| Full pinyin, all fuzzy pairs | 64/64 | 51/64 | 61/64 | 64/64 | 53/64 | 3.645 |

The frozen top-9 and keystroke model below retain the original nine-option
query baseline. The popup/session now defaults to seven options; an explicit
`page_size: 9` remains supported.

Truncation reports the existing key/posting/path/candidate budgets, even when
the expected word was found. Keystrokes mean typed input plus one selection
and one PageDown per nine preceding candidates; this is a query simulation,
not a replay of the partial-selection session. Missing words are excluded
from that ratio; none are missing here.

Interpreter and JIT agree on all **192/192** ranks, leading words, weights,
truncation flags and scan counts. The Simplified control agrees with the M2
reference on all **192/192** corresponding results.

The independent 32-case [conversion fixture](chinese_traditional_coverage.json)
matches every pinned conversion and all **38/38** expected word/readings are
present in the full pack. Source readings are inherited without guessing;
ambiguous Traditional forms are retained as separate candidates.

The profile has 234,399 lexicon postings (11,351 character readings and
223,048 word readings). It expands 17,164 ambiguous source postings, merges
186 identical word/readings by maximum source weight, and drops no converted
forms. The allowed character set contains 8,363 Han characters. Conversion
uses OpenCC STPhrases and STCharacters from
[commit 025f371](https://github.com/BYVoid/OpenCC/tree/025f371dc76b598d77384fbdab90c937471844d8)
(ver.1.4.2); the source lock and generated NOTICE retain the exact pins and
complete licenses. Regional vocabulary remains deferred (IME-O8).

The lexicon and abbreviation tables occupy 5,378,639 and 4,772,612 UTF-8 bytes;
their XZ versions total 3,026,368 bytes. Bytes plus u32 line-offset payloads
total 10,939,259 bytes. This excludes allocator/runtime overhead and is not
retained-heap attribution.

Release-host samples are included for scope, not a performance improvement
claim. Load includes resource reads and index construction; query percentiles
span each query's median of three samples. End-to-end per-key latency is not
measured. The provisional 200 ms load / 5 ms JIT p95 targets remain unmet.

| Tier | Load ms | Full pinyin p50/p95 ms | Initials p50/p95 ms | All-fuzzy p50/p95 ms | Process peak RSS bytes |
|---|---:|---:|---:|---:|---:|
| JIT | 343.37 | 3.98 / 19.67 | 7.36 / 13.15 | 8.06 / 34.29 | 209,616,896 |
| Interpreter | 688.14 | 15.36 / 70.29 | 22.74 / 42.65 | 26.04 / 119.70 | 172,343,296 |

Host: `macOS-26.5.2-arm64-arm-64bit-Mach-O`. Release executable SHA-256:
`11e528e8bdc6d7dfd358b0a2a8b865921924abe1fa5823251cea937684a07d1f`.
Pack manifest SHA-256: `daf3ea0a794b70d1410b213270602d7285abb1bbae66ac13f5f615ee422b6262`.
Local JSON reports, row-level samples and source hashes are under
`temp/ime_chinese/m4/qualification-hant-{jit-final,interp}.json`.

Reproduce against a release host after preparing the pack:

```sh
python3 test/ime/run_engine.py --executable <release-host> --profile zh-Hant \
  --tier jit --report temp/ime_chinese/m4/qualification-hant-jit.json
```

| Expected word | Reading | Full rank | Initials rank | All-fuzzy rank |
|---|---|---:|---:|---:|
| 上海 | shang'hai | 1 | 5 | 1 |
| 天津 | tian'jin | 1 | 7 | 1 |
| 廣州 | guang'zhou | 3 | 9 | 3 |
| 深圳 | shen'zhen | 1 | 20 | 1 |
| 南京 | nan'jing | 1 | 2 | 1 |
| 杭州 | hang'zhou | 1 | 14 | 1 |
| 蘇州 | su'zhou | 1 | 6 | 1 |
| 成都 | cheng'du | 3 | 4 | 5 |
| 武漢 | wu'han | 1 | 3 | 1 |
| 長沙 | chang'sha | 1 | 14 | 1 |
| 昆明 | kun'ming | 2 | 2 | 2 |
| 哈爾濱 | ha'er'bin | 1 | 1 | 1 |
| 圖書館 | tu'shu'guan | 1 | 1 | 1 |
| 博物館 | bo'wu'guan | 1 | 1 | 1 |
| 火車站 | huo'che'zhan | 1 | 1 | 1 |
| 飛機場 | fei'ji'chang | 2 | 2 | 2 |
| 公共汽車 | gong'gong'qi'che | 2 | 2 | 2 |
| 高速公路 | gao'su'gong'lu | 1 | 1 | 1 |
| 自行車 | zi'xing'che | 1 | 1 | 1 |
| 出租車 | chu'zu'che | 1 | 1 | 1 |
| 計算機 | ji'suan'ji | 1 | 1 | 1 |
| 軟件 | ruan'jian | 1 | 1 | 1 |
| 數據庫 | shu'ju'ku | 1 | 1 | 1 |
| 網絡 | wang'luo | 2 | 2 | 2 |
| 程序 | cheng'xu | 1 | 9 | 1 |
| 語言 | yu'yan | 1 | 12 | 1 |
| 文件 | wen'jian | 1 | 1 | 1 |
| 系統 | xi'tong | 1 | 1 | 1 |
| 科學 | ke'xue | 1 | 3 | 1 |
| 技術 | ji'shu | 1 | 3 | 1 |
| 教育 | jiao'yu | 1 | 5 | 1 |
| 學校 | xue'xiao | 1 | 3 | 1 |
| 老師 | lao'shi | 1 | 1 | 1 |
| 學生 | xue'sheng | 1 | 1 | 1 |
| 課堂 | ke'tang | 1 | 6 | 1 |
| 作業 | zuo'ye | 1 | 7 | 1 |
| 文化 | wen'hua | 1 | 1 | 1 |
| 歷史 | li'shi | 1 | 12 | 1 |
| 世界 | shi'jie | 1 | 13 | 1 |
| 生活 | sheng'huo | 1 | 4 | 1 |
| 天氣 | tian'qi | 1 | 2 | 1 |
| 今天 | jin'tian | 1 | 1 | 1 |
| 明天 | ming'tian | 1 | 1 | 1 |
| 昨天 | zuo'tian | 1 | 1 | 1 |
| 時間 | shi'jian | 1 | 1 | 1 |
| 現在 | xian'zai | 1 | 1 | 1 |
| 早上 | zao'shang | 1 | 5 | 1 |
| 晚上 | wan'shang | 1 | 1 | 1 |
| 早飯 | zao'fan | 4 | 9 | 7 |
| 午飯 | wu'fan | 2 | 7 | 2 |
| 健康 | jian'kang | 1 | 1 | 1 |
| 醫院 | yi'yuan | 1 | 8 | 1 |
| 醫生 | yi'sheng | 1 | 13 | 4 |
| 護士 | hu'shi | 1 | 28 | 1 |
| 水果 | shui'guo | 1 | 11 | 1 |
| 蘋果 | ping'guo | 2 | 2 | 3 |
| 西瓜 | xi'gua | 1 | 12 | 1 |
| 葡萄 | pu'tao | 1 | 5 | 1 |
| 米飯 | mi'fan | 2 | 12 | 2 |
| 麪條 | mian'tiao | 3 | 14 | 3 |
| 選擇 | xuan'ze | 1 | 2 | 1 |
| 候選 | hou'xuan | 3 | 48 | 3 |
| 設置 | she'zhi | 1 | 18 | 1 |
| 用戶 | yong'hu | 1 | 2 | 1 |
