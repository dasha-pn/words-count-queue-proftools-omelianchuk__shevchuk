#!/usr/bin/env python3

import sys
import subprocess
import statistics
import argparse
import csv
import os


def read_results_to_dict(filepath):
    results = {}
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and ":" in line:
                    word, count = line.rsplit(":", 1)
                    results[word.strip()] = int(count.strip())
    except FileNotFoundError:
        print(f"Error: Output file {filepath} not found.")
        sys.exit(1)
    return results


def parse_config_outputs(config_path):
    out_by_a = "res_a.txt"
    out_by_n = "res_n.txt"

    with open(config_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if line.startswith("out_by_a"):
                out_by_a = line.split("=", 1)[1].strip("\"' ")
            elif line.startswith("out_by_n"):
                out_by_n = line.split("=", 1)[1].strip("\"' ")

    return out_by_a, out_by_n


def parse_threads(config_path):
    with open(config_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if line.startswith("indexing_threads"):
                return int(line.split("=", 1)[1].strip())

    print(f"Error: indexing_threads not found in {config_path}")
    sys.exit(1)


def parse_total(stdout):
    for line in stdout.strip().split("\n"):
        line = line.strip()
        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        if key.strip() == "Total":
            try:
                return float(value.strip())
            except ValueError:
                pass

    return None


def run_single(exe, config, out_by_a, out_by_n, run_idx, label):
    print(f"[{label}] config={config} run={run_idx}")

    result = subprocess.run([exe, config], capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Error while running {label}: exit code {result.returncode}")
        print("STDOUT:")
        print(result.stdout)
        print("STDERR:")
        print(result.stderr)
        sys.exit(result.returncode)

    total = parse_total(result.stdout)
    if total is None:
        print(f"Error: Missing Total timer output for {label}.")
        print("STDOUT:")
        print(result.stdout)
        print("STDERR:")
        print(result.stderr)
        sys.exit(1)

    results_a = read_results_to_dict(out_by_a)
    results_n = read_results_to_dict(out_by_n)

    return total, results_a, results_n


def summarize(times):
    return {
        "min": min(times),
        "avg": statistics.mean(times),
        "stdev": statistics.stdev(times) if len(times) > 1 else 0.0,
    }


def run_impl_for_config(exe, label, config, runs, raw_csv_rows):
    out_by_a, out_by_n = parse_config_outputs(config)
    threads = parse_threads(config)

    totals = []
    baseline_a = None
    baseline_n = None

    for i in range(runs):
        total, current_a, current_n = run_single(
            exe, config, out_by_a, out_by_n, i + 1, label
        )

        if baseline_a is None:
            baseline_a = current_a
            baseline_n = current_n
        else:
            if baseline_a != current_a or baseline_n != current_n:
                print(f"Error: Result mismatch across runs for {label} with threads={threads}.")
                sys.exit(1)

        totals.append(total)

        raw_csv_rows.append({
            "implementation": label,
            "config": os.path.basename(config),
            "threads": threads,
            "run": i + 1,
            "total_ms": total,
        })

    stats = summarize(totals)

    print(f"\n[{label}] threads={threads}")
    print(f"Min Total: {stats['min']:.0f} ms")
    print(f"Avg Total: {stats['avg']:.0f} ms")
    print(f"StdDev: {stats['stdev']:.2f} ms\n")

    return stats, baseline_a, baseline_n, threads


def write_csv(csv_path, fieldnames, rows):
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(
        description="Run basis_par and proftools on multiple configs and save results to CSV"
    )
    parser.add_argument("--exe1", required=True, help="Path to basis_par executable")
    parser.add_argument("--exe2", required=True, help="Path to proftools executable")
    parser.add_argument("--label1", default="basis_par")
    parser.add_argument("--label2", default="proftools")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--csv_file", default="compare_runs.csv")
    parser.add_argument("--summary_csv_file", default="compare_summary.csv")
    parser.add_argument("configs", nargs="+", help="List of config files")

    args = parser.parse_args()

    raw_csv_rows = []
    summary_csv_rows = []

    for config in args.configs:
        print("\n==============================")
        print(f"Processing config: {config}")
        print("==============================\n")

        stats1, res1_a, res1_n, threads1 = run_impl_for_config(
            args.exe1, args.label1, config, args.runs, raw_csv_rows
        )

        stats2, res2_a, res2_n, threads2 = run_impl_for_config(
            args.exe2, args.label2, config, args.runs, raw_csv_rows
        )

        if threads1 != threads2:
            print("Error: thread counts parsed for the two implementations differ.")
            sys.exit(1)

        if res1_a != res2_a or res1_n != res2_n:
            print(f"Error: Final results differ for config {config}.")
            sys.exit(1)

        speedup = stats1["avg"] / stats2["avg"] if stats2["avg"] != 0 else 0.0

        print("Results match between implementations.")
        print(f"Speedup ({args.label1} / {args.label2}) for threads={threads1}: {speedup:.3f}x\n")

        summary_csv_rows.append({
            "config": os.path.basename(config),
            "threads": threads1,
            "implementation": args.label1,
            "min_total_ms": stats1["min"],
            "avg_total_ms": stats1["avg"],
            "stdev_total_ms": stats1["stdev"],
        })

        summary_csv_rows.append({
            "config": os.path.basename(config),
            "threads": threads1,
            "implementation": args.label2,
            "min_total_ms": stats2["min"],
            "avg_total_ms": stats2["avg"],
            "stdev_total_ms": stats2["stdev"],
        })

    write_csv(
        args.csv_file,
        ["implementation", "config", "threads", "run", "total_ms"],
        raw_csv_rows
    )

    write_csv(
        args.summary_csv_file,
        ["config", "threads", "implementation", "min_total_ms", "avg_total_ms", "stdev_total_ms"],
        summary_csv_rows
    )

    print(f"Raw CSV saved to {args.csv_file}")
    print(f"Summary CSV saved to {args.summary_csv_file}")


if __name__ == "__main__":
    main()
