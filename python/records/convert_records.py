"""Converts the run records written before docs/code context/RECORDS_FORMAT.md into session folders.

Usage:
    python -I python/records/convert_records.py <records folder> [--write]

Without --write the script only prints, per old folder with an RSI packet capture, the new session
folder, its files, touch count and outcome, then the old entries that are deleted after conversion
(plans/RECORDS_SESSION_FORMAT_EXECPLAN.md, Milestone 5). With --write it creates the session
folders; an existing folder is never overwritten. It deletes nothing.

Rules (ExecPlan, Milestone 2): a folder without rsi_packets_* holds only a Sampling plan and gets no
session. The session start is the first "run" row of the PLC log inside the folder, or else the first
"run" row of a records/plc_io_*.csv, at or after the folder's Sampling time (UTC identifier + 3 h, the
PC's time zone) and before the next Sampling of any old folder. The PLC rows of a session run from that
row to the next "run" row of the same log or its end. RSI rows come from the capture files in order
(main file, then _part2, _part3, ...); a reply joins the latest earlier request with its IPOC. The
result comes from calibration_result_*.csv, or, with all touches detected, from calibration_fit.py.
"""

import argparse
import base64
import csv
import datetime as dt
import glob
import importlib.util
import json
import os
import re
import sys
import xml.etree.ElementTree as ElementTree

LOCAL_ZONE = dt.timezone(dt.timedelta(hours=3))  # Russian Standard Time, UTC+3 without daylight saving
NEW_NAME = re.compile(r"^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_(calibration|chamfer|test-lin)(_failed)?(_\d+)?$")
STAMP = re.compile(r"(\d{8}_\d{6}_\d{3})")
PART = re.compile(r"_part(\d+)\.csv$")
PLC_TIME = "%Y-%m-%d %H:%M:%S.%f"

REQUEST_KEYS = ["IPOC", "Start", "Result", "TouchIndex", "Touch", "Delay.D",
                "RIst.X", "RIst.Y", "RIst.Z", "RIst.A", "RIst.B", "RIst.C",
                "AIPos.A1", "AIPos.A2", "AIPos.A3", "AIPos.A4", "AIPos.A5", "AIPos.A6"]
DECODED_REQUEST = ["ipoc", "start", "result", "touch_index", "touch", "delay",
                   "rist_x", "rist_y", "rist_z", "rist_a", "rist_b", "rist_c",
                   "a1", "a2", "a3", "a4", "a5", "a6"]
REPLY_KEYS = ["RKorr.X", "RKorr.Y", "RKorr.Z", "RKorr.A", "RKorr.B", "RKorr.C", "Stop", "Result"]
DECODED_REPLY = ["corr_x", "corr_y", "corr_z", "corr_a", "corr_b", "corr_c", "stop", "result"]

RSI_HEADER = ("t_s;ipoc;start;result;touch_index;touch;delay;rist_x_mm;rist_y_mm;rist_z_mm;rist_a_deg;rist_b_deg;"
              "rist_c_deg;a1_deg;a2_deg;a3_deg;a4_deg;a5_deg;a6_deg;stream_index;corr_x_mm;corr_y_mm;corr_z_mm;"
              "corr_a_deg;corr_b_deg;corr_c_deg;stop;reply_result;xml_base64")
TIMING_HEADER = "ipoc;t_s;entry_us;sleep_us;wake_us;prev_done_us;reply_us;write_us;write_bytes"
PLC_HEADER = ("t_s;event;x1_0_aut_ext;x1_2_peri_rdy;x1_3_stopmess;x1_4_pro_act;x1_5_pgno_req;x1_6_appl_run;"
              "x1_7_in_home;y1_0_ext_start;y1_1_drives_on;y1_2_conf_mess;y1_3_pgno0;y1_4_pgno1;y1_7_probe;y2_0_spindle;"
              "step;program;result")
RESULT_KEYS = {"ball_centre_F": "ball_centre_f_mm", "sphere_centre_B": "sphere_centre_b_mm",
               "verification_sphere_centre_B": "verification_sphere_centre_b_mm",
               "contact_vertical": "contact_vertical_mm", "contact_horizontal": "contact_horizontal_mm",
               "quantile95": "quantile95_mm", "repeatability": "repeatability_mm",
               "smallest_singular_value": "smallest_singular_value"}


