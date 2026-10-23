#!/usr/bin/env python3
"""Draw the benchmark charts in this directory from results.csv.

    python3 docs/benchmarks/plot.py

Each row of results.csv is one batch of 5 to 7 timed runs of one algorithm at one
thread count, on an Apple M5 Max with 6 Super and 12 Performance cores, in the
format of `ptarjan_bench --csv` with three columns added in front. The current
code was swept three times, minutes apart (the pass column), and the
dissertation-era engine once. A batch counted only if all other processes together
used under one core before and after it (other_cpu_before and other_cpu_after, in
percent of a core). The charts use the fastest of the medians, as the README's
tables do. Each chart is written twice, for GitHub's light and dark themes.

The script needs only the Python standard library.
"""

import csv
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent

THEMES = {
    "light": {
        "surface": "#ffffff",
        "text": "#0b0b0b",
        "secondary": "#52514e",
        "muted": "#898781",
        "grid": "#e1e0d9",
        "axis": "#c3c2b7",
        "parallel": "#2a78d6",
        "hybrid": "#eb6834",
        "dissertation": "#898781",
    },
    "dark": {
        "surface": "#0d1117",
        "text": "#f0efec",
        "secondary": "#c3c2b7",
        "muted": "#898781",
        "grid": "#2c2c2a",
        "axis": "#383835",
        "parallel": "#3987e5",
        "hybrid": "#d95926",
        "dissertation": "#898781",
    },
}

FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"

# (family, vertices), title, and what its components look like
GRAPHS = {
    "clusters-1M": (("clusters", 1_000_000), "clusters, 1M vertices", "15,625 components of 64"),
    "clusters-10M": (("clusters", 10_000_000), "clusters, 10M vertices", "10,000 components of 1,000"),
    "random": (("random", 1_000_000), "random, 1M vertices", "one component of 960,670"),
    "dag": (("dag", 1_000_000), "dag, 1M vertices", "every component a single vertex"),
}

CORES = 18


def load_times():
    """Return the fastest median per (family, vertices, algorithm, threads), in ms."""
    times = {}
    with open(HERE / "results.csv", newline="") as f:
        for row in csv.DictReader(f):
            key = (row["family"], int(row["vertices"]), row["algorithm"], int(row["threads"]))
            times[key] = min(times.get(key, math.inf), float(row["median_ms"]))
    return times


def series(times, graph, algorithm, max_threads=math.inf):
    """Return [(threads, ms)] for one algorithm on one graph, by thread count."""
    family, vertices = GRAPHS[graph][0]
    return sorted((t, ms) for (f, v, a, t), ms in times.items()
                  if (f, v, a) == (family, vertices, algorithm) and t <= max_threads)


def text_width(text, size):
    """Estimate the width of text in the system sans; SVG cannot measure it here."""
    return 0.52 * size * len(text)


def speedup_label(value):
    return f"{value:.2f}×" if value < 1 else f"{value:.1f}×"


def ms_label(value):
    return f"{value:.1f} ms" if value < 100 else f"{value:,.0f} ms"


class Svg:
    def __init__(self, width, height, theme, title, description):
        self.width, self.height, self.theme = width, height, theme
        self.parts = [f"<title>{title}</title>", f"<desc>{description}</desc>",
                      self.element("rect", width=width, height=height, fill=theme["surface"])]

    @staticmethod
    def element(tag, content=None, **attrs):
        attributes = " ".join(f'{name.rstrip("_").replace("_", "-")}="{value}"'
                              for name, value in attrs.items() if value is not None)
        return f"<{tag} {attributes}/>" if content is None else f"<{tag} {attributes}>{content}</{tag}>"

    def add(self, tag, content=None, **attrs):
        self.parts.append(self.element(tag, content, **attrs))

    def text(self, x, y, content, size=12, color=None, anchor="start", weight=None):
        self.add("text", content, x=f"{x:.1f}", y=f"{y:.1f}", font_size=size,
                 fill=color or self.theme["text"], text_anchor=anchor, font_weight=weight)

    def line(self, x1, y1, x2, y2, color, width=1, dash=None):
        self.add("line", x1=f"{x1:.1f}", y1=f"{y1:.1f}", x2=f"{x2:.1f}", y2=f"{y2:.1f}",
                 stroke=color, stroke_width=width, stroke_dasharray=dash)

    def heading(self, title, subtitle):
        self.text(0, 20, title, size=16, weight=600)
        self.text(0, 42, subtitle, size=13, color=self.theme["secondary"])

    def legend(self, y, items):
        """Draw a row of legend keys; each item is (label, color, dash)."""
        x = 0
        for label, color, dash in items:
            self.line(x, y - 4, x + 20, y - 4, color, width=2, dash=dash)
            self.text(x + 26, y, label, color=self.theme["secondary"])
            x += 26 + text_width(label, 12) + 22

    def render(self):
        body = "\n  ".join(self.parts)
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" '
                f'height="{self.height}" viewBox="0 0 {self.width} {self.height}" '
                f'font-family="{FONT}">\n  {body}\n</svg>\n')


