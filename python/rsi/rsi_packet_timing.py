"""Timing analysis of one RSI run recorded by RoboCrap.

Usage:
    python -I python/rsi/rsi_packet_timing.py <record folder>

The record folder is a session records/<yyyy-MM-dd_HH-mm-ss>_<kind>[_failed] (format:
docs/code context/RECORDS_FORMAT.md). The script reads rsi.csv (one row per controller request
with its reply) and rsi_timing.csv (the same rows: receive time and the handler, event-loop and
reply times as microsecond offsets), and reports late reads and the replies the controller
rejected; for a chamfer session it also reads plan.csv and reports how far the rejected
corrections moved the final pose.
"""

import argparse
import csv
import math
import os

import numpy as np

CYCLE_MS = 4                # IPOC step of one RSI cycle.
LATE_READ_MS = 2.0          # A request read later than this after its reference time.
SAME_READ_MS = 0.3          # Requests read closer together were read in one go.
WOKE_TO_ENTRY_MS = 0.5      # Handler start after wake-up still counts as caused by the wake.
OWN_WORK_MS = 1.0           # Previous handler finishing this long after its reply.
SETTLE_CYCLES = 50          # Zero corrections the app appends after the motion.
HANDLER_COLUMNS = ("entry_us", "sleep_us", "wake_us", "prev_done_us")


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as file:
        return list(csv.DictReader(file, delimiter=";"))


def read_packets(folder):
    """Requests and replies in capture order (each reply right after its request), each as a dict."""
    packets = []
    for row, timing in zip(read_rows(os.path.join(folder, "rsi.csv")),
                           read_rows(os.path.join(folder, "rsi_timing.csv"))):
        if not row["ipoc"]:
            continue  # unreadable request XML
        ns = round(float(row["t_s"]) * 1e9)
        ipoc = int(row["ipoc"])
        packets.append({
            "ns": ns, "rx": True, "index": -1, "ipoc": ipoc,
            "delay": int(row["delay"]) if row["delay"] else None,
            "rist": tuple(float(row["rist_%s_%s" % (axis, "mm" if axis in "xyz" else "deg")])
                          for axis in "xyzabc") if row["rist_x_mm"] else None,
            "control": False, "times": None,
        })
        if not row["stream_index"]:
            continue  # unanswered
        handler = [timing[name] for name in HANDLER_COLUMNS]
        packets.append({
            "ns": ns + round(float(timing["reply_us"]) * 1000), "rx": False,
            "index": int(row["stream_index"]), "ipoc": ipoc, "delay": None, "rist": None,
            "control": row["stop"] == "1" or row["reply_result"] == "1",
            "times": [ns + round(float(value) * 1000) for value in handler] if all(handler) else None,
        })
    return packets


def reference_fit(requests):
    """Line pc_ns = a * IPOC + b through the earliest-read 20 % of requests."""
    ipoc = np.array([request["ipoc"] for request in requests], float)
    ns = np.array([request["ns"] for request in requests], float)
    selected = np.ones(len(ns), bool)
    for _ in range(6):
        a, b = np.polyfit(ipoc[selected], ns[selected], 1)
        residual = ns - (a * ipoc + b)
        selected = residual <= np.percentile(residual, 20)
    return a, b


def late_reads(requests, lateness):
    """Groups of requests read in one go whose first request was read late."""
    groups = []
    first = 0
    while first < len(requests):
        last = first
        while (last + 1 < len(requests)
               and requests[last + 1]["ns"] - requests[last]["ns"] < SAME_READ_MS * 1e6):
            last += 1
        if lateness[requests[first]["ipoc"]] > LATE_READ_MS:
            groups.append((first, last - first + 1))
        first = last + 1
    return groups


def rotation(a, b, c):
    """KUKA A/B/C in degrees: Rz(A) Ry(B) Rx(C)."""
    a, b, c = (math.radians(value) for value in (a, b, c))
    rz = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])
    ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    rx = np.array([[1, 0, 0], [0, math.cos(c), -math.sin(c)], [0, math.sin(c), math.cos(c)]])
    return rz @ ry @ rx


def angle_between(first, second):
    """Shortest rotation angle in degrees between two rotation matrices."""
    cosine = (np.trace(first.T @ second) - 1.0) / 2.0
    return math.degrees(math.acos(max(-1.0, min(1.0, cosine))))


def read_plan(path):
    """Start pose (row 0 target) and the motion corrections, one per stream index; None for no chamfer plan."""
    if not os.path.exists(path):
        return None
    with open(path, newline="", encoding="utf-8") as file:
        rows = list(csv.reader(file, delimiter=";"))
    if rows[0][0] != "time_s":
        return None
    values = [[float(value) for value in row] for row in rows[1:] if row]
    return values[0][1:7], [row[13:19] for row in values[1:]]


