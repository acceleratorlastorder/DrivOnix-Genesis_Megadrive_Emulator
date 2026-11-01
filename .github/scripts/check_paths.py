"""Keep tracked project paths lowercase, with CMake's required filename."""
from pathlib import Path, PurePosixPath
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode("utf-8").split("\0")
    invalid = []
    for name in filter(None, paths):
        path = PurePosixPath(name)
        parts = path.parts[:-1] if path.name == "CMakeLists.txt" else path.parts
        if any(part != part.lower() for part in parts):
            invalid.append(name)
    if invalid:
        raise SystemExit("Project paths must be lowercase:\n" + "\n".join(invalid))
    print("Tracked paths are lowercase; CMakeLists.txt is the required filename exception")


if __name__ == "__main__":
    main()
