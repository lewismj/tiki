"""Trains tiki's network on 'tiki_app datagen' output and exports it for the engine.

    python3 train.py --data data/*.txt --epochs 20 --out nets/

writes nets/tiki-<epoch>.pt (checkpoint) and nets/tiki-<epoch>.nnue (engine format) after each epoch.
"""

import argparse
import os
import time

import torch
from torch.utils.data import DataLoader, random_split

from tiki_nnue.data import TextDataset, collate
from tiki_nnue.export import export
from tiki_nnue.model import TikiNNUE, loss_fn


def run_epoch(model, loader, device, wdl, optimiser=None):
    """One pass over 'loader', training if an optimiser is given. Returns the mean loss."""
    model.train(optimiser is not None)
    total, batches = 0.0, 0
    with torch.set_grad_enabled(optimiser is not None):
        for stm_indices, stm_offsets, nstm_indices, nstm_offsets, score, result in loader:
            output = model(stm_indices.to(device), stm_offsets.to(device),
                           nstm_indices.to(device), nstm_offsets.to(device))
            loss = loss_fn(output, score.to(device), result.to(device), wdl)
            if optimiser is not None:
                optimiser.zero_grad(set_to_none=True)
                loss.backward()
                optimiser.step()
                model.clip_weights()
            total += loss.item()
            batches += 1
    return total / max(batches, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", nargs="+", required=True, help="datagen output files")
    parser.add_argument("--out", default="nets", help="directory for checkpoints and exported networks")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=16384)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--lr-drop", type=int, default=10, help="multiply the learning rate by 0.1 every N epochs")
    parser.add_argument("--wdl", type=float, default=0.3, help="weight of the game result vs the search score")
    parser.add_argument("--validation", type=float, default=0.02, help="fraction held out for validation")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--resume", help="checkpoint (.pt) to continue from")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    os.makedirs(args.out, exist_ok=True)

    dataset = TextDataset(args.data)
    held_out = max(1, int(len(dataset) * args.validation))
    train_set, validation_set = random_split(dataset, [len(dataset) - held_out, held_out])
    print(f"{len(train_set)} training positions, {len(validation_set)} validation")

    loader_args = dict(batch_size=args.batch_size, collate_fn=collate, num_workers=args.workers,
                       persistent_workers=args.workers > 0)
    train_loader = DataLoader(train_set, shuffle=True, **loader_args)
    validation_loader = DataLoader(validation_set, shuffle=False, **loader_args)

    model = TikiNNUE().to(args.device)
    optimiser = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = torch.optim.lr_scheduler.StepLR(optimiser, step_size=args.lr_drop, gamma=0.1)
    first_epoch = 1
    if args.resume:
        checkpoint = torch.load(args.resume, map_location=args.device)
        model.load_state_dict(checkpoint["model"])
        optimiser.load_state_dict(checkpoint["optimiser"])
        scheduler.load_state_dict(checkpoint["scheduler"])
        first_epoch = checkpoint["epoch"] + 1

    for epoch in range(first_epoch, args.epochs + 1):
        start = time.time()
        train_loss = run_epoch(model, train_loader, args.device, args.wdl, optimiser)
        validation_loss = run_epoch(model, validation_loader, args.device, args.wdl)
        scheduler.step()

        name = os.path.join(args.out, f"tiki-{epoch}")
        torch.save({"model": model.state_dict(), "optimiser": optimiser.state_dict(),
                    "scheduler": scheduler.state_dict(), "epoch": epoch}, name + ".pt")
        export(model.cpu(), name + ".nnue")
        model.to(args.device)
        print(f"epoch {epoch}: train {train_loss:.6f}, validation {validation_loss:.6f}, "
              f"{time.time() - start:.0f}s -> {name}.nnue")


if __name__ == "__main__":
    main()
