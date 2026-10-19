"""Focused admission tests; run from the repository root with python3."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'test/benchmark'))
from contracts import freeze_lambda_closure, havlak_contract, match_havlak, require_same_work
from expansion import prepare_new


class Contracts(unittest.TestCase):
    def setUp(self):
        (ROOT / 'temp').mkdir(exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=ROOT / 'temp', prefix='mvp-contract-')
        self.out = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

    def test_havlak_equal_output_does_not_admit_mismatched_work(self):
        row = prepare_new(self.out, 'lambda.exe', True, ['awfy/havlak'])[0]
        with self.assertRaises(ValueError):
            require_same_work(*row['work_contracts'].values())
        for count in (1, 50):
            match_havlak(row, count)
            require_same_work(*row['work_contracts'].values())
            self.assertEqual(row['work_contracts']['candidate']['full_recognitions'], count + 1)

    def test_transitive_annotations(self):
        rows = prepare_new(self.out, 'lambda.exe', True, ['julia/iteration_pi_sum', 'text/hyphen'])
        self.assertEqual(rows[0]['lambda_numeric_annotations'], [])
        self.assertIn('hyphen_core.ls', rows[1]['lambda_numeric_annotations'])
        self.assertTrue((self.out / 'lambda_reference/hyphen_common.ls').is_file())

    def test_missing_import_fails_closed(self):
        source = self.out / 'source'; source.mkdir()
        entry = source / 'entry.ls'; entry.write_text('import .missing\n1\n')
        with self.assertRaises(ValueError):
            freeze_lambda_closure(entry, source, self.out / 'frozen')

    def test_comments_strings_and_import_cycles(self):
        source = self.out / 'source'; source.mkdir()
        entry = source / 'entry.ls'
        entry.write_text('// import .missing\nimport .peer\n"import .absent: int"\n')
        (source / 'peer.ls').write_text('import .entry\n1\n')
        _, files, annotated = freeze_lambda_closure(entry, source, self.out / 'frozen')
        self.assertEqual(len(files), 4)
        self.assertEqual(annotated, [])


if __name__ == '__main__':
    unittest.main()
