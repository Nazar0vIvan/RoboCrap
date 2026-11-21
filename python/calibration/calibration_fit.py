"""Offline probe-sphere calibration of one recorded session, the same computation as the application.

Usage:
    python -I python/calibration/calibration_fit.py <record folder>

The record folder is a calibration session records/<yyyy-MM-dd_HH-mm-ss>_calibration[_failed]
(format: docs/code context/RECORDS_FORMAT.md). It must hold plan.csv and touches.csv; the
script also compares its result with the folder's session.json "result" and residual_sets.csv
and exits with status 1 when a value differs by more than COMPARE_TOLERANCE.

This is a copy of fitProbeCalibration (src/trajectories/probe/probecalibrationfit.cpp): the base
model of docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md, section 5, with the
nominal KR 10 forward kinematics of Kr10Kinematics::solveFK and the joint table Joints of
src/application/robot/robotdefinition.cpp. A change of either must be repeated here; on session
calibration_20261118_113045_223 the output equals the application's result to all six decimals.
Output: the result values and the residual sets, then the differences to the stored values.
"""

import argparse
import csv
import json
import math
import os
import sys

import numpy as np

# robotdefinition.cpp Joints: offset to the preceding frame (mm, Z-Y-X deg) and the sign of the local z axis.
JOINTS = [
    (0.0, 0.0, 227.0, 0.0, 0.0, 0.0, -1.0),
    (150.0, -90.4, 223.0, 0.0, 0.0, -90.0, 1.0),
    (0.0, -610.0, -11.1, 90.0, 0.0, 0.0, 1.0),
    (-20.0, -357.0, 101.5, 0.0, 0.0, -90.0, 1.0),
    (0.0, -39.0, -303.0, 0.0, 0.0, 90.0, 1.0),
    (0.0, -80.0, -39.0, 0.0, 0.0, 90.0, -1.0),
]
Q_HOME = (0.0, -90.0, 90.0, 0.0, 0.0, 0.0)  # deg
BALL_START = np.array([-0.048, -0.097, 127.0])  # TOOL_DATA[5], mm: start value of the ball centre in {F}
CONTACT_START = 14.0  # nominal contact distance L = r_b + r_s, mm
MAX_ITERATIONS = 50
STEP_TOLERANCE = 1e-9  # mm
RANK_TOLERANCE = 1e-9  # smallest singular value relative to the largest
ANGLE_TOLERANCE = 1e-6  # deg
QUANTILE_LEVEL = 0.95
COMPARE_TOLERANCE = 1.5e-6  # stored values have six decimals


def rotation(axis, angle):
    """4x4 rotation by angle (deg) about the x, y or z axis."""
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    i, j = {"x": (1, 2), "y": (2, 0), "z": (0, 1)}[axis]
    matrix = np.eye(4)
    matrix[i, i], matrix[i, j], matrix[j, i], matrix[j, j] = c, -s, s, c
    return matrix


def translation(x, y, z):
    matrix = np.eye(4)
    matrix[:3, 3] = (x, y, z)
    return matrix


LOCAL = [translation(dx, dy, dz) @ rotation("z", da) @ rotation("y", db) @ rotation("x", dc)
         for dx, dy, dz, da, db, dc, _ in JOINTS]


def flange(joints):
    """Flange pose in {B} for the six joint angles (deg), as Kr10Kinematics::solveFK."""
    pose = np.eye(4)
    for index, (*_, sign) in enumerate(JOINTS):
        pose = pose @ LOCAL[index] @ rotation("z", sign * (joints[index] - Q_HOME[index]))
    return pose


def read_rows(path):
    """Rows of one record CSV (separator ';', one header line) as dicts."""
    with open(path, newline="", encoding="utf-8") as file:
        return list(csv.DictReader(file, delimiter=";"))


def read_session(folder):
    """Plan rows and detected joint angles (touch number -> six deg) of one record folder, or an error."""
    plan_path, touches_path = os.path.join(folder, "plan.csv"), os.path.join(folder, "touches.csv")
    if not os.path.exists(plan_path):
        return None, None, "The folder has no plan.csv."
    joints = {}
    if os.path.exists(touches_path):
        joints = {int(row["touch"]): [float(row["a%d_deg" % axis]) for axis in range(1, 7)]
                  for row in read_rows(touches_path)}
    return read_rows(plan_path), joints, None


