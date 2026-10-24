import os
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

try:
    import lichess_download
except ImportError:                     # python-chess / zstandard not installed.
    lichess_download = None


@unittest.skipIf(lichess_download is None, "needs python-chess and zstandard")
class FetchTest(unittest.TestCase):
    def setUp(self):
        self.quiet = lichess_download.console.quiet
        lichess_download.console.quiet = True
        self.pauses = []

    def tearDown(self):
        lichess_download.console.quiet = self.quiet

    def fetch(self, outcomes, retries=3):
        with mock.patch.object(lichess_download.lichess, "download", side_effect=outcomes) as download:
            try:
                return lichess_download.fetch("2026-01", "dir", retries, 60, self.pauses.append)
            finally:
                self.calls = download.call_count

    def test_retries_after_failures_with_longer_pauses(self):
        outcomes = [urllib.error.URLError("reset"), ConnectionResetError(), "dir/dump.pgn.zst"]
        self.assertEqual("dir/dump.pgn.zst", self.fetch(outcomes))
        self.assertEqual(3, self.calls)
        self.assertEqual([60, 120], self.pauses)

    def test_gives_up_after_the_retries(self):
        with self.assertRaises(urllib.error.URLError):
            self.fetch([urllib.error.URLError("down")] * 3, retries=2)
        self.assertEqual(3, self.calls)

    def test_an_unpublished_month_isnt_retried(self):
        missing = urllib.error.HTTPError("url", 404, "Not Found", {}, None)
        with self.assertRaises(urllib.error.HTTPError):
            self.fetch([missing])
        self.assertEqual(1, self.calls)
        self.assertEqual([], self.pauses)

    def test_remaining_counts_a_partial_download(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(1000, lichess_download.remaining("2026-01", directory, 1000))
            with open(lichess_download.dump_path("2026-01", directory) + ".part", "wb") as file:
                file.write(b"x" * 300)
            self.assertEqual(700, lichess_download.remaining("2026-01", directory, 1000))


if __name__ == "__main__":
    unittest.main()
