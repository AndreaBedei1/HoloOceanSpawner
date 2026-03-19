"""Populate ExampleLevel with runtime-spawned assets for sonar experiments.

Public-facing category names use English labels such as ``mine``,
``torpedo``, and ``anchor``. The underlying Unreal asset package names remain
unchanged to avoid breaking binary asset references.
"""

import json
import math
import random
import zlib
from pathlib import Path

import holoocean
from holoocean.command import Command


CATEGORY_ALIASES = {
    "mine": "mine",
    "mina": "mine",
    "torpedo": "torpedo",
    "siluro": "torpedo",
    "anchor": "anchor",
    "ancora": "anchor",
    "rock": "rocks",
    "rocks": "rocks",
    "coral": "coral",
    "seaweed": "seaweed",
    "seaweed_giant": "giant_seaweed",
    "giant_seaweed": "giant_seaweed",
}


def normalize_category(category):
    """Normalize category aliases to stable public English names."""

    if not isinstance(category, str):
        return ""
    key = category.strip().lower()
    return CATEGORY_ALIASES.get(key, key)


class SpawnAssetCommand(Command):
    """Client wrapper for the native Unreal ``SpawnAsset`` world command."""

    def __init__(
        self,
        mesh_asset_path,
        location=None,
        rotation=None,
        scale=None,
        label="",
        units="ue",
    ):
        super().__init__()
        self.set_command_type("SpawnAsset")

        location = [0.0, 0.0, 0.0] if location is None else location
        rotation = [0.0, 0.0, 0.0] if rotation is None else rotation
        scale = [1.0, 1.0, 1.0] if scale is None else scale

        self.add_number_parameters(location)
        self.add_number_parameters(rotation)
        self.add_number_parameters(scale)
        self.add_string_parameters(mesh_asset_path)
        self.add_string_parameters(label)
        self.add_string_parameters(units)


def load_population(population_json_path):
    """Load spawn metadata and entries from a population JSON file."""

    if not population_json_path.exists():
        print(f"[population] file not found: {population_json_path}")
        return {}, []
    data = json.loads(population_json_path.read_text(encoding="utf-8"))
    spawns = data.get("spawns", [])
    if not isinstance(spawns, list):
        raise ValueError("Invalid population JSON: 'spawns' must be a list")
    return data.get("metadata", {}), spawns


def _offset_location(location, z_offset):
    loc = list(location) if isinstance(location, (list, tuple)) else [0.0, 0.0, 0.0]
    while len(loc) < 3:
        loc.append(0.0)
    loc = loc[:3]
    loc[2] = float(loc[2]) + float(z_offset)
    return [float(loc[0]), float(loc[1]), float(loc[2])]


def _scale_vector(scale, multiplier):
    vec = list(scale) if isinstance(scale, (list, tuple)) else [1.0, 1.0, 1.0]
    while len(vec) < 3:
        vec.append(vec[-1] if vec else 1.0)
    vec = vec[:3]
    m = float(multiplier)
    return [float(vec[0]) * m, float(vec[1]) * m, float(vec[2]) * m]


def _average_scale(scale):
    vec = list(scale) if isinstance(scale, (list, tuple)) else [1.0, 1.0, 1.0]
    while len(vec) < 3:
        vec.append(vec[-1] if vec else 1.0)
    return sum(abs(float(v)) for v in vec[:3]) / 3.0


def _category_overlap_radius(category, scale):
    category = normalize_category(category)
    avg_scale = _average_scale(scale)
    base_radius = {
        "seaweed": 26.0,
        "giant_seaweed": 34.0,
        "rocks": 30.0,
        "coral": 18.0,
        "mine": 85.0,
        "torpedo": 42.0,
        "anchor": 48.0,
    }.get(category, 24.0)
    scale_factor = {
        "seaweed": 1.4,
        "giant_seaweed": 1.8,
        "rocks": 1.0,
        "coral": 1.2,
        "mine": 0.55,
        "torpedo": 1.4,
        "anchor": 1.5,
    }.get(category, 0.8)
    return base_radius + avg_scale * scale_factor


