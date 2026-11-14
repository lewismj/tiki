"""A small line chart in braille characters, as a rich Text. Used by tui.py for the training graphs.

    chart([Series("train", "#44b4ff", xs, ys), ...], width, height) -> Text

Each character cell holds 2 x 4 dots, so a chart 60 cells wide plots 120 points across. The left
edge has the y axis and its labels, the bottom the x axis with whole-number ticks (epochs), and
the first row the legend.
"""

from dataclasses import dataclass

from rich.text import Text

AXIS = "#859fd5"
DOTS = ((0x01, 0x02, 0x04, 0x40), (0x08, 0x10, 0x20, 0x80))     # [column][row] of a braille cell.
STEPS = (1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000)


@dataclass
class Series:
    label: str
    style: str
    xs: list
    ys: list


def tick_step(span, most):
    """The smallest step from STEPS that puts at most 'most' ticks on a span of whole numbers."""
    for step in STEPS:
        if span / step + 1 <= most:
            return step
    return STEPS[-1]


def chart(series, width, height, y_format="{:.4f}", x_range=None, message="No data yet"):
    """The series drawn in 'width' x 'height' cells. 'x_range' (first, last) fixes the x axis, e.g. to
    all of a run's epochs; otherwise it spans the data."""
    points = [(x, y) for s in series for x, y in zip(s.xs, s.ys)]
    if not points or width < 16 or height < 5:
        return Text(message if not points else "", style=AXIS)

    x_low, x_high = x_range or (min(x for x, _ in points), max(x for x, _ in points))
    x_low, x_high = min(x_low, min(x for x, _ in points)), max(x_high, max(x for x, _ in points))
    if x_high == x_low:
        x_high = x_low + 1
    y_low, y_high = min(y for _, y in points), max(y for _, y in points)
    margin = (y_high - y_low) * 0.06 or abs(y_high) * 0.05 or 1.0
    y_low, y_high = y_low - margin, y_high + margin

    rows = height - 3                                           # Less the legend, the axis and its labels.
    labels = {row: y_format.format(y_high - (y_high - y_low) * (row + 0.5) / rows)
              for row in range(0, rows, max(2, rows // 5))}
    gutter = max(len(label) for label in labels.values()) + 1
    columns = width - gutter - 1
    if columns < 8:
        return Text("")
    dots_wide, dots_high = columns * 2, rows * 4

    def position(x, y):
        return (round((x - x_low) / (x_high - x_low) * (dots_wide - 1)),
                round((y_high - y) / (y_high - y_low) * (dots_high - 1)))

    bits = [[0] * columns for _ in range(rows)]
    styles = [[None] * columns for _ in range(rows)]

    def dot(px, py, style):
        if 0 <= px < dots_wide and 0 <= py < dots_high:
            bits[py // 4][px // 2] |= DOTS[px % 2][py % 4]
            styles[py // 4][px // 2] = style

    for s in series:
        placed = [position(x, y) for x, y in zip(s.xs, s.ys)]
        for (x0, y0), (x1, y1) in zip(placed, placed[1:] or placed):
            steps = max(abs(x1 - x0), abs(y1 - y0), 1)
            for step in range(steps + 1):
                dot(round(x0 + (x1 - x0) * step / steps), round(y0 + (y1 - y0) * step / steps), s.style)

    text = Text(no_wrap=True)
    text.append(" " * (gutter + 1))
    for s in series:
        text.append("━━ ", style=s.style)
        text.append(s.label + "   ", style="#c5c7d2")
    text.append("\n")
    for row in range(rows):
        text.append(labels.get(row, "").rjust(gutter) + ("┤" if row in labels else "│"), style=AXIS)
        for column in range(columns):
            if bits[row][column]:
                text.append(chr(0x2800 + bits[row][column]), style=styles[row][column])
            else:
                text.append(" ")
        text.append("\n")

    # The x axis: whole-number ticks, each label centred under its tick where there's room.
    step = tick_step(x_high - x_low, max(2, columns // 6))
    axis, names = ["─"] * columns, [" "] * columns
    tick = -(-int(x_low) // step) * step if x_low > 0 else int(x_low)
    while tick <= x_high:
        if tick >= x_low:
            column = position(tick, y_low)[0] // 2
            name = str(tick)
            start = min(max(column - len(name) // 2, 0), columns - len(name))
            if all(c == " " for c in names[max(start - 1, 0):start + len(name) + 1]):
                axis[column] = "┬"
                names[start:start + len(name)] = name
        tick += step
    text.append(" " * gutter + "└" + "".join(axis) + "\n", style=AXIS)
    text.append(" " * (gutter + 1) + "".join(names), style=AXIS)
    return text
