"""Trains tiki's network on 'tiki datagen' or lichess.py output and exports it for the engine.

    python3 train.py --data data/*.txt --epochs 20 --out nets/

writes nets/tiki-<epoch>.pt (checkpoint) and nets/tiki-<epoch>.nnue (engine format) after each epoch,
and appends progress to nets/train.log (handy for 'tail -f' from another terminal).

--data takes binary (.bin) files, or text (.txt) files, which are converted to a .bin beside them
the first time (and again if the .txt changes).
"""

import argparse
import os
import time

import numpy as np
import torch

from rich.console import Console
from rich.progress import (
    BarColumn,
    DownloadColumn,
    MofNCompleteColumn,
    Progress,
    TextColumn,
    TimeElapsedColumn,
    TimeRemainingColumn,
    TransferSpeedColumn,
)

from tiki_nnue import binary
from tiki_nnue.data import batch_tensors, validation_split
from tiki_nnue.export import export
from tiki_nnue.model import HIDDEN, TikiNNUE, loss_fn

console = Console()
UPDATE_EVERY = 16       # Batches between progress updates (each waits for the GPU to catch up).


class Log:
    """Appends timestamped lines to <out>/train.log, flushed as they're written."""

    def __init__(self, path):
        self.file = open(path, "a", buffering=1)

    def __call__(self, message):
        self.file.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}\n")


class Batches:
    """Batches of 'records' (binary.RECORD) at 'indices', as tensors on 'device' for the model and
    loss_fn. The features are computed on the device."""

    def __init__(self, records, indices, batch_size, device, rng=None):
        """Shuffled each pass if an 'rng' (numpy Generator) is given."""
        self.records, self.indices, self.batch_size, self.device, self.rng = records, indices, batch_size, device, rng

    def __len__(self):
        return -(-len(self.indices) // self.batch_size)

    def __iter__(self):
        order = self.rng.permutation(self.indices) if self.rng is not None else self.indices
        for start in range(0, len(order), self.batch_size):
            yield batch_tensors(self.records[order[start:start + self.batch_size]], self.device)


def add_schedule_arguments(parser):
    """The learning rate options, shared with train_streaming.py."""
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--lr-schedule", choices=["step", "cosine"], default="step",
                        help="step: multiply the learning rate by 0.1 every --lr-drop epochs; "
                             "cosine: lower it smoothly from --lr to --lr-min over the epochs")
    parser.add_argument("--lr-drop", type=int, default=10, help="multiply the learning rate by 0.1 every N epochs")
    parser.add_argument("--lr-min", type=float, default=1e-5, help="cosine: the learning rate of the last epoch")


def make_scheduler(optimiser, args):
    """The learning rate schedule, stepped after each epoch. Cosine reaches --lr-min in the last epoch."""
    if args.lr_schedule == "cosine":
        return torch.optim.lr_scheduler.CosineAnnealingLR(optimiser, T_max=max(args.epochs - 1, 1),
                                                          eta_min=args.lr_min)
    return torch.optim.lr_scheduler.StepLR(optimiser, step_size=args.lr_drop, gamma=0.1)


def add_model_arguments(parser):
    """The network options, shared with train_streaming.py."""
    parser.add_argument("--hidden", type=int, default=HIDDEN,
                        help="feature transformer width per side (the engine must be built with the same NNUE_HIDDEN)")
    parser.add_argument("--factoriser", action="store_true",
                        help="train shared rows for each piece and square across the king buckets as well "
                             "(added into the buckets' rows on export; the engine is unchanged)")


def load_model(model, state, args, parser):
    """Restores a checkpoint's weights, which must have the same --factoriser setting, and be of this
    architecture (pairwise inputs and the hidden layer, format version 5)."""
    if "l1.weight" not in state or "l1_shared.weight" not in state:
        parser.error("the checkpoint is from before the hidden layer (network format version 2 or 3), "
                     "it can't be resumed with this trainer")
    if state["l1.weight"].shape[1] != model.hidden:
        parser.error(f"the checkpoint's hidden layer has {state['l1.weight'].shape[1]} inputs, this trainer's "
                     f"{model.hidden} (pairwise inputs; a network of format version 4 or another --hidden), "
                     "it can't be resumed with these settings")
    if ("factor.weight" in state) != args.factoriser:
        parser.error(f"the checkpoint was trained {'with' if 'factor.weight' in state else 'without'} --factoriser")
    model.load_state_dict(state)


