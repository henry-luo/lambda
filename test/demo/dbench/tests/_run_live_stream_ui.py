#!/usr/bin/env python3
"""Exercise complete export through disposable, TLS-enabled database servers."""
import json
import os
from pathlib import Path
from _live_ui_helpers import run_events

root = Path(__file__).resolve().parents[4]
queries = {
    "postgresql": "SELECT n FROM generate_series(1,3000) AS n ORDER BY n",
    "mysql": "WITH RECURSIVE digits(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM digits WHERE n<9) "
             "SELECT 1+a.n+10*b.n+100*c.n+1000*d.n AS num FROM digits a CROSS JOIN digits b "
             "CROSS JOIN digits c CROSS JOIN digits d WHERE a.n+10*b.n+100*c.n+1000*d.n<3000 ORDER BY num",
}
for driver, sql in queries.items():
    uri = os.environ["DBENCH_TEST_" + driver.upper() + "_URI"]
    output = root / ("temp/dbench-live-stream-" + driver + ".json")
    output.unlink(missing_ok=True)
    events = [
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-rows", "equals": "256"},
        {"type": "select_option", "target": {"selector": "#driver"}, "value": driver},
        {"type": "type", "target": {"selector": "#target"}, "text": uri, "clear": True},
        {"type": "click", "target": {"selector": "#connect"}},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-generation", "equals": "2"},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-connected", "equals": "true", "assert_timeout": 30000},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-loading", "equals": "false", "assert_timeout": 30000},
        {"type": "type", "target": {"selector": "#sql"}, "text": sql, "clear": True},
        {"type": "click", "target": {"selector": "#run"}},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-mode", "equals": "query", "assert_timeout": 30000},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-loading", "equals": "false", "assert_timeout": 30000},
    ]
    for count in range(512, 2561, 256):
        events.extend([
            {"type": "click", "target": {"selector": "#more"}},
            {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-rows", "equals": str(count), "assert_timeout": 30000},
        ])
    events.extend([
        {"type": "script", "js": "const p=document.getElementById('result-scroll');p.scrollTop=54000;p.dispatchEvent(new Event('scroll'))"},
        {"type": "assert_text", "target": {"selector": "#result-2000-0"}, "contains": "2001", "assert_timeout": 30000},
        {"type": "script", "js": "const p=document.getElementById('result-scroll');p.scrollTop=0;p.dispatchEvent(new Event('scroll'))"},
        {"type": "assert_text", "target": {"selector": "#result-0-0"}, "contains": "1", "assert_timeout": 30000},
        {"type": "click", "target": {"selector": "#export"}},
        {"type": "type", "target": {"selector": "#export-path"}, "text": str(output.relative_to(root)), "clear": True},
        {"type": "select_option", "target": {"selector": "#export-kind"}, "value": "json"},
        {"type": "select_option", "target": {"selector": "#export-scope"}, "value": "complete"},
        {"type": "click", "target": {"selector": "#export-save"}},
        {"type": "assert_text", "target": {"selector": "#status"}, "contains": "Created ", "assert_timeout": 30000},
        {"type": "click", "target": {"selector": "#tab-new"}},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-active-tab", "equals": "2"},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-connected", "equals": "true", "assert_timeout": 30000},
        {"type": "assert_attribute", "target": {"selector": "#dbench"}, "attribute": "data-loading", "equals": "false", "assert_timeout": 30000},
        {"type": "type", "target": {"selector": "#sql"}, "text": "SELECT 77 AS answer", "clear": True},
        {"type": "click", "target": {"selector": "#run"}},
        {"type": "assert_text", "target": {"selector": "#result-0-0"}, "contains": "77", "assert_timeout": 30000},
        {"type": "click", "target": {"selector": "#tab-1"}},
        {"type": "assert_text", "target": {"selector": "#result-0-0"}, "contains": "1", "assert_timeout": 30000},
        {"type": "click", "target": {"selector": "#tab-2"}},
        {"type": "assert_text", "target": {"selector": "#result-0-0"}, "contains": "77", "assert_timeout": 30000},
    ])
    try:
        run_events("DBench live streamed export", events, timeout=90)
        result = json.loads(output.read_text(encoding="utf-8"))
        assert result["rows"] == [[n] for n in range(1, 3001)]
    finally:
        output.unlink(missing_ok=True)
