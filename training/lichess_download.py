"""Downloads Lichess monthly dumps without converting them, to convert later with lichess.py.

    python3 lichess_download.py --download-dir /mnt/f/lichess --months 2025-10:2026-09

The dumps keep the names lichess.py looks for, so running lichess.py later with the same
--download-dir converts them without downloading them again, with whatever --sample, --min-elo
etc. you choose then.

Meant to run unattended, e.g. overnight:
- months already downloaded are skipped, and an interrupted download resumes;
- a download that fails (the connection drops, the server errors) is retried after a pause;
- before each month, the dump's size is checked against the free space on the disk, and the run
  stops rather than fill the disk;
- a month that isn't published yet is reported and skipped.
"""

import argparse
import http.client
import os
import shutil
import sys
import time
import urllib.error
import urllib.request

from rich.table import Table

import lichess
from lichess import URL, console

GB = 1024 ** 3


def dump_path(month, directory):
    return os.path.join(directory, f"lichess_db_standard_rated_{month}.pgn.zst")


def remote_size(month):
    """The dump's size in bytes, None if the server doesn't say."""
    request = urllib.request.Request(URL.format(month=month), method="HEAD")
    with urllib.request.urlopen(request, timeout=60) as response:
        length = response.headers.get("Content-Length")
        return int(length) if length is not None else None


def remaining(month, directory, size):
    """Bytes still to download for the month, given the dump's size."""
    partial = dump_path(month, directory) + ".part"
    return size - (os.path.getsize(partial) if os.path.exists(partial) else 0)


def fetch(month, directory, retries, wait, sleep=time.sleep):
    """Downloads the month with lichess.download, retrying after failures. A month that isn't
    published (HTTP 404) isn't retried."""
    for attempt in range(1, retries + 2):
        try:
            return lichess.download(month, directory)
        except urllib.error.HTTPError as error:
            if error.code == 404 or attempt > retries:
                raise
            reason = f"HTTP {error.code}"
        except (urllib.error.URLError, http.client.HTTPException, OSError, RuntimeError) as error:
            if attempt > retries:
                raise
            reason = str(error) or type(error).__name__
        pause = wait * attempt
        console.print(f"[yellow]{month}: {reason}; retry {attempt} of {retries} in {pause}s[/yellow]")
        sleep(pause)


def print_summary(results):
    table = Table(title="Downloads")
    table.add_column("Month")
    table.add_column("Status")
    table.add_column("Size", justify="right")
    for month, status, size in results:
        table.add_row(month, status, f"{size / GB:.1f} GB" if size else "-")
    console.print()
    console.print(table)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--download-dir", required=True, help="where the .pgn.zst dumps go (30+ GB each)")
    parser.add_argument("--months", nargs="+", required=True, help="YYYY-MM, or ranges YYYY-MM:YYYY-MM")
    parser.add_argument("--retries", type=int, default=10, help="retries of a failed download before giving up on it")
    parser.add_argument("--wait", type=int, default=60, help="seconds before the first retry, longer for each after")
    parser.add_argument("--reserve-gb", type=float, default=10, help="free space to leave on the disk, in GB")
    args = parser.parse_args()

    if args.retries < 0:
        parser.error("--retries must be >= 0")
    if args.wait < 0:
        parser.error("--wait must be >= 0")
    try:
        requested = lichess.months(args.months)
    except ValueError as error:
        parser.error(str(error))

    os.makedirs(args.download_dir, exist_ok=True)
    console.print()
    console.print("[bold]Tiki · Lichess downloads[/bold]")
    console.print(f"Months: {', '.join(requested)}")
    console.print()

    results = []
    failed = False
    for month in requested:
        path = dump_path(month, args.download_dir)
        if os.path.exists(path):
            console.print(f"[dim]{month}: already downloaded[/dim]")
            results.append((month, "Already downloaded", os.path.getsize(path)))
            continue
        try:
            size = remote_size(month)
            if size is not None:
                free = shutil.disk_usage(args.download_dir).free - args.reserve_gb * GB
                needed = remaining(month, args.download_dir, size)
                if needed > free:
                    console.print(f"[red]{month}: needs {needed / GB:.1f} GB, "
                                  f"{max(free, 0) / GB:.1f} GB free (after --reserve-gb); stopping[/red]")
                    results.append((month, "No space", size))
                    failed = True
                    break
            console.print(f"[bold cyan]{month}[/bold cyan]")
            path = fetch(month, args.download_dir, args.retries, args.wait)
            results.append((month, "Downloaded", os.path.getsize(path)))
        except KeyboardInterrupt:
            console.print("\n[yellow]Interrupted; the partial download resumes next time.[/yellow]")
            results.append((month, "Interrupted", None))
            print_summary(results)
            return 130
        except urllib.error.HTTPError as error:
            status = "Not published" if error.code == 404 else f"Failed (HTTP {error.code})"
            console.print(f"[red]{month}: {status}[/red]")
            results.append((month, status, None))
            failed = True
        except Exception as error:
            console.print(f"[red]{month}: failed: {error}[/red]")
            results.append((month, "Failed", None))
            failed = True

    print_summary(results)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
