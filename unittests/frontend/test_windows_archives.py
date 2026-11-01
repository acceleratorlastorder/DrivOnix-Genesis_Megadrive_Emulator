"""Exercise the real Win32 archive chooser before any SFML/OpenGL startup."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import zipfile

from test_cli import isolated_settings_environment


class NativeUi:
    TITLE = "Select a game from archive"
    LIST = 100
    WM_COMMAND = 0x0111
    LB_SETCURSEL = 0x0186
    LB_GETCURSEL = 0x0188
    LB_GETTEXT = 0x0189
    LB_GETTEXTLEN = 0x018A
    LB_GETCOUNT = 0x018B

    def __init__(self):
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        declarations = {
            "EnumWindows": ([self.callback_type, wintypes.LPARAM], wintypes.BOOL),
            "GetWindowThreadProcessId": ([wintypes.HWND, ctypes.POINTER(wintypes.DWORD)], wintypes.DWORD),
            "GetWindowTextW": ([wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
            "GetClassNameW": ([wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
            "IsWindowVisible": ([wintypes.HWND], wintypes.BOOL),
            "GetDlgItem": ([wintypes.HWND, ctypes.c_int], wintypes.HWND),
            "PostMessageW": ([wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM], wintypes.BOOL),
            "SendMessageTimeoutW": ([wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                                      wintypes.LPARAM, wintypes.UINT, wintypes.UINT,
                                      ctypes.POINTER(ctypes.c_size_t)], ctypes.c_ssize_t),
        }
        for name, (arguments, result) in declarations.items():
            function = getattr(self.user, name)
            function.argtypes = arguments
            function.restype = result

    def text(self, window, function="GetWindowTextW"):
        buffer = ctypes.create_unicode_buffer(1024)
        getattr(self.user, function)(window, buffer, len(buffer))
        return buffer.value

    def find_dialog(self, process):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if process.poll() is not None:
                stdout, stderr = process.communicate()
                raise AssertionError(f"Frontend exited before native chooser: {process.returncode}, {stdout!r}, {stderr!r}")
            matches = []

            @self.callback_type
            def inspect(window, unused):
                owner = wintypes.DWORD()
                self.user.GetWindowThreadProcessId(window, ctypes.byref(owner))
                if (owner.value == process.pid and self.user.IsWindowVisible(window)
                        and self.text(window) == self.TITLE
                        and self.text(window, "GetClassNameW") == "#32770"):
                    matches.append(window)
                return True

            if not self.user.EnumWindows(inspect, 0):
                raise ctypes.WinError(ctypes.get_last_error())
            if matches:
                assert len(matches) == 1, f"Ambiguous chooser windows: {matches}"
                return matches[0]
            time.sleep(0.05)
        raise AssertionError("Native archive chooser did not appear within 20 seconds")

    def message(self, window, message, wparam=0, lparam=0):
        result = ctypes.c_size_t()
        # A hung UI must fail the test rather than block the CI process.
        if not self.user.SendMessageTimeoutW(window, message, wparam, lparam,
                                            0x0003, 3000, ctypes.byref(result)):
            raise ctypes.WinError(ctypes.get_last_error())
        return ctypes.c_ssize_t(result.value).value

    def members(self, dialog):
        listing = self.user.GetDlgItem(dialog, self.LIST)
        assert listing and self.text(listing, "GetClassNameW").upper() == "LISTBOX", \
            "Chooser did not create its native game list"
        count = self.message(listing, self.LB_GETCOUNT)
        assert count == 2, f"Chooser must expose both eligible games, got {count}"
        names = []
        for row in range(count):
            length = self.message(listing, self.LB_GETTEXTLEN, row)
            assert 0 <= length <= 32768, f"Invalid member label length: {length}"
            buffer = ctypes.create_unicode_buffer(length + 1)
            copied = self.message(listing, self.LB_GETTEXT, row, ctypes.addressof(buffer))
            assert copied == length, "Native list label could not be read"
            names.append(buffer.value)
        assert names == ["first.bin", "second.smd"], f"Native chooser member order differs: {names!r}"
        return listing, names

    def command(self, dialog, command):
        if not self.user.PostMessageW(dialog, self.WM_COMMAND, command, 0):
            raise ctypes.WinError(ctypes.get_last_error())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, default=Path("build/windows-archive-report"))
    args = parser.parse_args()
    if os.name != "nt":
        raise SystemExit("This native chooser test requires Windows")
    emulator = str(args.emulator.resolve())
    probe = args.probe.resolve()
    report_dir = args.report_dir.resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    ui = NativeUi()
    report = {"platform": "windows", "cases": []}
    with tempfile.TemporaryDirectory(prefix="drivonix native archives ") as temporary:
        directory = Path(temporary)
        archive = directory / "two games.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
            package.writestr("first.bin", bytes(512))
            package.writestr("second.smd", bytes(513))
        digest = hashlib.sha256(archive.read_bytes()).digest()
        environment = isolated_settings_environment(directory)
        # Follow the real executable-directory lookup and ROM reader from a
        # Unicode installation, without initializing SFML or an OpenGL context.
        installation = directory / "installation-é-日本語" / "bin"
        installation.mkdir(parents=True)
        for executable in (Path(emulator), probe):
            assert executable.is_file(), f"Missing native test executable: {executable}"
            shutil.copy2(executable, installation / executable.name)
        for library in Path(emulator).parent.glob("*.dll"):
            shutil.copy2(library, installation / library.name)
        bios = Path(emulator).parent / "bios"
        assert (bios / "drivonix.bios").is_file(), "Built emulator BIOS fixture is missing"
        shutil.copytree(bios, installation / "bios")
        unrelated = directory / "unrelated working directory"
        unrelated.mkdir()
        resolved = subprocess.run([str(installation / probe.name), "datafile", "unused"],
                                  cwd=unrelated, env=environment, capture_output=True,
                                  text=True, encoding="utf-8", errors="strict", timeout=15)
        assert resolved.returncode == 0, \
            f"Unicode executable directory did not resolve and read BIOS: {resolved.stdout!r}, {resolved.stderr!r}"
        assert "日本語" in resolved.stdout, "BIOS lookup lost the Unicode executable directory"
        assert Path(resolved.stdout.strip()).resolve() == (installation / "bios/drivonix.bios").resolve(), \
            f"BIOS lookup selected the wrong installation: {resolved.stdout!r}"
        report["cases"].append({"action": "unicode_install_bios", "exit_code": resolved.returncode,
                                 "resolved": resolved.stdout.strip()})
        settings = directory / "settings/drivonix.ini"
        command = [emulator, "--config", str(settings), "--no-audio", str(archive)]
        baseline = None
        for selected in (False, True):
            process = subprocess.Popen(command, cwd=directory, env=environment,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding="utf-8", errors="replace")
            try:
                dialog = ui.find_dialog(process)
                listing, names = ui.members(dialog)
                if selected:
                    assert ui.message(listing, ui.LB_SETCURSEL, 1) == 1, "Second row selection failed"
                    assert ui.message(listing, ui.LB_GETCURSEL) == 1, "Native list did not retain selected row"
                ui.command(dialog, 1 if selected else 2)  # IDOK / IDCANCEL.
                stdout, stderr = process.communicate(timeout=15)
                expected = 1 if selected else 0
                assert process.returncode == expected, \
                    f"Chooser action status {process.returncode} != {expected}: {stdout!r}, {stderr!r}"
                if selected:
                    assert "complete 16 KiB" in stderr, \
                        f"Frontend did not load the exact selected second.smd before display startup: {stderr!r}"
                else:
                    assert not stderr, f"Cancelling archive chooser reported an error: {stderr!r}"
                report["cases"].append({"action": "open_second" if selected else "cancel",
                                         "members": names, "exit_code": process.returncode,
                                         "stderr": stderr})
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=10)
            assert settings.is_file(), "Isolated settings were not created"
            if baseline is None:
                baseline = settings.read_bytes()
            else:
                assert settings.read_bytes() == baseline, "Chooser/error path rewrote existing settings"
            assert hashlib.sha256(archive.read_bytes()).digest() == digest, "Archive was modified"
            assert not (directory / "first.bin").exists() and not (directory / "second.smd").exists(), \
                "Archive games were extracted into the working directory"
    report["passed"] = True
    (report_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Windows native archive chooser cancellation and exact second-member selection passed")


if __name__ == "__main__":
    main()
