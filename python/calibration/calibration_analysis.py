"""Offline analysis of format-1 probe calibration records. Prints JSON in mm, s and mm/s.

    python -I python/calibration/calibration_analysis.py sessions <folder> [<folder> ...]
    python -I python/calibration/calibration_analysis.py search <folder>
    python -I python/calibration/calibration_analysis.py gauge <combined-session-folder>

Pass --ball-centre X Y Z (flange frame, mm) to search a partial run without a fitted centre.
Null search metrics indicate insufficient contiguous data or a speed plateau that never occurred.
"""

import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np

here = Path(__file__).resolve().parent
sys.path[:0] = [str(here), str(here.parent / "rsi")]
import calibration_fit as fit
from rsi_packet_timing import CYCLE_MS, iter_packets


def read_calibration(folder):
    metadata = json.loads((folder / "session.json").read_text(encoding="utf-8"))
    if metadata.get("format") != 1 or metadata.get("kind") != "calibration":
        raise ValueError(f"{folder}: expected a format-1 calibration session")
    plan, joints, error = fit.read_session(folder)
    if error or not plan:
        raise ValueError(f"{folder}: {error or 'empty touch plan'}")
    numbers = [int(row["touch"]) for row in plan]
    if len(numbers) != len(set(numbers)):
        raise ValueError(f"{folder}: duplicate planned touch number")
    return metadata, plan, joints


def quantile95(errors):
    ordered = sorted(abs(value) for value in errors)
    return ordered[math.ceil(fit.QUANTILE_LEVEL * len(ordered)) - 1]


def verification_metrics(touches, model):
    errors = np.array([fit.residual(model, touch) for touch in touches])
    groups = np.array([touch["group"] for touch in touches])
    metrics = {}
    for group in sorted(set(groups)):
        selected = errors[groups == group]
        metrics[str(group)] = {
            "touches": len(selected), "mean_mm": selected.mean(),
            "mean_abs_mm": abs(selected).mean(), "sd_mm": selected.std(ddof=1) if len(selected) > 1 else None,
            "quantile95_mm": quantile95(selected) if len(selected) >= 20 else None,
        }
    return {"touches": len(errors), "quantile95_mm": quantile95(errors), "groups": metrics}


def report_fit(calibration, verification, start, full_repeatability):
    if not calibration or not verification:
        raise ValueError("both calibration and verification touches are required for each fit")
    model, smallest = fit.solve(calibration, start, True)
    spread = full_repeatability if full_repeatability is not None else fit.repeatability(calibration, model)
    check, _ = fit.solve(verification, model, False)
    rows = fit.jacobian(calibration, model, True)
    _, singular, vt = np.linalg.svd(rows, full_matrices=False)
    factors = np.linalg.norm(vt.T / singular, axis=1)
    sets = [{"set": "ring" if ring else "roll", "group": group, "angle_deg": angle,
             "mean_mm": mean, "bound_mm": bound, "count": count}
            for ring, group, angle, mean, bound, count in fit.residual_sets(calibration, model, spread)]
    report = {
        "calibration_touches": len(calibration),
        "ball_centre_f_mm": model[0].tolist(), "sphere_centre_b_mm": model[1].tolist(),
        "contact_vertical_mm": model[2], "contact_horizontal_mm": model[3],
        "verification_sphere_centre_b_mm": check[1].tolist(),
        "verification": verification_metrics(verification, check),
        "full_session_repeatability_mm": spread, "smallest_singular_value": smallest,
        "sd_factors": dict(zip(("ball_x", "ball_y", "ball_z", "sphere_x", "sphere_y", "sphere_z",
                                "contact_vertical", "contact_horizontal"), factors)),
        "sets_outside": sum(1 for row in sets if abs(row["mean_mm"]) > row["bound_mm"]),
        "residual_sets": sets,
    }
    return model, check, report


def roll_contrasts(touches, model, spread, verification):
    rings = (15, 45, 75, 90) if verification else (30,)
    groups = (1, 2) if verification else (1, 2, 3)
    positive = (30, 150, 270) if verification else (0, 120, 240)
    negative = (90, 210, 330) if verification else (60, 180, 300)
    report = []
    for group in groups:
        for tilt in rings:
            ring = [t for t in touches if t["group"] == group and t["tilt"] == tilt]
            plus = [fit.residual(model, t) for t in ring if t["roll"] in positive]
            minus = [fit.residual(model, t) for t in ring if t["roll"] in negative]
            if not plus or not minus:
                continue
            report.append({"component": "2a_s" if verification else "2a_c",
                           "group": group, "tilt_deg": tilt, "positive_count": len(plus),
                           "negative_count": len(minus), "contrast_mm": np.mean(plus) - np.mean(minus),
                           "standard_error_mm": spread * math.sqrt(1 / len(plus) + 1 / len(minus))})
    return report