def plan_touches(plan, joints):
    """Plan touches with their flange poses and the start sphere centre; an error when a touch is missing."""
    missing = [row["touch"] for row in plan if int(row["touch"]) not in joints]
    if len(missing) == len(plan):
        return None, "No touch detected."
    if missing:
        return None, "%d of %d touches detected." % (len(plan) - len(missing), len(plan))
    touches = []
    for row in plan:
        pose = flange(joints[int(row["touch"])])
        touches.append({"purpose": row["purpose"], "group": int(row["group"]), "tilt": float(row["tilt_deg"]),
                        "roll": float(row["roll_deg"]), "position": pose[:3, 3], "rotation": pose[:3, :3]})
    first = plan[0]  # tilt 0: contact point = sphere centre + L * normal
    sphere = np.array([float(first["contact_%s_mm" % axis]) - CONTACT_START * float(first["normal_" + axis])
                       for axis in "xyz"])
    return (touches, sphere), None


def residual(model, touch):
    """Distance of the calculated ball centre from the sphere centre minus the class's contact distance."""
    ball, sphere, vertical, horizontal = model
    contact = horizontal if touch["group"] == 2 else vertical
    return np.linalg.norm(touch["position"] + touch["rotation"] @ ball - sphere) - contact


def solve(touches, model, calibration):
    """Gauss-Newton: all 8 parameters (calibration) or only the sphere centre (verification)."""
    columns = 8 if calibration else 3
    ball, sphere, vertical, horizontal = model
    for _ in range(MAX_ITERATIONS):
        jacobian = np.zeros((len(touches), columns))
        residuals = np.zeros(len(touches))
        for row, touch in enumerate(touches):
            direction = touch["position"] + touch["rotation"] @ ball - sphere
            direction /= np.linalg.norm(direction)
            residuals[row] = residual((ball, sphere, vertical, horizontal), touch)
            if not calibration:
                jacobian[row] = -direction
                continue
            jacobian[row, 0:3] = direction @ touch["rotation"]
            jacobian[row, 3:6] = -direction
            jacobian[row, 6] = 0.0 if touch["group"] == 2 else -1.0
            jacobian[row, 7] = -1.0 if touch["group"] == 2 else 0.0
        singular = np.linalg.svd(jacobian, compute_uv=False)
        if not singular[-1] >= RANK_TOLERANCE * singular[0]:
            sys.exit("The parameters are not identifiable from these touches.")
        step = np.linalg.lstsq(jacobian, -residuals, rcond=None)[0]
        if calibration:
            ball, sphere = ball + step[0:3], sphere + step[3:6]
            vertical, horizontal = vertical + step[6], horizontal + step[7]
        else:
            sphere = sphere + step
        if np.abs(step).max() < STEP_TOLERANCE:
            return (ball, sphere, vertical, horizontal), singular[-1]
    sys.exit("The fit did not converge in %d iterations." % MAX_ITERATIONS)


def same_direction(first, second):
    return (first["group"] == second["group"] and abs(first["tilt"] - second["tilt"]) < ANGLE_TOLERANCE
            and abs(first["roll"] - second["roll"]) < ANGLE_TOLERANCE)


def repeatability(calibration, model):
    """s_rep from the residual differences of the twice-touched directions (method 5.2.3)."""
    squares = [(residual(model, first) - residual(model, second)) ** 2
               for index, first in enumerate(calibration) for second in calibration[index + 1:]
               if same_direction(first, second)]
    return math.sqrt(sum(squares) / (2 * len(squares)))


def residual_sets(calibration, model, spread):
    """Method step K7: mean residual of every tilt ring and roll of each group, with its bound."""
    sets = []
    for ring in (True, False):
        for touch in calibration:
            value = touch["tilt"] if ring else touch["roll"]
            match = next((entry for entry in sets if entry["group"] == touch["group"] and entry["ring"] == ring
                          and abs(entry["value"] - value) < ANGLE_TOLERANCE), None)
            if match is None:
                match = {"group": touch["group"], "ring": ring, "value": value, "sum": 0.0, "count": 0}
                sets.append(match)
            match["sum"] += residual(model, touch)
            match["count"] += 1
    return [(entry["ring"], entry["group"], entry["value"], entry["sum"] / entry["count"],
             3.0 * spread / math.sqrt(entry["count"]), entry["count"]) for entry in sets]


