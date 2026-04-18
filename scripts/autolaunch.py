#!/usr/bin/env python3

import sys
import subprocess
import statistics
import argparse
import os
import tempfile


def clear_os_cache():
    if os.name == 'nt':
        print("I don't know how to clear cache on windows :(")
    else:
        ret = os.system("sudo sync; sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'")
        if ret != 0:
            print("Failed to flush buffers. Missing privileges?")


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


def replace_threads_in_config(config_path, threads):
    with open(config_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    new_lines = []
    found = False

    for line in lines:
        clean = line.split("#")[0].strip()
        if clean.startswith("indexing_threads"):
            new_lines.append(f"indexing_threads={threads}\n")
            found = True
        else:
            new_lines.append(line)

    if not found:
        new_lines.append(f"\nindexing_threads={threads}\n")

    tmp = tempfile.NamedTemporaryFile(delete=False, mode="w", encoding="utf-8")
    tmp.writelines(new_lines)
    tmp.close()

    return tmp.name


def parse_timers(stdout):
    timers = {
        "Total": None,
        "Finding": None,
        "Reading": None,
        "Writing": None,
    }

    for line in stdout.strip().split("\n"):
        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()

        if key in timers:
            try:
                timers[key] = float(value.strip())
            except ValueError:
                pass

    return timers


def summarize(values):
    return {
        "min": min(values),
        "avg": statistics.mean(values),
        "stdev": statistics.stdev(values) if len(values) > 1 else 0.0,
    }


def main():
    parser = argparse.ArgumentParser(description="Autolaunch (Section 5)")

    parser.add_argument("--exe", required=True)
    parser.add_argument("--configs", nargs="+", required=True)
    parser.add_argument("--threads", nargs="+", type=int, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--clear-cache", action="store_true",
                        help="Clear OS cache before each run")

    args = parser.parse_args()

    for config in args.configs:
        out_by_a, out_by_n = parse_config_outputs(config)

        print(f"\n===== CONFIG: {config} =====")

        for threads in args.threads:
            print(f"\n--- indexing_threads = {threads} ---")

            totals = []
            findings = []
            readings = []
            writings = []

            baseline_a = None
            baseline_n = None

            for i in range(args.runs):
                print(f"Run {i + 1}/{args.runs}")

                if args.clear_cache:
                    print("Clearing OS cache...")
                    clear_os_cache()

                tmp_config = replace_threads_in_config(config, threads)

                try:
                    result = subprocess.run(
                        [args.exe, tmp_config],
                        capture_output=True,
                        text=True
                    )
                finally:
                    if os.path.exists(tmp_config):
                        os.unlink(tmp_config)

                if result.returncode != 0:
                    print("Execution failed.")
                    print("STDOUT:\n", result.stdout)
                    print("STDERR:\n", result.stderr)
                    sys.exit(result.returncode)

                timers = parse_timers(result.stdout)

                if timers["Total"] is None:
                    print("Error: Missing Total timer output.")
                    sys.exit(1)

                current_a = read_results_to_dict(out_by_a)
                current_n = read_results_to_dict(out_by_n)

                if baseline_a is None:
                    baseline_a = current_a
                    baseline_n = current_n
                else:
                    if baseline_a != current_a or baseline_n != current_n:
                        print("Error: Results mismatch between runs.")
                        print("Possible data race.")
                        sys.exit(1)

                totals.append(timers["Total"])
                findings.append(timers["Finding"] or 0.0)
                readings.append(timers["Reading"] or 0.0)
                writings.append(timers["Writing"] or 0.0)

                print(
                    f"  Total={timers['Total']:.0f} ms"
                    + (f", Finding={timers['Finding']:.0f} ms" if timers["Finding"] else "")
                    + (f", Reading={timers['Reading']:.0f} ms" if timers["Reading"] else "")
                    + (f", Writing={timers['Writing']:.0f} ms" if timers["Writing"] else "")
                )

            total_stats = summarize(totals)

            print("Results matched across runs.")
            print("Summary:")
            print(
                f"  Total -> min: {total_stats['min']:.0f} ms, "
                f"avg: {total_stats['avg']:.0f} ms, "
                f"stdev: {total_stats['stdev']:.2f} ms"
            )

    print("\nDone.")


if __name__ == "__main__":
    main()