def sessions(folders):
    if len(set(folder.resolve() for folder in folders)) != len(folders):
        raise ValueError("sessions requires distinct record folders")
    reports, full_fits = [], []
    for folder in folders:
        metadata, plan, joints = read_calibration(folder)
        data, error = fit.plan_touches(plan, joints)
        if error or metadata["outcome"] != "ok":
            raise ValueError(f"{folder}: {error or metadata.get('error') or 'unsuccessful session'}")
        touches, sphere = data
        calibration = [t for t in touches if t["purpose"] == "calibration"]
        verification = [t for t in touches if t["purpose"] == "verification"]
        start = fit.BALL_START, sphere, fit.CONTACT_START, fit.CONTACT_START
        model, check, full = report_fit(calibration, verification, start, None)
        spread = full["full_session_repeatability_mm"]
        results = {"session": folder.name, "full": full}
        for repetition, label in ((1, "passes_1_3"), (2, "passes_4_6")):
            subset = [t for row, t in zip(plan, touches) if t["purpose"] == "calibration"
                      and int(row["repetition"]) == repetition]
            _, _, results[label] = report_fit(subset, verification, start, spread)
        results["roll_contrasts"] = (roll_contrasts(calibration, model, spread, False)
                                     + roll_contrasts(verification, check, spread, True))
        reports.append(results)
        full_fits.append((folder, model, verification))
    pairs = []
    for source, model, _ in full_fits:
        for target, target_model, verification in full_fits:
            if source == target:
                continue
            start = model[0], target_model[1], model[2], model[3]
            check, _ = fit.solve(verification, start, False)
            pairs.append({"parameters_from": source.name, "verification_from": target.name,
                          "quantile95_mm": quantile95([fit.residual(check, t) for t in verification])})
    return {"sessions": reports, "ordered_pairs": pairs}


def search_metrics(packets, direction, ball, feed):
    """One search, ending at its first valid detection. Positions are projected on its approach."""
    ipoc = np.array([p["ipoc"] for p in packets], dtype=np.int64)
    dt = np.diff(ipoc)
    if (dt <= 0).any():
        raise ValueError("IPOC did not increase within the search")
    projected = np.full(len(packets), np.nan)
    for i, packet in enumerate(packets):
        if packet["joints"] is not None:
            pose = fit.flange(packet["joints"])
            projected[i] = direction @ (pose[:3, 3] + pose[:3, :3] @ ball)
    speed = np.diff(projected) * 1000 / dt
    speed[dt != CYCLE_MS] = np.nan
    streak = 0
    settled = None
    for i, value in enumerate(speed):
        streak = streak + 1 if abs(value - feed) <= 0.05 * feed else 0
        if streak == 25:
            settled = projected[i + 1] - projected[0]
            break
    mean100 = None
    if len(speed) >= 25 and np.isfinite(speed[-25:]).all():
        mean100 = (projected[-1] - projected[-26]) / 0.100
    before = speed[-51:-1]
    metrics = {
        "start_ipoc": int(ipoc[0]), "detection_ipoc": int(ipoc[-1]),
        "elapsed_cycles": (int(ipoc[-1]) - int(ipoc[0])) / CYCLE_MS,
        "irregular_intervals": int(np.count_nonzero(dt != CYCLE_MS)),
        "missing_joint_samples": int(np.count_nonzero(~np.isfinite(projected))),
        "travel_to_detection_mm": projected[-1] - projected[0], "settled_after_mm": settled,
        "detection_mm_s": speed[-1] if len(speed) else None,
        "previous_mm_s": speed[-2] if len(speed) >= 2 else None,
        "previous_50_peak_to_peak_mm_s": np.ptp(before) if len(before) == 50 else None,
        "trailing_100ms_mm_s": mean100,
    }
    return {k: v if v is None or math.isfinite(v) else None for k, v in metrics.items()}