class Panel:
    """One small chart: threads on a log2 x axis, a linear or log10 y axis."""

    def __init__(self, svg, left, top, width, height, threads, ydomain, ylog=False):
        self.svg, self.theme = svg, svg.theme
        self.left, self.top, self.width, self.height = left, top, width, height
        self.xmin, self.xmax = threads
        self.ymin, self.ymax = ydomain
        self.ylog = ylog
        self.obstacles = []  # points the labels must stay clear of
        self.labels = []

    def px(self, threads):
        span = math.log2(self.xmax) - math.log2(self.xmin)
        return self.left + self.width * (math.log2(threads) - math.log2(self.xmin)) / span

    def py(self, value):
        scale = math.log10 if self.ylog else (lambda v: v)
        span = scale(self.ymax) - scale(self.ymin)
        return self.top + self.height * (1 - (scale(value) - scale(self.ymin)) / span)

    def title(self, name, description=None):
        detail = (None if description is None else self.svg.element(
            "tspan", description, dx=8, font_size=12, font_weight=400, fill=self.theme["secondary"]))
        self.svg.text(self.left, self.top - 16, name + (detail or ""), size=13, weight=600)

    def axes(self, yticks, ytick_label, xticks):
        bottom = self.top + self.height
        for value in yticks:
            y = self.py(value)
            color = self.theme["axis"] if math.isclose(y, bottom) else self.theme["grid"]
            self.svg.line(self.left, y, self.left + self.width, y, color)
            self.svg.text(self.left - 8, y + 4, ytick_label(value), size=11,
                          color=self.theme["secondary"], anchor="end")
        if not any(math.isclose(self.py(v), bottom) for v in yticks):
            self.svg.line(self.left, bottom, self.left + self.width, bottom, self.theme["axis"])
        for threads in xticks:
            self.svg.text(self.px(threads), bottom + 16, str(threads), size=11,
                          color=self.theme["secondary"], anchor="middle")
        self.svg.text(self.left + self.width / 2, bottom + 32, "threads", size=11,
                      color=self.theme["muted"], anchor="middle")

    def cores(self):
        x = self.px(CORES)
        self.svg.line(x, self.top, x, self.top + self.height, self.theme["muted"], dash="2 3")
        self.obstacles += [(x, self.top + i) for i in range(0, int(self.height) + 1, 2)]
        self.svg.text(x + 4, self.top + 10, f"{CORES} cores", size=11, color=self.theme["muted"])
        self.labels.append((x + 2, self.top, x + 6 + text_width(f"{CORES} cores", 11), self.top + 13))

    def reference(self, value, color):
        """A dashed horizontal line across the panel."""
        y = self.py(value)
        self.svg.line(self.left, y, self.left + self.width, y, color, width=1.25, dash="5 4")
        self.obstacles += [(self.left + i, y) for i in range(0, int(self.width) + 1, 2)]
        return y

    def plot(self, points, color, width=2):
        coords = [(self.px(t), self.py(v)) for t, v in points]
        path = " ".join(f"{'M' if i == 0 else 'L'}{x:.1f},{y:.1f}" for i, (x, y) in enumerate(coords))
        self.svg.add("path", d=path, fill="none", stroke=color, stroke_width=width,
                     stroke_linecap="round", stroke_linejoin="round")
        for (x1, y1), (x2, y2) in zip(coords, coords[1:]):
            steps = max(1, int(math.hypot(x2 - x1, y2 - y1) / 2))
            self.obstacles += [(x1 + (x2 - x1) * s / steps, y1 + (y2 - y1) * s / steps)
                               for s in range(steps + 1)]

    def dot(self, threads, value, color):
        self.svg.add("circle", cx=f"{self.px(threads):.1f}", cy=f"{self.py(value):.1f}", r=4,
                     fill=color, stroke=self.theme["surface"], stroke_width=2)

    def label(self, x, y, content, offsets, anchor="middle"):
        """Write content at the first offset from (x, y) whose box is clear of the lines,
        the other labels and the panel's edges."""
        width = text_width(content, 12)
        shift = {"start": 0, "middle": width / 2, "end": width}[anchor]
        boxes = []
        for dx, dy in offsets:
            box = (x + dx - shift - 2, y + dy - 11, x + dx - shift + width + 2, y + dy + 3)
            boxes.append(box)
            inside = (box[0] >= self.left - 4 and box[2] <= self.left + self.width + 4
                      and box[1] >= self.top - 2 and box[3] <= self.top + self.height + 2)
            crosses = any(box[0] <= px <= box[2] and box[1] <= py <= box[3] for px, py in self.obstacles)
            overlaps = any(box[0] < b[2] and b[0] < box[2] and box[1] < b[3] and b[1] < box[3]
                           for b in self.labels)
            if inside and not crosses and not overlaps:
                break
        else:
            dx, dy = offsets[0]
            box = boxes[0]
        self.labels.append(box)
        self.svg.text(x + dx, y + dy, content, anchor=anchor)