def load_fit_module():
    """python/calibration/calibration_fit.py, loaded by path (isolated mode has no script folder on sys.path)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "calibration", "calibration_fit.py")
    spec = importlib.util.spec_from_file_location("calibration_fit", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sampling_time(name):
    """Local time of the Sampling identifier in a folder name (UTC + 3 h); None without a time stamp."""
    match = STAMP.search(name)
    if not match:
        return None
    utc = dt.datetime.strptime(match.group(1)[:15], "%Y%m%d_%H%M%S").replace(tzinfo=dt.timezone.utc)
    return utc.astimezone(LOCAL_ZONE).replace(tzinfo=None)


def read_plc_log(path):
    """Rows of an old plc_io file: (local time, the 17 values)."""
    rows = []
    with open(path, encoding="utf-8") as file:
        for line in file:
            values = line.rstrip("\n").split(";")
            if values[0] == "time" or len(values) < 17:
                continue
            rows.append((dt.datetime.strptime(values[0], PLC_TIME), values))
    return rows


def plc_window(rows, start_index):
    """State row and the session rows from rows[start_index] to the next "run" row."""
    end = start_index + 1
    while end < len(rows) and not rows[end][1][1].startswith("run "):
        end += 1
    state = rows[start_index - 1] if start_index > 0 else rows[start_index]
    return state, rows[start_index:end]


def find_run(folder, sampling, next_sampling, root_logs):
    """(log rows, index of the session's first "run" row, log name) or None."""
    own = sorted(glob.glob(os.path.join(folder, "plc_io_*.csv")))
    candidates = [(read_plc_log(path), path) for path in own] if own else root_logs
    for rows, path in candidates:
        for index, (time, values) in enumerate(rows):
            if not values[1].startswith("run "):
                continue
            if time >= sampling and (next_sampling is None or time < next_sampling):
                return rows, index, os.path.basename(path)
    return None


def plc_csv(state, rows, start):
    lines = [PLC_HEADER]
    for time, values in [(start, ["", "state"] + state[1][2:])] + rows:
        seconds = (time - start).total_seconds()
        lines.append(";".join(["%.6f" % seconds, values[1]] + values[2:14] + ["", ""] + values[14:17]))
    return "\n".join(lines) + "\n"


def xml_values(data):
    """Element text and Element.Attribute values of one packet, as captureRow; None for unreadable XML."""
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError:
        return None
    values = {}
    for element in root:
        for name, value in element.attrib.items():
            values[element.tag + "." + name] = value
        values[element.tag] = (element.text or "").strip()
    return values or None


def capture_files(folder):
    files = glob.glob(os.path.join(folder, "rsi_packets_*.csv"))
    return sorted(files, key=lambda path: int(PART.search(path).group(1)) if PART.search(path) else 1)


def read_capture(folder):
    """Requests in receive order, each with its joined reply, and the dropped-row count."""
    requests, pending, dropped, orphans = [], {}, 0, 0
    for path in capture_files(folder):
        header = []
        with open(path, newline="", encoding="utf-8") as file:
            for row in csv.reader(file):
                if not row:
                    continue
                if row[0].startswith("#"):
                    if row[0] == "# dropped_rows":
                        dropped += int(row[1])
                    continue
                if row[0] == "pc_ns":
                    header = row
                    continue
                decoded = "ipoc" in header
                fields = dict(zip(header, row)) if decoded else None
                ns = int(row[0])
                if row[1] == "rx":
                    if decoded:
                        values = [fields[key] for key in DECODED_REQUEST]
                        raw = fields["xml_base64"]
                    else:
                        parsed = xml_values(base64.b64decode(row[5]))
                        values = [parsed.get(key, "") for key in REQUEST_KEYS] if parsed else [""] * 18
                        raw = "" if parsed else row[5]
                    request = {"ns": ns, "values": values, "raw": raw, "reply": None}
                    requests.append(request)
                    if values[0]:
                        pending[values[0]] = request
                    continue
                if decoded:
                    ipoc = fields["ipoc"]
                    reply_values = [fields[key] for key in DECODED_REPLY]
                    times = [fields[key] for key in ("entry_ns", "sleep_ns", "wake_ns", "prev_done_ns")]
                    index, written, write_ns = fields["stream_index"], fields["write_bytes"], fields["write_ns"]
                else:
                    parsed = xml_values(base64.b64decode(row[5])) or {}
                    ipoc = parsed.get("IPOC", "")
                    reply_values = [parsed.get(key, "") for key in REPLY_KEYS]
                    times = row[6:10] if len(row) >= 10 else ["", "", "", ""]
                    index, written, write_ns = row[2], row[3], row[4]
                request = pending.pop(ipoc, None)
                if request is None:
                    orphans += 1
                    continue
                request["reply"] = {"ns": ns, "values": reply_values, "times": times, "index": index,
                                    "written": written, "write_ns": write_ns}
    return requests, dropped, orphans


def offset_us(value, origin):
    if value in ("", None) or int(value) < 0:
        return ""
    return "%.1f" % ((int(value) - origin) / 1000.0)


def rsi_tables(requests, dropped):
    """rsi.csv text, rsi_timing.csv text and the session.json "rsi" summary."""
    origin = requests[0]["ns"]
    rsi, timing = [RSI_HEADER], [TIMING_HEADER]
    summary = {"requests": len(requests), "unanswered": 0, "delayed": 0, "max_delay": 0, "unreadable": 0,
               "dropped": dropped}
    for request in requests:
        values, reply, ns = request["values"], request["reply"], request["ns"]
        t_s = "%.6f" % ((ns - origin) / 1e9)
        delay = int(values[5]) if values[5].lstrip("-").isdigit() else 0
        summary["delayed"] += delay > 0
        summary["max_delay"] = max(summary["max_delay"], delay)
        summary["unreadable"] += bool(request["raw"])
        if reply is None:
            summary["unanswered"] += 1
            rsi.append(";".join([t_s] + values + [""] * 9 + [request["raw"]]))
            timing.append(";".join([values[0], t_s] + [""] * 7))
            continue
        rsi.append(";".join([t_s] + values + [reply["index"]] + reply["values"] + [request["raw"]]))
        write_us = "%.1f" % (int(reply["write_ns"]) / 1000.0) if reply["write_ns"].lstrip("-").isdigit() else ""
        timing.append(";".join([values[0], t_s] + [offset_us(time, ns) for time in reply["times"]]
                               + [offset_us(reply["ns"], ns), write_us, reply["written"]]))
    return "\n".join(rsi) + "\n", "\n".join(timing) + "\n", summary


def rename_plan_column(name):
    if name in ("tilt", "roll"):
        return name + "_deg"
    match = re.match(r"^(start|contact)_([XYZABC])$", name)
    if match:
        return "%s_%s_%s" % (match.group(1), match.group(2).lower(), "mm" if match.group(2) in "XYZ" else "deg")
    return name.lower()


def read_old_csv(path):
    """Header and rows of an old comma file without its # lines, and its # plan_id value."""
    plan_id, rows = None, []
    with open(path, newline="", encoding="utf-8") as file:
        for row in csv.reader(file):
            if row and row[0] == "# plan_id":
                plan_id = row[1]
            elif row and not row[0].startswith("#"):
                rows.append(row)
    return rows[0], rows[1:], plan_id


def semicolon_csv(header, rows):
    return "\n".join(";".join(row) for row in [header] + rows) + "\n"


def old_result(folder):
    """session.json "result" and residual_sets.csv rows from calibration_result_*.csv, or None."""
    paths = glob.glob(os.path.join(folder, "calibration_result_*.csv"))
    if not paths:
        return None, None
    result, sets, in_sets = {}, [], False
    with open(paths[0], newline="", encoding="utf-8") as file:
        for row in csv.reader(file):
            if not row or row[0].startswith("#") or row[0] == "quantity":
                continue
            if row[0] == "set":
                in_sets = True
                continue
            if in_sets:
                sets.append(row)
            elif row[0] == "touches":
                result["calibration_touches"], result["verification_touches"] = int(row[1]), int(row[2])
            else:
                numbers = [round(float(value), 6) for value in row[1:] if value != ""]
                result[RESULT_KEYS[row[0]]] = numbers if len(numbers) > 1 else numbers[0]
    ordered = {key: result[key] for key in list(RESULT_KEYS.values()) + ["calibration_touches",
                                                                          "verification_touches"]}
    return ordered, sets


def convert(folder, run, fit_module):
    """Name and files of the session of one old folder with a capture."""
    rows, index, log_name = run
    start = rows[index][0]
    state, plc_rows = plc_window(rows, index)
    plans = (glob.glob(os.path.join(folder, "rsi_plan_*.csv"))
             + glob.glob(os.path.join(folder, "rsi_calibration_plan_*.csv")))
    plan_header, plan_rows, plan_id = read_old_csv(plans[0])
    plan_header = [rename_plan_column(name) for name in plan_header]
    plan = [dict(zip(plan_header, row)) for row in plan_rows]
    files = {"plan.csv": semicolon_csv(plan_header, plan_rows)}
    joints = {}
    touch_paths = glob.glob(os.path.join(folder, "calibration_touches_*.csv"))
    if touch_paths:
        header, touch_rows, _ = read_old_csv(touch_paths[0])
        header = [name if name == "touch" else name + "_deg" for name in header]
        files["touches.csv"] = semicolon_csv(header, touch_rows)
        joints = {int(row[0]): [float(value) for value in row[1:7]] for row in touch_rows}
    result, sets = old_result(folder)
    detected = sum(1 for row in plan if int(row["touch"]) in joints)
    error = ""
    if result is None and detected == len(plan):
        result, computed, error = fit_module.fit(plan, joints)
        sets = [[entry["set"], str(entry["group"])] + ["%.6f" % entry[key] for key in
                                                       ("value_deg", "mean_mm", "bound_mm")] + [str(entry["count"])]
                for entry in computed] if computed else None
    elif result is None:
        error = "No touch detected." if detected == 0 else "%d of %d touches detected." % (detected, len(plan))
    if sets:
        files["residual_sets.csv"] = semicolon_csv(["set", "group", "value_deg", "mean_mm", "bound_mm", "count"],
                                                   sets)
    requests, dropped, orphans = read_capture(folder)
    rsi, timing, summary = rsi_tables(requests, dropped)
    files["rsi.csv"], files["rsi_timing.csv"] = rsi, timing
    files["plc.csv"] = plc_csv(state, plc_rows, start)
    failed = result is None
    session = {
        "format": 1,
        "kind": "calibration",
        "krl_program": "RSI_Probe_Calibration",
        "start": start.replace(tzinfo=LOCAL_ZONE).isoformat(timespec="milliseconds"),
        "duration_s": round((requests[-1]["ns"] - requests[0]["ns"]) / 1e9, 3),
        "outcome": "failed" if failed else "ok",
        "error": error,
        "job_id": plan_id or os.path.basename(folder),
        "inputs": {"touches": len(plan)},
    }
    if not failed:
        session["result"] = result
    session["rsi"] = summary
    session["converted_from"] = os.path.basename(folder)
    session["clock"] = "rsi and plc times have separate origins"
    # As the application writes it: top-level keys in document order, nested keys sorted (QJsonObject), indent 4.
    session = {key: dict(sorted(value.items())) if isinstance(value, dict) else value for key, value in session.items()}
    files["session.json"] = json.dumps(session, indent=4) + "\n"
    name = start.strftime("%Y-%m-%d_%H-%M-%S") + "_calibration" + ("_failed" if failed else "")
    backwards = sum(1 for first, second in zip(requests, requests[1:]) if second["ns"] < first["ns"])
    report = {"log": log_name, "touches": "%d/%d" % (detected, len(plan)), "requests": len(requests),
              "plc_rows": len(plc_rows), "orphan_replies": orphans, "time_reversals": backwards}
    return name, files, report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("records")
    parser.add_argument("--write", action="store_true", help="create the session folders")
    arguments = parser.parse_args()
    records = arguments.records
    entries = sorted(os.listdir(records))
    old = [entry for entry in entries if not NEW_NAME.match(entry)]
    folders = [entry for entry in old if os.path.isdir(os.path.join(records, entry))]
    samplings = sorted((sampling_time(entry), entry) for entry in folders if sampling_time(entry))
    root_logs = [(read_plc_log(path), path) for path in sorted(glob.glob(os.path.join(records, "plc_io_*.csv")))]
    fit_module = load_fit_module()
    for position, (sampling, entry) in enumerate(samplings):
        folder = os.path.join(records, entry)
        if not glob.glob(os.path.join(folder, "rsi_packets_*.csv")):
            continue
        next_sampling = samplings[position + 1][0] if position + 1 < len(samplings) else None
        run = find_run(folder, sampling, next_sampling, root_logs)
        if run is None:
            print("%s: no PLC run row found; not converted" % entry)
            continue
        name, files, report = convert(folder, run, fit_module)
        target = os.path.join(records, name)
        state = "exists, skipped" if os.path.exists(target) else ("written" if arguments.write else "would write")
        print("%s -> %s (%s)" % (entry, name, state))
        print("   %s; files: %s" % (", ".join("%s %s" % item for item in report.items()), " ".join(sorted(files))))
        if arguments.write and not os.path.exists(target):
            os.makedirs(target)
            for file_name, contents in files.items():
                with open(os.path.join(target, file_name), "w", encoding="utf-8", newline="\n") as file:
                    file.write(contents)
    print("old entries to delete after conversion (%d):" % len(old))
    for entry in old:
        print("   " + entry)


if __name__ == "__main__":
    sys.exit(main())
