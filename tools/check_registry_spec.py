#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check the OA Registry Specification schemas against their examples.

The check is not a full JSON Schema validator. It parses every schema as
JSON, requires draft 2020-12 and a unique $id at the file's address in the
engine repository, and, for each example, requires every required key and
refuses a key outside properties where additionalProperties is false.
Objects are checked recursively. YAML examples are read with oamod_yaml,
the engine's Python twin of its YAML reader. catalogue.json.sig has no schema.
"""

import argparse
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oamod_yaml

DRAFT = "https://json-schema.org/draft/2020-12/schema"
ID_PREFIX = (
    "https://raw.githubusercontent.com/open-annihilation/open-annihilation/"
    "main/docs/registries/schemas/"
)

SCHEMA_FILES = (
    "descriptor.schema.json",
    "catalogue.schema.json",
    "registries-yaml.schema.json",
    "download-request.schema.json",
    "download-response.schema.json",
    "challenge.schema.json",
    "challenge-state.schema.json",
    "result.schema.json",
    "error.schema.json",
    "counts.schema.json",
)

# example file, schema file, yaml or json. The signature has no schema.
EXAMPLES = (
    ("registry.yaml", "descriptor.schema.json", "yaml"),
    ("example.oareg", "descriptor.schema.json", "yaml"),
    ("Registries.yaml", "registries-yaml.schema.json", "yaml"),
    ("catalogue.json", "catalogue.schema.json", "json"),
    ("download-request.json", "download-request.schema.json", "json"),
    ("download-response.json", "download-response.schema.json", "json"),
    ("challenge.json", "challenge.schema.json", "json"),
    ("challenge-state.json", "challenge-state.schema.json", "json"),
    ("result.json", "result.schema.json", "json"),
    ("error.json", "error.schema.json", "json"),
    ("counts.json", "counts.schema.json", "json"),
)
UNSCHEMATIZED = ("catalogue.json.sig",)


def resolve(schema, root):
    """Follows a local $ref. Any other schema is returned as it stands."""
    if isinstance(schema, dict) and "$ref" in schema:
        ref = schema["$ref"]
        if not isinstance(ref, str) or not ref.startswith("#/"):
            return schema
        node = root
        for part in ref[2:].split("/"):
            if not isinstance(node, dict) or part not in node:
                return schema
            node = node[part]
        return node
    return schema


def instance_problems(instance, schema, root, path):
    """Returns problems of required keys and additionalProperties, recursively."""
    schema = resolve(schema, root)
    if not isinstance(schema, dict):
        return []
    problems = []
    if isinstance(instance, list):
        item = schema.get("items")
        if isinstance(item, dict):
            for index, element in enumerate(instance):
                problems.extend(instance_problems(element, item, root, f"{path}[{index}]"))
        return problems
    if not isinstance(instance, dict):
        return problems
    properties = schema.get("properties")
    if not isinstance(properties, dict):
        properties = {}
    required = schema.get("required")
    if isinstance(required, list):
        for key in required:
            if key not in instance:
                problems.append(f"{path}: missing required '{key}'")
    if schema.get("additionalProperties") is False:
        for key in instance:
            if key not in properties:
                problems.append(f"{path}: extra property '{key}'")
    for key, value in instance.items():
        sub = properties.get(key)
        if isinstance(sub, dict):
            problems.extend(instance_problems(value, sub, root, f"{path}.{key}"))
    return problems


def schema_header_problems(name, schema, seen_ids):
    """Returns problems of $schema and $id for one schema object."""
    problems = []
    if schema.get("$schema") != DRAFT:
        problems.append(f"{name}: $schema is not draft 2020-12")
    ident = schema.get("$id")
    expected = ID_PREFIX + name
    if ident != expected:
        problems.append(f"{name}: $id is not {expected}")
    elif ident in seen_ids:
        problems.append(f"{name}: duplicate $id {ident}")
    else:
        seen_ids.add(ident)
    return problems


def load_json(path):
    """Returns (value, error). error is set when the file is not JSON."""
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return None, f"{path.name}: {error}"


def load_yaml(path):
    """Returns (value, error) using the engine's YAML reader."""
    try:
        return oamod_yaml.loads(path.read_bytes()), None
    except (OSError, UnicodeError, oamod_yaml.OamodYamlError) as error:
        return None, f"{path.name}: {error}"


