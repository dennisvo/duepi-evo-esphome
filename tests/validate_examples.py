from pathlib import Path
import os
import shutil
import subprocess
import sys
import yaml

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    "read-only": ("false", "false"),
    "controls": ("true", "false"),
    "installer": ("true", "true"),
}


def field(mapping, name):
    return next(value for key, value in mapping.value if key.value == name)


def main():
    operation = sys.argv[1] if len(sys.argv) > 1 else "config"
    if operation not in ("config", "compile"):
        raise SystemExit("Expected config or compile")
    destination = ROOT / ".validation-build"
    destination.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "examples" / "secrets.yaml.example", destination / "secrets.yaml")
    names = sys.argv[2:] or list(VARIANTS)
    for name in names:
        if name not in VARIANTS:
            raise SystemExit(f"Unknown variant: {name}; choose {', '.join(VARIANTS)}")
        config_path = destination / f"duepi-{name}.yaml"
        writes, installer = VARIANTS[name]
        example = (ROOT / "examples" / "esp32-c3.yaml").read_text(encoding="utf-8")
        assert "substitutions:" not in example and "${" not in example
        config = yaml.compose(example)
        field(field(config, "esphome"), "name").value = f"duepi-{name}"
        field(field(config, "external_components").value[0], "source").value = "../components"
        field(field(config, "duepi"), "enable_writes").value = writes
        field(field(config, "duepi"), "enable_installer").value = installer
        config_path.write_text(yaml.serialize(config), encoding="utf-8")
        print(f"Validating {name}: {operation}", flush=True)
        result = subprocess.run(
            [sys.executable, "-m", "esphome", operation, str(config_path)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
        )
        log = destination / f"{name}.{operation}.log"
        log.write_text(result.stdout, encoding="utf-8")
        print("\n".join(result.stdout.splitlines()[-8:] if result.returncode == 0 else result.stdout.splitlines()[-60:]))
        print(f"Full log: {log}", flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)
        if operation == "compile":
            build = destination / ".esphome" / "build" / f"duepi-{name}"
            source = (build / "src" / "main.cpp").read_text(encoding="utf-8")
            defines = (build / "src" / "esphome" / "core" / "defines.h").read_text(encoding="utf-8")
            firmware = (build / "build" / "firmware.elf").read_bytes()
            assert "Arm One Custom Command" not in source
            assert b"Arm One Custom Command" not in firmware
            included = installer == "true"
            assert ("#define USE_DUEPI_INSTALLER" in defines) == included
            for entity in ("DuepiInstallerValue", "DuepiInstallerButton", "DuepiInstallerAPI", "Installer Access"):
                assert (entity in source) == included, (name, entity)
            for symbol in (b"Installer Access", b"_installer_write", b"_send_raw"):
                assert (symbol in firmware) == included, (name, symbol)
            print(f"PASS: installer implementation/entities/actions {'included' if included else 'excluded'}", flush=True)


if __name__ == "__main__":
    main()