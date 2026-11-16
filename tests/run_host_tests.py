from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components" / "duepi"


def main():
    with tempfile.TemporaryDirectory(prefix="duepi-host-") as directory:
        build = Path(directory)
        fake_headers = (
            "core/component.h", "core/hal.h", "core/log.h", "core/preferences.h", "core/defines.h",
            "components/uart/uart.h", "components/sensor/sensor.h",
            "components/text_sensor/text_sensor.h", "components/binary_sensor/binary_sensor.h",
            "components/climate/climate.h", "components/number/number.h", "components/button/button.h",
            "components/switch/switch.h",
        )
        for header in fake_headers:
            destination = build / "esphome" / header
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text('#include "fake_esphome.h"\n', encoding="ascii")
        suites = {
            "core": ["protocol.cpp", "transaction.cpp"],
            "hub": ["protocol.cpp", "transaction.cpp", "duepi.cpp", "duepi_climate.cpp", "privileged.cpp"],
            "hub_without_installer": ["protocol.cpp", "transaction.cpp", "duepi.cpp", "duepi_climate.cpp", "privileged.cpp"],
        }
        for name, sources in suites.items():
            defines = [] if name == "hub_without_installer" else ["-DUSE_DUEPI_INSTALLER"]
            test_name = "hub" if name == "hub_without_installer" else name
            executable = build / (f"test_{name}.exe" if os.name == "nt" else f"test_{name}")
            subprocess.run(
                [sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-variable", "-pedantic", *defines, "-I", str(SOURCE), "-I", str(build),
                 "-I", str(ROOT / "tests"), *[str(SOURCE / source) for source in sources],
                 str(ROOT / "tests" / f"test_{test_name}.cpp"), "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()