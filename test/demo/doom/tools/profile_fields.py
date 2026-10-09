"""Read complete numeric key/value fields from native frame-profile lines."""
import re
import statistics
from pathlib import Path

NUMERIC_FIELD = re.compile(r"(\w+)=([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(?=\s|$)")


def numeric_fields(line):
    return {key: float(value) for key, value in NUMERIC_FIELD.findall(line)}


def profile_records(engine_log, application_log=None):
    records = []
    for path in dict.fromkeys([engine_log, application_log or engine_log]):
        for line in Path(path).read_text().splitlines():
            for marker in ('DOOM_PROFILE', '[FRAME_PROFILE]', '[FRAME_MEMORY]'):
                if marker not in line:
                    continue
                fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
                fields.update(numeric_fields(line))
                records.append((marker.strip('[]'), fields))
    offsets = [fields['timestamp_ms'] - fields['document_timestamp_ms']
               for marker, fields in records if marker == 'FRAME_PROFILE']
    if not offsets or max(offsets) - min(offsets) > .002:
        raise ValueError('Missing or inconsistent native/document clock alignment')
    offset = statistics.median(offsets)
    for marker, fields in records:
        if marker == 'DOOM_PROFILE':
            fields['timestamp_ms'] += offset
    return sorted(records, key=lambda record: (record[1]['timestamp_ms'],
                  ('DOOM_PROFILE', 'FRAME_PROFILE', 'FRAME_MEMORY').index(record[0])))