def _apply_rotation_jitter(rotation, category, label):
    category = normalize_category(category)
    rot = list(rotation) if isinstance(rotation, (list, tuple)) else [0.0, 0.0, 0.0]
    while len(rot) < 3:
        rot.append(0.0)
    rot = [float(rot[0]), float(rot[1]), float(rot[2])]

    roll_jitter, pitch_jitter, yaw_jitter = {
        "seaweed": (6.0, 6.0, 10.0),
        "giant_seaweed": (8.0, 8.0, 12.0),
        "rocks": (10.0, 10.0, 8.0),
        "coral": (9.0, 9.0, 8.0),
        "mine": (7.0, 7.0, 5.0),
        "torpedo": (9.0, 9.0, 6.0),
        "anchor": (10.0, 10.0, 6.0),
    }.get(category, (4.0, 4.0, 0.0))

    seed = zlib.crc32(f"{category}:{label}".encode("utf-8"))
    rng = random.Random(seed)
    rot[0] = max(-35.0, min(35.0, rot[0] + rng.uniform(-roll_jitter, roll_jitter)))
    rot[1] = max(-35.0, min(35.0, rot[1] + rng.uniform(-pitch_jitter, pitch_jitter)))
    rot[2] = rot[2] + rng.uniform(-yaw_jitter, yaw_jitter)
    return [round(rot[0], 2), round(rot[1], 2), round(rot[2], 2)]


def _resolve_spawn_overlap(location, radius, placed_objects, label):
    loc = [
        float(location[0]),
        float(location[1]),
        float(location[2]),
    ]
    seed = zlib.crc32(f"overlap:{label}".encode("utf-8"))
    rng = random.Random(seed)

    for _ in range(12):
        push_x = 0.0
        push_y = 0.0
        max_penetration = 0.0
        overlapped = False

        for other in placed_objects[-240:]:
            dx = loc[0] - other["x"]
            dy = loc[1] - other["y"]
            min_sep = radius + other["radius"]
            dist2 = dx * dx + dy * dy
            if dist2 >= min_sep * min_sep:
                continue

            dist = dist2 ** 0.5
            overlapped = True
            if dist < 1e-3:
                angle = rng.uniform(0.0, math.tau)
                dx = math.cos(angle)
                dy = math.sin(angle)
                dist = 1.0

            penetration = min_sep - dist
            push_x += (dx / dist) * (penetration + 20.0)
            push_y += (dy / dist) * (penetration + 20.0)
            max_penetration = max(max_penetration, penetration)

        if not overlapped:
            return [round(loc[0], 2), round(loc[1], 2), round(loc[2], 2)]

        push_len = (push_x * push_x + push_y * push_y) ** 0.5
        if push_len < 1e-3:
            angle = rng.uniform(0.0, math.tau)
            push_x = math.cos(angle)
            push_y = math.sin(angle)
            push_len = 1.0

        step = max(25.0, max_penetration + 10.0)
        loc[0] += (push_x / push_len) * step
        loc[1] += (push_y / push_len) * step

    return [round(loc[0], 2), round(loc[1], 2), round(loc[2], 2)]


def spawn_population(
    env,
    spawns,
    batch_size=120,
    z_offset=150.0,
    category_scale_multipliers=None,
    category_absolute_z=None,
    category_z_offsets=None,
    category_max_z=None,
):
    """Spawn a population in batches using category-specific placement rules."""

    if category_scale_multipliers is None:
        category_scale_multipliers = {}
    if category_absolute_z is None:
        category_absolute_z = {}
    if category_z_offsets is None:
        category_z_offsets = {}
    if category_max_z is None:
        category_max_z = {}

    total = len(spawns)
    queued = 0
    placed_objects = []

    for idx, entry in enumerate(spawns, start=1):
        category = normalize_category(entry.get("category", ""))
        label = entry.get("label", "")

        location = _offset_location(entry.get("location", [0.0, 0.0, 0.0]), z_offset)
        if category in category_absolute_z:
            location[2] = float(category_absolute_z[category])
        if category in category_z_offsets:
            location[2] += float(category_z_offsets[category])
        if category in category_max_z:
            location[2] = min(location[2], float(category_max_z[category]))

        scale_multiplier = float(category_scale_multipliers.get(category, 1.0))
        scale = _scale_vector(entry.get("scale", [1.0, 1.0, 1.0]), scale_multiplier)
        radius = _category_overlap_radius(category, scale)
        location = _resolve_spawn_overlap(location, radius, placed_objects, label)
        rotation = _apply_rotation_jitter(
            entry.get("rotation", [0.0, 0.0, 0.0]),
            category,
            label,
        )

        env._enqueue_command(
            SpawnAssetCommand(
                mesh_asset_path=entry["mesh_asset"],
                location=location,
                rotation=rotation,
                scale=scale,
                label=label,
                units=entry.get("units", "ue"),
            )
        )
        placed_objects.append({"x": location[0], "y": location[1], "radius": radius})
        queued += 1

        if queued >= batch_size:
            env.tick(publish=False)
            queued = 0
            if idx % (batch_size * 10) == 0 or idx == total:
                print(f"[population] spawned {idx}/{total}")

    if queued > 0:
        env.tick(publish=False)