def search(folder, reference_ball):
    metadata, plan, joints = read_calibration(folder)
    ball = reference_ball if reference_ball is not None else metadata.get("result", {}).get("ball_centre_f_mm")
    if ball is None or len(ball) != 3 or not np.isfinite(ball).all():
        raise ValueError(f"{folder}: supply --ball-centre X Y Z in flange millimetres for a run without a fit")
    ball_array = np.asarray(ball)
    feed = metadata["inputs"].get("search_feed_mm_s")
    if feed is None and metadata.get("converted_from"):
        feed = 1.0  # Fixed speed of the historical calibration program.
    if feed is None or not math.isfinite(feed) or feed <= 0:
        raise ValueError(f"{folder}: missing or invalid recorded search feed")
    rows = {int(row["touch"]): row for row in plan}
    found, packets = {}, []
    active = None
    for packet in iter_packets(folder):
        if not packet["rx"]:
            continue
        number = packet["touch_index"]
        if packet["start"] == 0:
            active = None
            packets.clear()
            continue
        if packet["start"] != 1 or number not in rows or number in found:
            continue
        if active != number:
            active = number
            packets.clear()
        packets.append(packet)
        if packet["touch"] != 1 or packet["joints"] is None:
            continue
        normal = np.array([float(rows[number]["normal_" + axis]) for axis in "xyz"])
        metrics = search_metrics(packets, -normal, ball_array, feed)
        # The run recorder independently stores the detected joints at six decimals.
        matches = number in joints and np.allclose(packet["joints"], joints[number], rtol=0, atol=0.000001)
        found[number] = {"touch": number, "matches_touches_csv": bool(matches), **metrics}
        packets.clear()
    sphere = [n for n, row in rows.items() if row["purpose"] in ("calibration", "verification")]
    duration = None
    if sphere and sphere[0] in found and sphere[-1] in found:
        duration = (found[sphere[-1]]["detection_ipoc"] - found[sphere[0]]["detection_ipoc"]) / 1000
    return {"session": folder.name, "planned": len(plan), "captured_detections": len(found),
            "ball_centre_f_mm": ball, "centre_source": "argument" if reference_ball is not None else "session fit",
            "search_feed_mm_s": feed, "sphere_first_to_last_detection_s": duration,
            "touches": [found.get(n, {"touch": n, "status": "no valid detection in capture"}) for n in rows]}


def gauge(folder):
    metadata, plan, joints = read_calibration(folder)
    inputs = metadata["inputs"]
    if "gauge_length_mm" not in inputs:
        raise ValueError("This session contains no gauge reference. Select a combined calibration record.")
    length = float(inputs["gauge_length_mm"])
    if not math.isfinite(length) or length <= 0:
        raise ValueError("Expected a positive gauge reference length in mm")
    fitted, _, error = fit.fit(plan, joints)
    if error:
        raise ValueError(error)
    ball = np.array(fitted["ball_centre_f_mm"])
    groups = {group: {"A": [], "B": []} for group in (2, 1)}
    for row in plan:
        if row["purpose"] == "gauge":
            pose = fit.flange(joints[int(row["touch"])])
            groups[int(row["group"])][row["face"]].append(pose[:3, 3] + pose[:3, :3] @ ball)
    reports = []
    for group, faces in groups.items():
        if any(len(points) != 8 for points in faces.values()):
            raise ValueError(f"Expected eight detections per gauge face for group {group}")
        a, b = np.array(faces["A"]), np.array(faces["B"])
        centred = np.vstack((a - a.mean(axis=0), b - b.mean(axis=0)))
        _, singular, axes = np.linalg.svd(centred, full_matrices=False)
        if singular[1] <= singular[0] * 1e-9:
            raise ValueError(f"Gauge points do not determine parallel planes for group {group}")
        distance = abs(float((a.mean(axis=0) - b.mean(axis=0)) @ axes[-1]))
        sigma = float(singular[-1]) / math.sqrt(len(centred) - 4) * math.sqrt(1 / len(a) + 1 / len(b))
        contact = fitted["contact_horizontal_mm" if group == 2 else "contact_vertical_mm"]
        sphere_radius = contact - float(inputs["sphere_radius_mm"])
        measured = distance - 2 * sphere_radius
        difference = measured - length
        reports.append({"group": group, "ball_centre_plane_distance_mm": distance,
                        "plane_distance_std_mm": sigma, "gauge_effective_ball_radius_mm": (distance - length) / 2,
                        "sphere_effective_ball_radius_mm": sphere_radius, "measured_length_mm": measured,
                        "length_error_mm": difference, "exceeds_three_std": abs(difference) > 3 * sigma})
    return {"reference_length_mm": length, "reference": inputs["gauge_reference"], "groups": reports}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("sessions").add_argument("folders", type=Path, nargs="+")
    commands.add_parser("gauge").add_argument("folder", type=Path)
    searches = commands.add_parser("search")
    searches.add_argument("folder", type=Path)
    searches.add_argument("--ball-centre", type=float, nargs=3, metavar=("X", "Y", "Z"))
    args = parser.parse_args()
    try:
        if args.command == "sessions":
            result = sessions(args.folders)
        elif args.command == "gauge":
            result = gauge(args.folder)
        else:
            result = search(args.folder, args.ball_centre)
        json.dump(result, sys.stdout, indent=2, allow_nan=False)
        print()
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