def load_scheduler(scheduler, state, args, parser):
    """Restores a checkpoint's schedule, which must be the same kind as --lr-schedule. A resumed
    cosine schedule keeps the checkpoint's length, a resumed step schedule its --lr-drop."""
    if ("T_max" in state) != (args.lr_schedule == "cosine"):
        parser.error(f"the checkpoint was trained with the other --lr-schedule, not {args.lr_schedule}")
    scheduler.load_state_dict(state)


def progress_bar(description):
    """A transient bar for byte counts (converting and loading data)."""
    return Progress(TextColumn(description), BarColumn(), DownloadColumn(), TransferSpeedColumn(),
                    TimeRemainingColumn(), console=console, transient=True)


def binary_files(paths, workers, log):
    """The .bin files for 'paths', converting any .txt (to a .bin beside it) that isn't up to date."""
    result = []
    for path in paths:
        if path.endswith(".bin"):
            out = path
        else:
            out = os.path.splitext(path)[0] + ".bin"
            if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(path):
                with progress_bar(f"Converting {os.path.basename(path)}") as progress:
                    task = progress.add_task("", total=os.path.getsize(path))
                    start = time.time()
                    positions = binary.convert_text(path, out, workers,
                                                    lambda size: progress.update(task, advance=size))
                message = f"converted {path}: {positions:,} positions, {time.time() - start:.0f}s"
                console.print(message)
                log(message)
        if out not in result:
            result.append(out)
    return result


def add_loss_arguments(parser):
    """The loss options, shared by train.py and train_streaming.py."""
    parser.add_argument("--wdl", type=float, default=0.3, help="weight of the game result vs the search score")
    parser.add_argument("--loss-power", type=float, default=2.0,
                        help="exponent of the error (2: MSE; Stockfish uses 2.5); must be >= 1")


def check_loss_arguments(parser, args):
    if args.loss_power < 1.0:
        parser.error("--loss-power must be >= 1")