CATEGORY_DISPLAY_NAMES = {
    "seaweed": "seaweed",
    "giant_seaweed": "giant_seaweed",
    "rocks": "rocks",
    "coral": "coral",
    "mine": "mine",
    "torpedo": "torpedo",
    "anchor": "anchor",
}


def _format_category_dict(values):
    """Format internal category keys for user-facing logs."""

    formatted = {}
    for key, value in values.items():
        normalized = normalize_category(key)
        display = CATEGORY_DISPLAY_NAMES.get(normalized, normalized)
        formatted[display] = formatted.get(display, 0) + value
    return formatted


SCENARIO_CONFIG = {
    "name": "world_population_test",
    "world": "ExampleLevel",
    "main_agent": "auv0",
    "agents": [
        {
            "agent_name": "auv0",
            "agent_type": "HoveringAUV",
            "sensors": [{"sensor_type": "LocationSensor"}],
            "control_scheme": 0,
            "location": [0, 0, 1],
        }
    ],
}


_ROOT_DIR = Path(__file__).resolve().parent
_CONFIG_DIR = _ROOT_DIR / "engine" / "Content" / "Config"
_DEFAULT_POPULATION_JSON_PATH = _CONFIG_DIR / "world_population.json"
_LEGACY_POPULATION_JSON_PATH = _ROOT_DIR / "world_population.json"
_EXAMPLE_POPULATION_JSON_PATH = _ROOT_DIR / "world_population.json.example"

if _DEFAULT_POPULATION_JSON_PATH.exists():
    POPULATION_JSON_PATH = _DEFAULT_POPULATION_JSON_PATH
elif _LEGACY_POPULATION_JSON_PATH.exists():
    POPULATION_JSON_PATH = _LEGACY_POPULATION_JSON_PATH
else:
    POPULATION_JSON_PATH = _EXAMPLE_POPULATION_JSON_PATH
POPULATION_BATCH_SIZE = 120
POPULATION_Z_OFFSET = 120.0
CATEGORY_SCALE_MULTIPLIERS = {
    "seaweed": 11.0,
    "giant_seaweed": 11.0,
    "rocks": 10.0,
    "coral": 10.0,
    "mine": 0.9,
    "torpedo": 0.6666667,
    "anchor": 0.6666667,
}
CATEGORY_ABSOLUTE_Z = {
    "coral": -7150.0,
    "seaweed": -7150.0,
    "giant_seaweed": -7150.0,
}
CATEGORY_Z_OFFSETS = {
    "mine": 1100.0,
    "rocks": -120.0,
}
CATEGORY_MAX_Z = {
    "mine": -3600.0,
    "anchor": -3900.0,
}


def main():
    """Attach to a live Unreal session and spawn the configured population."""

    if POPULATION_JSON_PATH == _EXAMPLE_POPULATION_JSON_PATH:
        print(
            "[population] using public example dataset "
            f"{_EXAMPLE_POPULATION_JSON_PATH}"
        )

    metadata, spawns = load_population(POPULATION_JSON_PATH)
    print(
        f"[population] loading {len(spawns)} assets from "
        f"{POPULATION_JSON_PATH} | counts={_format_category_dict(metadata.get('counts', {}))}"
    )
    print(
        f"[population] spawn config: batch_size={POPULATION_BATCH_SIZE}, "
        f"z_offset={POPULATION_Z_OFFSET}, "
        f"scale_multipliers={_format_category_dict(CATEGORY_SCALE_MULTIPLIERS)}, "
        f"absolute_z={_format_category_dict(CATEGORY_ABSOLUTE_Z)}, "
        f"category_z_offsets={_format_category_dict(CATEGORY_Z_OFFSETS)}, "
        f"category_max_z={_format_category_dict(CATEGORY_MAX_Z)}"
    )

    zero_command = [0, 0, 0, 0, 0, 0, 0, 0]
    with holoocean.make(scenario_cfg=SCENARIO_CONFIG, start_world=False) as env:
        spawn_population(
            env,
            spawns,
            batch_size=POPULATION_BATCH_SIZE,
            z_offset=POPULATION_Z_OFFSET,
            category_scale_multipliers=CATEGORY_SCALE_MULTIPLIERS,
            category_absolute_z=CATEGORY_ABSOLUTE_Z,
            category_z_offsets=CATEGORY_Z_OFFSETS,
            category_max_z=CATEGORY_MAX_Z,
        )
        print("[population] spawn completed")
        for _ in range(1000):
            env.step(zero_command)


if __name__ == "__main__":
    main()