def rounded(values):
    return [round(float(value), 6) for value in values]


def fit(plan, joints):
    """Result (keys of session.json "result") and residual sets (rows of residual_sets.csv), or an error."""
    data, error = plan_touches(plan, joints)
    if error:
        return None, None, error
    touches, sphere = data
    calibration = [touch for touch in touches if touch["purpose"] == "calibration"]
    verification = [touch for touch in touches if touch["purpose"] == "verification"]
    model, smallest = solve(calibration, (BALL_START.copy(), sphere, CONTACT_START, CONTACT_START), True)
    spread = repeatability(calibration, model)
    check, _ = solve(verification, model, False)
    magnitudes = sorted(abs(residual(check, touch)) for touch in verification)
    quantile = magnitudes[math.ceil(QUANTILE_LEVEL * len(magnitudes)) - 1]
    ball, centre, vertical, horizontal = model
    result = {
        "ball_centre_f_mm": rounded(ball),
        "sphere_centre_b_mm": rounded(centre),
        "verification_sphere_centre_b_mm": rounded(check[1]),
        "contact_vertical_mm": round(float(vertical), 6),
        "contact_horizontal_mm": round(float(horizontal), 6),
        "quantile95_mm": round(float(quantile), 6),
        "repeatability_mm": round(float(spread), 6),
        "smallest_singular_value": round(float(smallest), 6),
        "calibration_touches": len(calibration),
        "verification_touches": len(verification),
    }
    sets = [{"set": "ring" if ring else "roll", "group": group, "value_deg": round(float(value), 6),
             "mean_mm": round(float(mean), 6), "bound_mm": round(float(bound), 6), "count": count}
            for ring, group, value, mean, bound, count in residual_sets(calibration, model, spread)]
    return result, sets, None


def as_list(value):
    return value if isinstance(value, list) else [value]


def differences(result, sets, folder):
    """Lines naming every value that differs from the folder's session.json result and residual_sets.csv."""
    lines = []
    session_path = os.path.join(folder, "session.json")
    stored = None
    if os.path.exists(session_path):
        with open(session_path, encoding="utf-8") as file:
            stored = json.load(file).get("result")
    if not stored:
        lines.append("session.json has no result")
    else:
        for key, value in result.items():
            other = as_list(stored.get(key))
            if len(other) != len(as_list(value)) or any(
                    other_value is None or abs(own - other_value) > COMPARE_TOLERANCE
                    for own, other_value in zip(as_list(value), other)):
                lines.append("%s: computed %s, stored %s" % (key, value, stored.get(key)))
    sets_path = os.path.join(folder, "residual_sets.csv")
    stored_sets = read_rows(sets_path) if os.path.exists(sets_path) else []
    if len(stored_sets) != len(sets):
        lines.append("residual_sets.csv: %d rows stored, %d computed" % (len(stored_sets), len(sets)))
    for computed, row in zip(sets, stored_sets):
        if (computed["set"] != row["set"] or computed["group"] != int(row["group"])
                or computed["count"] != int(row["count"])
                or any(abs(computed[key] - float(row[key])) > COMPARE_TOLERANCE
                       for key in ("value_deg", "mean_mm", "bound_mm"))):
            lines.append("residual set differs: computed %s, stored %s" % (computed, dict(row)))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder", help="calibration record folder records/<name>")
    arguments = parser.parse_args()
    plan, joints, error = read_session(arguments.folder)
    if not error:
        result, sets, error = fit(plan, joints)
    if error:
        sys.exit(error)
    for key, value in result.items():
        print("%s;%s" % (key, ";".join("%.6f" % item if isinstance(item, float) else str(item)
                                       for item in as_list(value))))
    print("set;group;value_deg;mean_mm;bound_mm;count")
    for entry in sets:
        print("%s;%d;%.6f;%.6f;%.6f;%d" % (entry["set"], entry["group"], entry["value_deg"], entry["mean_mm"],
                                           entry["bound_mm"], entry["count"]))
    lines = differences(result, sets, arguments.folder)
    print("differences to the stored result: %s" % ("none" if not lines else len(lines)))
    for line in lines:
        print("  " + line)
    if lines:
        sys.exit(1)


if __name__ == "__main__":
    main()
