"""Trains tiki's network like train.py, but streams the data from disk instead of loading it all, so
memory use stays the same however much data there is (months of Lichess, lc0 data, ...).

    python3 train_streaming.py --data /mnt/f/lichess/data /mnt/f/lc0/data --epochs 20 --out nets/

--data takes .bin or .txt files, or directories of them (.txt files are converted to a .bin beside
them first, as in train.py). Positions are read in chunks of consecutive records in a random order
across all the files, and shuffled in a buffer of --buffer positions (about 68 bytes each), so a
batch mixes many games and files. About two buffers are held in memory: the one being trained on
and the next, read in the background. See tiki_nnue/stream.py.

An epoch is a pass over all the data, or with --epoch-positions N, every N positions (for data too
large to pass over often). Each epoch is followed by validation, a checkpoint and an exported net,
as in train.py, and the learning rate drops every --lr-drop epochs (or with --lr-schedule cosine, falls
smoothly to --lr-min). Checkpoints are the same as train.py's, so either can --resume the other's.
"""

import argparse
import os
import time

import numpy as np
import torch
from rich.progress import BarColumn, MofNCompleteColumn, Progress, TextColumn, TimeElapsedColumn, TimeRemainingColumn

from tiki_nnue.export import export
from tiki_nnue.model import HIDDEN, TikiNNUE
from tiki_nnue.stream import Epoch, StreamData, TrainingStream, ValidationBatches, data_files
from train import Log, add_schedule_arguments, binary_files, console, load_scheduler, make_scheduler, run_epoch


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", nargs="+", required=True, help="training data files (.bin or .txt) or directories")
    parser.add_argument("--out", default="nets", help="directory for checkpoints and exported networks")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--epoch-positions", type=int, default=0,
                        help="positions per epoch (0: a whole pass over the data)")
    parser.add_argument("--batch-size", type=int, default=16384)
    parser.add_argument("--hidden", type=int, default=HIDDEN,
                        help="feature transformer width per side (the engine must be built with the same NNUE_HIDDEN)")
    parser.add_argument("--buffer", type=int, default=8_000_000, help="positions shuffled together (memory: ~68 bytes each, x2)")
    add_schedule_arguments(parser)
    parser.add_argument("--wdl", type=float, default=0.3, help="weight of the game result vs the search score")
    parser.add_argument("--validation", type=float, default=0.02,
                        help="fraction held out for validation, in blocks of consecutive positions (whole games)")
    parser.add_argument("--validation-max", type=int, default=2_000_000,
                        help="positions validated after each epoch, at most (0: all held out)")
    parser.add_argument("--workers", type=int, default=4, help="processes for converting .txt data to .bin")
    parser.add_argument("--resume", help="checkpoint (.pt) to continue from")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    if args.epoch_positions < 0 or args.buffer < 1 or args.validation_max < 0:
        parser.error("--epoch-positions and --validation-max must be >= 0, --buffer >= 1")

    torch.manual_seed(args.seed)
    os.makedirs(args.out, exist_ok=True)
    log = Log(os.path.join(args.out, "train.log"))
    device = args.device
    if device.startswith("cuda"):
        device += f" ({torch.cuda.get_device_name(torch.device(device))})"
    console.print(f"[bold]Tiki · NNUE training (streaming)[/bold] on {device}")
    log(f"start (streaming): {vars(args)}")

    files = data_files(args.data)
    if not files:
        parser.error("no .bin or .txt files in --data")
    paths = binary_files(files, args.workers, log)
    data = StreamData(paths, args.validation, args.seed)
    if data.training_count == 0:
        parser.error("no training positions in --data")
    summary = (f"{len(paths)} file(s): {data.training_count:,} training positions, "
               f"{data.validation_count:,} validation (whole games)")
    console.print(summary)
    log(summary)

    model = TikiNNUE(hidden=args.hidden).to(args.device)
    optimiser = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = make_scheduler(optimiser, args)
    first_epoch = 1
    if args.resume:
        checkpoint = torch.load(args.resume, map_location=args.device)
        model.load_state_dict(checkpoint["model"])
        optimiser.load_state_dict(checkpoint["optimiser"])
        load_scheduler(scheduler, checkpoint["scheduler"], args, parser)
        first_epoch = checkpoint["epoch"] + 1
        console.print(f"Resuming from {args.resume} at epoch {first_epoch}")
        log(f"resumed from {args.resume} at epoch {first_epoch}")

    # A new data order when resuming, rather than repeating the first epochs' order.
    stream = TrainingStream(data, args.batch_size, args.device, args.buffer,
                            np.random.default_rng([args.seed, first_epoch]))
    batches = iter(stream)
    per_epoch = -(-args.epoch_positions // args.batch_size) if args.epoch_positions else stream.passes()
    validation_batches = ValidationBatches(data, args.batch_size, args.device, args.validation_max, args.seed)
    message = (f"{per_epoch:,} batches of {args.batch_size:,} per epoch "
               f"({per_epoch * args.batch_size / data.training_count:.2f} passes), "
               f"validating {validation_batches.count:,} positions")
    console.print(message)
    log(message)

    with Progress(
        TextColumn("{task.description:<12}"),
        BarColumn(),
        MofNCompleteColumn(),
        TextColumn("loss {task.fields[loss]}"),
        TextColumn("{task.fields[rate]} pos/s"),
        TimeElapsedColumn(),
        TimeRemainingColumn(),
        console=console,
    ) as progress:
        overall = progress.add_task("Epochs", total=args.epochs, completed=first_epoch - 1, loss="-", rate="-")
        training = progress.add_task("Training", total=per_epoch, loss="-", rate="-")
        validating = progress.add_task("Validation", total=len(validation_batches), loss="-", rate="-")

        for epoch in range(first_epoch, args.epochs + 1):
            start = time.time()
            lr = optimiser.param_groups[0]["lr"]
            progress.reset(training, description=f"Epoch {epoch}", loss="-", rate="-")
            progress.reset(validating, loss="-", rate="-")
            log(f"epoch {epoch}: started, lr {lr:g}")

            train_loss = run_epoch(model, Epoch(batches, per_epoch), args.device, args.wdl, optimiser,
                                   progress, training, log, f"epoch {epoch} train")
            validation_loss = run_epoch(model, validation_batches, args.device, args.wdl,
                                        progress=progress, task=validating)
            scheduler.step()

            name = os.path.join(args.out, f"tiki-{epoch}")
            torch.save({"model": model.state_dict(), "optimiser": optimiser.state_dict(),
                        "scheduler": scheduler.state_dict(), "epoch": epoch}, name + ".pt")
            export(model.cpu(), name + ".nnue")
            model.to(args.device)

            result = (f"epoch {epoch}: train {train_loss:.6f}, validation {validation_loss:.6f}, "
                      f"lr {lr:g}, {time.time() - start:.0f}s -> {name}.nnue")
            progress.console.print(result)
            log(result)
            progress.update(overall, advance=1, loss=f"{validation_loss:.6f}")

    log("finished")


if __name__ == "__main__":
    main()