def replay(start, corrections, skipped):
    """Position and rotation after composing the corrections as RSI POSCORR (WORLD, relative)."""
    position = np.array(start[:3])
    orientation = rotation(*start[3:])
    for index, correction in enumerate(corrections):
        if index in skipped:
            continue
        position = position + np.array(correction[:3])
        orientation = rotation(*correction[3:]) @ orientation
    return position, orientation


def classify(reply, previous_reply, reference_ns):
    """Cause of a late read from the reply's thread timestamps (Milestone 2 columns)."""
    if previous_reply is None:
        return "first reply, no previous handler to compare"
    entry_ns, sleep_ns, wake_ns, prev_done_ns = reply["times"]
    slept = sleep_ns > prev_done_ns
    if prev_done_ns - previous_reply["ns"] > OWN_WORK_MS * 1e6:
        return "own work"
    # Before the next wait (RsiDevice::serve runs processEvents there).
    if sleep_ns - prev_done_ns > OWN_WORK_MS * 1e6:
        return "busy elsewhere (%.3f ms before waiting)" % ((sleep_ns - prev_done_ns) / 1e6)
    if slept and entry_ns - wake_ns <= WOKE_TO_ENTRY_MS * 1e6:
        return "woke late (%.3f ms after reference)" % ((wake_ns - reference_ns) / 1e6)
    return "busy elsewhere"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    args = parser.parse_args()

    packets = read_packets(args.folder)
    requests = [packet for packet in packets if packet["rx"]]
    replies = [packet for packet in packets if not packet["rx"]]
    print("requests %d, replies %d" % (len(requests), len(replies)))

    a, b = reference_fit(requests)
    reference = {request["ipoc"]: a * request["ipoc"] + b for request in requests}
    lateness = {request["ipoc"]: (request["ns"] - reference[request["ipoc"]]) / 1e6
                for request in requests}
    print("reference fit: %.3f PC ns per IPOC ms; typical read lateness %.3f ms"
          % (a, float(np.median(list(lateness.values())))))

    by_ipoc = {request["ipoc"]: request for request in requests}
    has_delay = any(request["delay"] is not None for request in requests)
    rejected = []
    if has_delay:
        for reply in replies:
            following = by_ipoc.get(reply["ipoc"] + CYCLE_MS)
            if following is not None and following["delay"]:
                rejected.append(reply)
        print("rejected replies %d: stream indices %s"
              % (len(rejected), " ".join(str(reply["index"]) for reply in rejected) or "none"))
        for reply in rejected:
            print("  index %d: request read %.3f ms late, reply sent %.3f ms after reference%s"
                  % (reply["index"], lateness[reply["ipoc"]],
                     (reply["ns"] - reference[reply["ipoc"]]) / 1e6,
                     ", stop/result reply" if reply["control"] else ""))
    else:
        print("rejected replies: unknown (the capture has no Delay field)")

    groups = late_reads(requests, lateness)
    print("late reads (> %.0f ms) %d" % (LATE_READ_MS, len(groups)))
    reply_after = {}
    previous = None
    for packet in packets:
        if not packet["rx"]:
            reply_after[packet["ipoc"]] = (packet, previous)
            previous = packet
    for first, count in groups:
        request = requests[first]
        line = "  t=%8.3f s: %d request(s), first read %.3f ms late" % (
            (request["ns"] - requests[0]["ns"]) / 1e9, count, lateness[request["ipoc"]])
        newest = requests[first + count - 1]["ipoc"]
        reply, previous_reply = reply_after.get(newest, (None, None))
        if reply is not None and reply["times"] is not None:
            line += "; " + classify(reply, previous_reply, reference[request["ipoc"]])
        print(line)

    plan = read_plan(os.path.join(args.folder, "plan.csv"))
    if plan:
        start, corrections = plan
        final = next(packet["rist"] for packet in reversed(requests) if packet["rist"])
        measured = rotation(*final[3:])
        skipped = {reply["index"] for reply in rejected if reply["index"] < len(corrections)}
        print("plan: %d motion corrections + %d settle zeros; %d rejected motion corrections"
              % (len(corrections), SETTLE_CYCLES, len(skipped)))
        for name, omitted in (("all applied", set()), ("rejected omitted", skipped)):
            position, orientation = replay(start, corrections, omitted)
            print("final difference to RIst, %s: %.3f deg, %.3f mm"
                  % (name, angle_between(orientation, measured),
                     float(np.linalg.norm(position - np.array(final[:3])))))


if __name__ == "__main__":
    main()