def check_tree(root):
    """Returns every problem under a repository root."""
    schemas_dir = root / "docs" / "registries" / "schemas"
    examples_dir = root / "docs" / "registries" / "examples"
    problems = []
    present = sorted(path.name for path in schemas_dir.glob("*.json")) if schemas_dir.is_dir() else []
    if present != sorted(SCHEMA_FILES):
        problems.append(
            "schemas: expected "
            + ", ".join(SCHEMA_FILES)
            + " but found "
            + ", ".join(present)
        )
    seen_ids = set()
    loaded = {}
    for name in SCHEMA_FILES:
        path = schemas_dir / name
        if not path.is_file():
            continue
        value, error = load_json(path)
        if error:
            problems.append(error)
            continue
        if not isinstance(value, dict):
            problems.append(f"{name}: schema is not an object")
            continue
        problems.extend(schema_header_problems(name, value, seen_ids))
        loaded[name] = value
    if not examples_dir.is_dir():
        problems.append("examples: directory is missing")
        return problems
    found = sorted(path.name for path in examples_dir.iterdir() if path.is_file())
    expected = sorted([name for name, _, _ in EXAMPLES] + list(UNSCHEMATIZED))
    if found != expected:
        problems.append("examples: expected " + ", ".join(expected) + " but found " + ", ".join(found))
    for name, schema_name, kind in EXAMPLES:
        path = examples_dir / name
        schema = loaded.get(schema_name)
        if schema is None or not path.is_file():
            continue
        if kind == "yaml":
            instance, error = load_yaml(path)
        else:
            instance, error = load_json(path)
        if error:
            problems.append(error)
            continue
        problems.extend(instance_problems(instance, schema, schema, name))
    return problems


def header_of(name, schema):
    """Returns header problems, recording ids in a fresh set when name is new."""
    return schema_header_problems(name, schema, set())


def self_test():
    """Plants each failure the check must report, and returns 0 when all are found."""
    missed = []

    def expect(label, problems):
        if not problems:
            missed.append(label)

    base = {
        "type": "object",
        "additionalProperties": False,
        "required": ["a"],
        "properties": {"a": {}, "b": {"type": "object", "additionalProperties": False, "properties": {"c": {}}}},
    }
    expect("missing required", instance_problems({"b": {}}, base, base, "ex"))
    expect("extra property", instance_problems({"a": 1, "z": 2}, base, base, "ex"))
    expect("nested extra", instance_problems({"a": 1, "b": {"c": 1, "z": 2}}, base, base, "ex"))
    if instance_problems({"a": 1, "b": {"c": 1}}, base, base, "ex"):
        missed.append("good instance was refused")

    good_id = ID_PREFIX + "descriptor.schema.json"
    good = {"$schema": DRAFT, "$id": good_id}
    if header_of("descriptor.schema.json", good):
        missed.append("good schema header was refused")
    expect("bad $schema", header_of("descriptor.schema.json", {"$schema": "https://example.invalid", "$id": good_id}))
    expect(
        "wrong $id",
        header_of("descriptor.schema.json", {"$schema": DRAFT, "$id": ID_PREFIX + "other.schema.json"}),
    )
    seen = {good_id}
    expect(
        "duplicate $id",
        schema_header_problems("descriptor.schema.json", good, seen),
    )

    with tempfile.TemporaryDirectory() as tmp:
        bad = Path(tmp) / "bad.json"
        bad.write_text("{", encoding="utf-8")
        value, error = load_json(bad)
        expect("bad JSON", [error] if error else [])
        schema_path = Path(tmp) / "tiny.schema.json"
        # A YAML document missing a required key, checked the same way as an example.
        tiny = {
            "$schema": DRAFT,
            "$id": ID_PREFIX + "tiny.schema.json",
            "type": "object",
            "additionalProperties": False,
            "required": ["id"],
            "properties": {"id": {"type": "string"}},
        }
        yaml_path = Path(tmp) / "tiny.yaml"
        yaml_path.write_text("name: \"example\"\n", encoding="utf-8")
        instance, error = load_yaml(yaml_path)
        if error:
            missed.append("yaml example did not load: " + error)
        else:
            expect("YAML missing required", instance_problems(instance, tiny, tiny, "tiny.yaml"))
        schema_path.write_text(json.dumps(tiny), encoding="utf-8")

    if missed:
        print("self-test missed: " + "; ".join(missed))
        return 1
    print("registry-spec schemas self-test: ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    problems = check_tree(args.root)
    if problems:
        for problem in problems:
            print(problem)
        print(f"registry-spec schemas: {len(problems)} problem(s)")
        return 1
    print("registry-spec schemas: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