def run_epoch(model, batches, device, wdl, optimiser=None, progress=None, task=None, log=None, label="",
              power=2.0):
    """One pass over 'batches', training if an optimiser is given. Returns (mean loss, mean MSE); they're
    the same when power is 2, and the MSE lets runs with different powers be compared.

    Advances 'task' on 'progress', and writes to 'log' every 10% of the pass."""
    model.train(optimiser is not None)
    total = torch.zeros((), device=device)
    total_mse = torch.zeros((), device=device)
    count, positions = 0, 0
    start = time.time()
    log_every = max(1, len(batches) // 10)

    def report(advance):
        mean = total.item() / count
        rate = positions / max(time.time() - start, 1e-9)
        if progress is not None:
            progress.update(task, advance=advance, loss=f"{mean:.6f}", rate=f"{rate:,.0f}")
        return mean, rate

    with torch.set_grad_enabled(optimiser is not None):
        for stm_indices, stm_offsets, nstm_indices, nstm_offsets, bucket, score, result in batches:
            output = model(stm_indices, stm_offsets, nstm_indices, nstm_offsets, bucket)
            loss = loss_fn(output, score, result, wdl, power)
            if optimiser is not None:
                optimiser.zero_grad(set_to_none=True)
                loss.backward()
                optimiser.step()
                model.clip_weights()
            total += loss.detach()
            if power != 2.0:
                with torch.no_grad():
                    total_mse += loss_fn(output, score, result, wdl)
            count += 1
            positions += len(score)
            if count % UPDATE_EVERY == 0:
                report(UPDATE_EVERY)
            if log is not None and count % log_every == 0:
                mean, rate = report(0)
                log(f"{label}: batch {count}/{len(batches)}, loss {mean:.6f}, {rate:,.0f} positions/s")
    if count == 0:
        return 0.0, 0.0
    mean = report(count % UPDATE_EVERY)[0]
    return mean, (mean if power == 2.0 else total_mse.item() / count)


def epoch_result(epoch, train_loss, validation, power, lr, seconds, name):
    """The line logged after each epoch. With a power other than 2 it adds the validation MSE, which
    compares with runs at other powers (the losses themselves don't)."""
    validation_loss, validation_mse = validation
    mse = "" if power == 2.0 else f" (MSE {validation_mse:.6f})"
    return (f"epoch {epoch}: train {train_loss:.6f}, validation {validation_loss:.6f}{mse}, "
            f"lr {lr:g}, {seconds:.0f}s -> {name}.nnue")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", nargs="+", required=True, help="training data files (.bin or .txt)")
    parser.add_argument("--out", default="nets", help="directory for checkpoints and exported networks")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=16384)
    add_model_arguments(parser)
    add_schedule_arguments(parser)
    add_loss_arguments(parser)
    parser.add_argument("--validation", type=float, default=0.02,
                        help="fraction held out for validation, in blocks of consecutive positions (whole games)")
    parser.add_argument("--workers", type=int, default=4, help="processes for converting .txt data to .bin")
    parser.add_argument("--resume", help="checkpoint (.pt) to continue from")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    check_loss_arguments(parser, args)

    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    os.makedirs(args.out, exist_ok=True)
    log = Log(os.path.join(args.out, "train.log"))
    device = args.device
    if device.startswith("cuda"):
        device += f" ({torch.cuda.get_device_name(torch.device(device))})"
    console.print(f"[bold]Tiki · NNUE training[/bold] on {device}")
    log(f"start: {vars(args)}")

    paths = binary_files(args.data, args.workers, log)
    with progress_bar("Loading data") as progress:
        task = progress.add_task("", total=sum(os.path.getsize(path) for path in paths))
        records = binary.load(paths, lambda size: progress.update(task, advance=size))
    # Held out in blocks of consecutive positions, so validation is games the network never sees.
    training, validation = validation_split(len(records), args.validation, rng)
    train_batches = Batches(records, training, args.batch_size, args.device, rng)
    validation_batches = Batches(records, validation, args.batch_size, args.device)
    summary = f"{len(training):,} training positions, {len(validation):,} validation (whole games)"
    console.print(summary)
    log(summary)

    model = TikiNNUE(hidden=args.hidden, factoriser=args.factoriser).to(args.device)
    optimiser = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = make_scheduler(optimiser, args)
    first_epoch = 1
    if args.resume:
        checkpoint = torch.load(args.resume, map_location=args.device)
        load_model(model, checkpoint["model"], args, parser)
        optimiser.load_state_dict(checkpoint["optimiser"])
        load_scheduler(scheduler, checkpoint["scheduler"], args, parser)
        first_epoch = checkpoint["epoch"] + 1
        console.print(f"Resuming from {args.resume} at epoch {first_epoch}")
        log(f"resumed from {args.resume} at epoch {first_epoch}")

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
        training = progress.add_task("Training", total=len(train_batches), loss="-", rate="-")
        validating = progress.add_task("Validation", total=len(validation_batches), loss="-", rate="-")

        for epoch in range(first_epoch, args.epochs + 1):
            start = time.time()
            lr = optimiser.param_groups[0]["lr"]
            progress.reset(training, description=f"Epoch {epoch}", loss="-", rate="-")
            progress.reset(validating, loss="-", rate="-")
            log(f"epoch {epoch}: started, lr {lr:g}")

            train_loss = run_epoch(model, train_batches, args.device, args.wdl, optimiser,
                                   progress, training, log, f"epoch {epoch} train", power=args.loss_power)[0]
            validation = run_epoch(model, validation_batches, args.device, args.wdl,
                                   progress=progress, task=validating, power=args.loss_power)
            scheduler.step()

            name = os.path.join(args.out, f"tiki-{epoch}")
            torch.save({"model": model.state_dict(), "optimiser": optimiser.state_dict(),
                        "scheduler": scheduler.state_dict(), "epoch": epoch}, name + ".pt")
            export(model.cpu(), name + ".nnue")
            model.to(args.device)

            result = epoch_result(epoch, train_loss, validation, args.loss_power, lr, time.time() - start, name)
            progress.console.print(result)
            log(result)
            progress.update(overall, advance=1, loss=f"{validation[0]:.6f}")

    log("finished")


if __name__ == "__main__":
    main()
