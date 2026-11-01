"""Exercise the real frontend's argument/error paths without requiring a display."""
import argparse
import configparser
import os
from pathlib import Path
import subprocess
import tempfile


def isolated_settings_environment(directory):
    """Keep subprocess settings and desktop caches away from the real profile."""
    directory = Path(directory)
    home = directory / "profile"
    home.mkdir(parents=True, exist_ok=True)
    return dict(os.environ, HOME=str(home), USERPROFILE=str(home),
                XDG_CONFIG_HOME=str(directory / "xdg"),
                LOCALAPPDATA=str(directory / "localappdata"))


def assert_defaults(path):
    values = configparser.ConfigParser()
    values.read_string(path.read_text(encoding="utf-8-sig"))
    assert values.getint("display", "scale") == 1, "New settings must default to native scale"
    assert not values.getboolean("display", "fullscreen"), "New settings must default to windowed"
    assert values.getboolean("audio", "enabled"), "New settings must enable audio"
    assert values.getint("audio", "volume") == 100, "New settings must default to full volume"
    assert values.getboolean("input", "enabled"), "New settings must enable controllers"
    assert values.getint("input", "deadzone") == 25, "New settings must default to 25 percent stick deadzone"
    assert values.getint("input", "player1") == 0 and values.getint("input", "player2") == 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator", type=Path, required=True)
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    with tempfile.TemporaryDirectory(prefix="drivonix frontend ") as temporary:
        directory = Path(temporary)
        environment = isolated_settings_environment(directory)
        environment.pop("DISPLAY", None)
        environment.pop("WAYLAND_DISPLAY", None)
        profile_base = Path(environment["LOCALAPPDATA" if os.name == "nt" else "XDG_CONFIG_HOME"])
        default_config = profile_base / "drivonix/drivonix.ini"

        def check(arguments, status, message, env=None):
            result = subprocess.run([emulator, *arguments], cwd=directory,
                                    env=environment if env is None else env,
                                    capture_output=True, text=True, encoding="utf-8", timeout=10)
            output = result.stdout + result.stderr
            if result.returncode != status or message not in output:
                raise AssertionError(f"{arguments}: status={result.returncode}, output={output!r}")
            return result

        def path_check(arguments, expected, env=None):
            result = check([*arguments, "--print-config-path"], 0, "", env)
            assert Path(result.stdout.strip()).resolve() == expected.resolve(), result.stdout

        check(["--help"], 0, "Usage:")
        check(["-h"], 0, "file picker")
        check(["one.bin", "two.bin"], 2, "Usage:")
        check(["--unknown"], 2, "Usage:")
        check(["--config"], 2, "Usage:")
        check(["--archive-member"], 2, "Usage:")
        check(["--list-archive"], 2, "Usage:")
        check(["--archive-member", "one.bin", "--archive-member", "two.bin", "file.zip"], 2, "Usage:")
        never_written = directory / "must-not-create/drivonix.ini"
        check(["--config", str(never_written), "--help"], 0, "Usage:")
        check(["--config", str(never_written), "--unknown"], 2, "Usage:")
        check(["--config", str(never_written), "--portable"], 2, "Usage:")
        assert not never_written.exists(), "Argument/help paths wrote explicit settings"
        assert not default_config.exists(), "Argument/help paths wrote user settings"

        path_check([], default_config)
        custom_path = directory / "custom settings/drivonix.ini"
        path_check(["--config", "custom settings/drivonix.ini"], custom_path)
        assert not default_config.exists() and not custom_path.exists(), "Printing a path created settings"
        portable_path = Path(emulator).parent / "config/drivonix.ini"
        portable_existed = portable_path.exists()
        path_check(["--portable"], portable_path)
        assert portable_path.exists() == portable_existed, "Printing a portable path created settings beside the binary"

        if os.name != "nt":
            relative_xdg = dict(environment, XDG_CONFIG_HOME="relative-settings")
            home_fallback = Path(environment["HOME"]) / ".config/drivonix/drivonix.ini"
            path_check([], home_fallback, relative_xdg)
            assert not home_fallback.exists() and not (directory / "relative-settings").exists(), \
                "Relative XDG path must be ignored without writing"

        # Reading settings precedes ROM loading, so this proves actual default
        # creation on both operating systems without opening a native window.
        check(["missing.bin"], 1, "Cannot open ROM")
        assert_defaults(default_config)
        unicode_base = directory / "profile-é-東京"
        unicode_environment = dict(environment, XDG_CONFIG_HOME=str(unicode_base),
                                   LOCALAPPDATA=str(unicode_base))
        unicode_default = unicode_base / "drivonix/drivonix.ini"
        path_check([], unicode_default, unicode_environment)
        assert not unicode_default.exists(), "Printing a Unicode profile path wrote settings"
        check(["missing.bin"], 1, "Cannot open ROM", unicode_environment)
        assert_defaults(unicode_default)

        unicode_explicit = directory / "personal-é-東京/drivonix.ini"
        check(["--config", str(unicode_explicit), "missing.bin"], 1, "Cannot open ROM")
        assert_defaults(unicode_explicit)
        check(["--config", str(custom_path), "missing.bin"], 1, "Cannot open ROM")
        assert_defaults(custom_path)

        preserved = (b"; hand edited settings\r\n[display]\r\nscale=2\r\nfullscreen=false\r\n"
                     b"[audio]\r\nenabled=true\r\nvolume=37\r\n[future]\r\nlayout=keep\r\n")
        custom_path.write_bytes(preserved)
        check(["--config", str(custom_path), "--no-audio", "missing.bin"], 1, "Cannot open ROM")
        assert custom_path.read_bytes() == preserved, "Session audio override rewrote the INI"

        for section, key, value in (("display", "scale", "99"), ("display", "fullscreen", "perhaps"),
                                    ("audio", "volume", "101")):
            invalid = f"[{section}]\n{key}={value}\n".encode()
            custom_path.write_bytes(invalid)
            path_check(["--config", str(custom_path)], custom_path)
            result = check(["--config", str(custom_path), "missing.bin"], 1, "settings line 2")
            assert section + "." + key in result.stderr, "Settings error omitted the failing key"
            assert custom_path.read_bytes() == invalid, "Invalid settings were overwritten"

        check(["--no-audio", "missing.bin"], 1, "Cannot open ROM")
        check([""], 1, "ROM path is empty")
        check(["missing.bin"], 1, "Cannot open ROM")
        check(["no-extension"], 1, "Unsupported ROM extension")
        check(["unsupported.txt"], 1, "Unsupported ROM extension")
        (directory / "truncated.bin").write_bytes(b"short")
        check(["truncated.bin"], 1, "complete header")
        (directory / "partial.smd").write_bytes(bytes(513))
        check(["partial.smd"], 1, "complete 16 KiB blocks")
        (directory / "oversized.bin").write_bytes(bytes(0x800001))
        check(["oversized.bin"], 1, "8 MiB")
        if os.name != "nt":
            # A valid, uppercase filename in an unrelated working directory gets
            # through the real loader and asset lookup before rejecting no display.
            (directory / "valid ROM.BIN").write_bytes(bytes(0x400))
            check(["valid ROM.BIN"], 1, "X11 display")
            check([], 1, "Cannot open the ROM picker")
    print("Frontend arguments, isolated INI paths/creation, Unicode settings, invalid ROMs and startup errors passed")


if __name__ == "__main__":
    main()