def speedup_chart(times, theme):
    """Sequential time over parallel time for both algorithms, one panel per graph."""
    svg = Svg(880, 636, theme, "Speed-up over sequential Tarjan",
              "Sequential time divided by parallel time, for parallel Tarjan and the hybrid "
              "algorithm, at 1 to 36 threads on four graphs.")
    svg.heading("Speed-up over sequential Tarjan",
                "Sequential time divided by parallel time; above the dashed line, faster than "
                "Tarjan's algorithm on one thread.")
    svg.legend(76, [("Parallel Tarjan", theme["parallel"], None), ("Hybrid", theme["hybrid"], None),
                    ("Sequential Tarjan (1×)", theme["secondary"], "5 4")])
    for i, graph in enumerate(["clusters-1M", "clusters-10M", "random", "dag"]):
        column, row = i % 2, i // 2
        panel = Panel(svg, left=column * 450 + 34, top=row * 262 + 132, width=380, height=170,
                      threads=(1, 36), ydomain=(0, 4.5))
        panel.title(*GRAPHS[graph][1:])
        panel.axes([0, 1, 2, 3, 4], lambda v: f"{v}×", [1, 2, 4, 8, 16, 36])
        panel.cores()
        panel.reference(1, theme["secondary"])
        sequential = series(times, graph, "sequential")[0][1]
        at16 = {}
        for algorithm in ["parallel", "hybrid"]:
            points = [(t, sequential / ms) for t, ms in series(times, graph, algorithm)]
            panel.plot(points, theme[algorithm])
            at16[algorithm] = dict(points)[16]
        for algorithm in ["parallel", "hybrid"]:
            value = at16[algorithm]
            other = at16["hybrid" if algorithm == "parallel" else "parallel"]
            above, below = [(0, -11), (0, -8)], [(0, 19), (0, 16)]
            near = above + below if value >= other else below + above
            panel.label(panel.px(16), panel.py(value), speedup_label(value),
                        near + [(-24, 4), (-24, -11), (-24, 19)])
        for algorithm in ["parallel", "hybrid"]:
            panel.dot(16, at16[algorithm], theme[algorithm])
    return svg.render()


def dissertation_chart(times, theme):
    """Run times of the dissertation-era engine beside the current algorithms."""
    svg = Svg(880, 400, theme, "The dissertation-era engine and the current code",
              "Time per run in milliseconds at 1 to 18 threads, on a log scale, for the "
              "dissertation-era engine and the current algorithms on three graphs.")
    svg.heading("The dissertation-era engine and the current code",
                "Time per run on the same graphs, log scale; lower is better.")
    svg.legend(76, [("Dissertation-era parallel", theme["dissertation"], None),
                    ("Dissertation-era sequential", theme["dissertation"], "5 4"),
                    ("Parallel Tarjan", theme["parallel"], None), ("Hybrid", theme["hybrid"], None),
                    ("Sequential Tarjan", theme["secondary"], "5 4")])
    for i, graph in enumerate(["clusters-1M", "random", "dag"]):
        panel = Panel(svg, left=i * 300 + 50, top=130, width=224, height=210,
                      threads=(1, CORES), ydomain=(1, 1000), ylog=True)
        panel.title(GRAPHS[graph][1])
        panel.axes([1, 10, 100, 1000], lambda v: "1 s" if v == 1000 else f"{v} ms",
                   [1, 2, 4, 8, 16])
        references = [(series(times, graph, "dissertation-sequential")[0][1], theme["dissertation"]),
                      (series(times, graph, "sequential")[0][1], theme["secondary"])]
        for value, color in references:
            panel.reference(value, color)
        panel.plot(series(times, graph, "dissertation-parallel"), theme["dissertation"])
        for algorithm in ["parallel", "hybrid"]:
            panel.plot(series(times, graph, algorithm, CORES), theme[algorithm])
        right = panel.left + panel.width
        for value, _ in references:
            panel.label(right, panel.py(value), ms_label(value), [(0, -6), (0, 15), (-60, -6), (-60, 15)],
                        anchor="end")
    return svg.render()


def main():
    times = load_times()
    for name, draw in [("speedup", speedup_chart), ("dissertation", dissertation_chart)]:
        for mode, theme in THEMES.items():
            suffix = "" if mode == "light" else "-dark"
            (HERE / f"{name}{suffix}.svg").write_text(draw(times, theme))


if __name__ == "__main__":
    main()
