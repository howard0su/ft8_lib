"""
Generate TXT from WAV using jt9 decoder
"""
import argparse
import contextlib
import pathlib
import shlex
import subprocess
@contextlib.contextmanager
def reencode(path):
    cmd = ['file', path]
    output = subprocess.check_output(cmd)
    freq = int(output.split( )[-2])
    if freq < 10000:
        tmp_path = pathlib.Path(".tmp.wav")
        cmd = shlex.split(f"sox {path} -r 12000 {tmp_path}")
        subprocess.check_call(cmd)
    else:
        tmp_path = path
    yield tmp_path
    if tmp_path != path:
        tmp_path.unlink()

def main(src_dir: pathlib.Path):
    for file in src_dir.glob("*.wav"):
        print(".", end="", flush=True)
        with reencode(file) as new_file:
            cmd = shlex.split(f"jt9 -d 2 -8 {new_file}")
            output = subprocess.check_output(cmd)
        txt_path = file.with_suffix(".txt")
        decoded = True
        with open(txt_path, "wb") as f:
            for line in output.splitlines(keepends=True)[:-1]:
                if b"EOF on input file" in line:
                    decoded = False
                    break
                f.write(line)
        if not decoded:
            txt_path.unlink()
    print("\nDone")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("src_dir", type=pathlib.Path)
    args = parser.parse_args()
    main(**vars(args))