"""Offline probe-sphere calibration of one recorded session, the same computation as the application.

Usage:
    python -I python/calibration/calibration_fit.py <record folder>

The record folder is records/<id> in the repository. It must hold the Sampling plan
(rsi_plan_<id>.csv; the two oldest sessions: rsi_calibration_plan_<id>.csv) and the touch file
calibration_touches_<id>.csv written by the application at the end of a calibration run.

This is a copy of fitProbeCalibration (src/trajectories/probe/probecalibrationfit.cpp): the base
model of docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md, section 5, with the
nominal KR 10 forward kinematics of Kr10Kinematics::solveFK and the joint table Joints of
src/application/robot/robotdefinition.cpp. A change of either must be repeated here; on session
calibration_20261118_113045_223 the output equals the application's calibration_result file to
all six decimals. Output: the rows of calibration_result_<id>.csv.
"""

import argparse
import csv
import glob
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
    with open(path, newline="") as file:
        return list(csv.DictReader(line for line in file if not line.startswith("#")))


def read_touches(folder):
    """Plan touches with their flange poses; an error text when a touch is missing."""
    plans = (glob.glob(os.path.join(folder, "rsi_plan_*.csv"))
             + glob.glob(os.path.join(folder, "rsi_calibration_plan_*.csv")))
    detections = glob.glob(os.path.join(folder, "calibration_touches_*.csv"))
    if not plans or not detections:
        return None, "The folder needs a plan file and calibration_touches_<id>.csv."
    plan = read_rows(plans[0])
    joints = {int(row["touch"]): [float(row["a%d" % axis]) for axis in range(1, 7)]
              for row in read_rows(detections[0])}
    missing = [row["touch"] for row in plan if int(row["touch"]) not in joints]
    if missing:
        return None, "%d of %d touches detected; the calibration needs all of them." % (
            len(plan) - len(missing), len(plan))
    touches = []
    for row in plan:
        pose = flange(joints[int(row["touch"])])
        touches.append({"purpose": row["purpose"], "group": int(row["group"]), "tilt": float(row["tilt"]),
                        "roll": float(row["roll"]), "position": pose[:3, 3], "rotation": pose[:3, :3]})
    first = plan[0]  # tilt 0: contact point = sphere centre + L * normal
    sphere = np.array([float(first["contact_" + axis]) - CONTACT_START * float(first["normal_" + axis])
                       for axis in "XYZ"])
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder", help="record folder records/<id>")
    arguments = parser.parse_args()
    data, error = read_touches(arguments.folder)
    if error:
        sys.exit(error)
    touches, sphere = data
    calibration = [touch for touch in touches if touch["purpose"] == "calibration"]
    verification = [touch for touch in touches if touch["purpose"] == "verification"]
    model, smallest = solve(calibration, (BALL_START.copy(), sphere, CONTACT_START, CONTACT_START), True)
    spread = repeatability(calibration, model)
    check, _ = solve(verification, model, False)
    magnitudes = sorted(abs(residual(check, touch)) for touch in verification)
    quantile = magnitudes[math.ceil(QUANTILE_LEVEL * len(magnitudes)) - 1]
    ball, centre, vertical, horizontal = model
    print("quantity,value_1,value_2,value_3")
    print("ball_centre_F,%.6f,%.6f,%.6f" % tuple(ball))
    print("sphere_centre_B,%.6f,%.6f,%.6f" % tuple(centre))
    print("verification_sphere_centre_B,%.6f,%.6f,%.6f" % tuple(check[1]))
    print("contact_vertical,%.6f" % vertical)
    print("contact_horizontal,%.6f" % horizontal)
    print("quantile95,%.6f" % quantile)
    print("repeatability,%.6f" % spread)
    print("smallest_singular_value,%.6f" % smallest)
    print("touches,%d,%d" % (len(calibration), len(verification)))
    print("set,group,value_deg,mean_mm,bound_mm,count")
    for ring, group, value, mean, bound, count in residual_sets(calibration, model, spread):
        print("%s,%d,%.6f,%.6f,%.6f,%d" % ("ring" if ring else "roll", group, value, mean, bound, count))


if __name__ == "__main__":
    main()
