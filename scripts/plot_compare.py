#!/usr/bin/env python3

import csv
import argparse
from collections import defaultdict
import matplotlib.pyplot as plt


def load_data(path):
    data = defaultdict(list)

    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            impl = row["implementation"]
            threads = int(row["threads"])
            avg = float(row["avg_total_ms"])
            data[impl].append((threads, avg))

    for impl in data:
        data[impl].sort(key=lambda x: x[0])

    return data


def compute_speedup(data):
    speedup = {}

    for impl, points in data.items():
        base_time = None
        for t, time in points:
            if t == 1:
                base_time = time
                break

        if base_time is None:
            raise ValueError(f"No t=1 for {impl}")

        speedup[impl] = [(t, base_time / time) for t, time in points]

    return speedup


def compute_efficiency(speedup):
    efficiency = {}

    for impl, points in speedup.items():
        efficiency[impl] = [(t, s / t) for t, s in points]

    return efficiency


def plot(data, ylabel, title, filename):
    plt.figure(figsize=(8, 5))

    for impl, points in data.items():
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        plt.plot(xs, ys, marker="o", label=impl)

    plt.xlabel("Indexing threads")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.xticks(sorted({x for points in data.values() for x, _ in points}))
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(filename, dpi=150)

    print(f"Saved: {filename}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="compare_summary.csv")
    args = parser.parse_args()

    data = load_data(args.input)

    plot(
        data,
        ylabel="Average total time (ms)",
        title="Time vs threads",
        filename="time.png"
    )

    speedup = compute_speedup(data)
    plot(
        speedup,
        ylabel="Speedup (T1 / Tp)",
        title="Speedup vs threads",
        filename="speedup.png"
    )

    efficiency = compute_efficiency(speedup)
    plot(
        efficiency,
        ylabel="Efficiency (S(p) / p)",
        title="Efficiency vs threads",
        filename="efficiency.png"
    )


if __name__ == "__main__":
    main()
