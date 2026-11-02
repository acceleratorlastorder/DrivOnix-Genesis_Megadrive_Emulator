"""Run the real frontend from a Unicode Windows installation and unrelated cwd.

Usage: python test_windows_install.py --application build/Release/application_tests.exe
       --probe build/Release/archive_tests.exe --report-dir C:/ascii/report

The default data source is the application's directory, where the DrivOnix
post-build step places assets and bios. --data-dir may name share/drivonix in a
staged package. --skip-application performs only the installed BIOS probe when
the full application acceptance has already run separately.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage(application, probe, data, installation):
    """Copy only application files into the normal bin/share package layout."""
    binary = installation / "bin"
    shared = installation / "share" / "drivonix"
    binary.mkdir(parents=True)
    shared.mkdir(parents=True)
    copies = {}
    for source in (application, probe):
        require(source.is_file(), f"Missing test executable: {source}")
        target = binary / source.name
        shutil.copy2(source, target)
        copies[str(target.relative_to(installation))] = digest(source)
    # Native Windows DLL loading must work from the copied executable folder;
    # the test does not add source-build directories to PATH.
    dlls = {}
    for parent in {application.parent, probe.parent}:
        for source in parent.iterdir():
            if source.is_file() and source.suffix.lower() == ".dll":
                key = source.name.lower()
                require(key not in dlls or digest(dlls[key]) == digest(source),
                        f"Conflicting adjacent runtime DLLs: {source.name}")
                dlls[key] = source
    require(dlls, "No adjacent runtime DLLs were available to stage")
    for key, source in dlls.items():
        target = binary / key
        shutil.copy2(source, target)
        copies[str(target.relative_to(installation))] = digest(source)
    required = ("bios/drivonix.bios", "assets/fonts/unifont.otf",
                "assets/fonts/unifont_upper.otf", "assets/fonts/manifest.json",
                "assets/branding.bmp")
    for relative in required:
        require((data / relative).is_file(), f"Missing built/installed data file: {data / relative}")
    shutil.copytree(data / "assets", shared / "assets")
    shutil.copytree(data / "bios", shared / "bios")
    if (data / "build-info.json").is_file():
        shutil.copy2(data / "build-info.json", shared / "build-info.json")
    for target in shared.rglob("*"):
        if target.is_file():
            relative = target.relative_to(shared)
            require(digest(target) == digest(data / relative), f"Staged asset bytes changed: {relative}")
            copies[str(target.relative_to(installation))] = digest(target)
    font_lock = json.loads((shared / "assets/fonts/manifest.json").read_text(encoding="utf-8"))
    for entry in font_lock["files"]:
        target = shared / "assets/fonts" / entry["path"]
        require(target.is_file() and target.stat().st_size == entry["size"]
                and digest(target) == entry["sha256"],
                f"Installed font/license failed its source lock: {entry['path']}")
    return binary / application.name, binary / probe.name, shared, copies


def run(command, cwd, environment, report_dir, stem, timeout):
    started = time.monotonic()
    # File streams keep a failure's complete trace without a pipe size limit or
    # Windows console-codepage conversion. The child emits UTF-8 bytes.
    stdout_path = report_dir / f"{stem}.stdout.txt"
    stderr_path = report_dir / f"{stem}.stderr.txt"
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        result = subprocess.run([str(item) for item in command], cwd=cwd, env=environment,
                                stdout=stdout, stderr=stderr, timeout=timeout, check=False)
    output = stdout_path.read_text(encoding="utf-8", errors="strict")
    errors = stderr_path.read_text(encoding="utf-8", errors="replace")
    require(result.returncode == 0,
            f"{stem} failed ({result.returncode}): {output[-2000:]} {errors[-4000:]}")
    return output, {"exit_code": result.returncode, "elapsed_seconds": round(time.monotonic() - started, 3),
                    "stdout": stdout_path.name, "stderr": stderr_path.name}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--data-dir", type=Path)
    parser.add_argument("--report-dir", required=True, type=Path)
    parser.add_argument("--skip-application", action="store_true")
    args = parser.parse_args()
    require(os.name == "nt", "The installed Windows acceptance test requires native Windows")
    application, probe = args.application.resolve(), args.probe.resolve()
    data = args.data_dir.resolve() if args.data_dir else application.parent
    report_dir = args.report_dir.resolve()
    require(str(report_dir).isascii(), "Use an ASCII external report directory; Unicode is tested in the installation and ROM paths")
    report_dir.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "cases": [], "full_application": not args.skip_application}
    try:
        with tempfile.TemporaryDirectory(prefix="drivonix-installed-") as temporary:
            root = Path(temporary)
            installation = root / "installation-é-日本語"
            app, installed_probe, shared, files = stage(application, probe, data, installation)
            report["installation"] = str(installation)
            report["files"] = files
            unrelated = root / "unrelated-working-directory"
            unrelated.mkdir()
            require(not list(unrelated.iterdir()), "The unrelated working directory must start empty")
            environment = os.environ.copy()
            environment["LOCALAPPDATA"] = str(root / "isolated-user-data")
            environment["XDG_DATA_HOME"] = str(root / "isolated-xdg-data")
            environment["XDG_CONFIG_HOME"] = str(root / "isolated-config")
            system = Path(environment.get("SystemRoot", "C:/Windows"))
            environment["PATH"] = os.pathsep.join((str(app.parent), str(system / "System32"), str(system)))
            output, case = run([installed_probe, "datafile", "unused"], unrelated, environment,
                               report_dir, "installed-bios", 20)
            expected = (shared / "bios/drivonix.bios").resolve()
            resolved = Path(output.strip()).resolve()
            require(resolved == expected and "日本語" in output,
                    f"BIOS lookup did not select the Unicode installed share directory: {output!r}")
            case.update(action="unicode_install_bios", resolved=str(resolved))
            report["cases"].append(case)
            if not args.skip_application:
                app_report = report_dir / "application"
                _, case = run([app, "--report-dir", app_report], unrelated, environment,
                              report_dir, "installed-application", 180)
                require((app_report / "result.txt").is_file(), "Installed application did not finish its acceptance report")
                for name in ("controller-paused.png", "controller-search-keyboard.png",
                             "first-indexed-library.png", "reopened-favorite-library.png"):
                    require((app_report / name).is_file(), f"Installed full UI did not capture {name}")
                case.update(action="unicode_install_full_application", report="application/result.txt")
                report["cases"].append(case)
            require(not list(unrelated.iterdir()), "Installed app wrote runtime files into an unrelated working directory")
            for relative, expected_hash in files.items():
                require(digest(installation / relative) == expected_hash,
                        f"Application changed an installed executable, asset, or license: {relative}")
            report["passed"] = True
    finally:
        (report_dir / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Windows Unicode installation BIOS and locked assets passed" +
          ("; full controller-driven application acceptance passed" if not args.skip_application else ""))


if __name__ == "__main__":
    main()
