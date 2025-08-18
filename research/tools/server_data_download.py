#!/usr/bin/env python3
import os
import sys
import getpass
import stat
from pathlib import Path
import paramiko
import yaml


def connect_sftp(host, port, username, password, key_path):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    pkey = None

    if key_path:
        key_path = os.path.expanduser(key_path)
        for loader in (
            paramiko.RSAKey.from_private_key_file,
            getattr(paramiko, "Ed25519Key", None) and paramiko.Ed25519Key.from_private_key_file,
            paramiko.ECDSAKey.from_private_key_file,
        ):
            if loader:
                try:
                    pkey = loader(key_path, password=password)
                    break
                except Exception:
                    continue

    client.connect(
        hostname=host,
        port=port,
        username=username,
        password=(password if not pkey else None),
        pkey=pkey,
        look_for_keys=False,
        allow_agent=True,
    )
    return client.open_sftp()


def is_remote_dir(sftp, path):
    try:
        return stat.S_ISDIR(sftp.stat(path).st_mode)
    except Exception:
        return False


def iter_remote_files(sftp, root_remote, recursive):
    root_remote = root_remote.rstrip("/")

    def walk(dir_remote, rel_base):
        for entry in sftp.listdir_attr(dir_remote):
            remote_path = f"{dir_remote}/{entry.filename}"
            rel_path = rel_base / entry.filename
            if stat.S_ISDIR(entry.st_mode):
                if recursive:
                    yield from walk(remote_path, rel_path)
            else:
                yield remote_path, rel_path

    yield from walk(root_remote, Path("."))


def ensure_local_parent(path):
    path.parent.mkdir(parents=True, exist_ok=True)


def download_file(sftp, remote_path, local_path):
    size = sftp.stat(remote_path).st_size
    ensure_local_parent(local_path)
    print(f"[+] Downloading {remote_path} -> {local_path} ({size} bytes)")
    sftp.get(remote_path, str(local_path))


def main(config_path):
    with open(config_path, "r") as f:
        config = yaml.safe_load(f)

    host = config["ssh"]["host"]
    port = config["ssh"].get("port", 22)
    user = config["ssh"]["user"]
    key = config["ssh"].get("key")
    password = config["ssh"].get("password")

    if not password and not key:
        password = getpass.getpass(f"Password for {user}@{host}: ") or None

    remote_dir = config["paths"]["remote_dir"]
    local_dir = Path(config["paths"]["local_dir"]).expanduser().resolve()
    recursive = config.get("options", {}).get("recursive", False)

    local_dir.mkdir(parents=True, exist_ok=True)

    sftp = connect_sftp(host, port, user, password, key)

    if not is_remote_dir(sftp, remote_dir):
        print(f"[!] Remote directory not found: {remote_dir}")
        sys.exit(1)

    downloaded, skipped = 0, 0
    for remote_path, rel_path in iter_remote_files(sftp, remote_dir, recursive):
        local_path = local_dir / rel_path
        if local_path.exists():
            skipped += 1
            continue
        download_file(sftp, remote_path, local_path)
        downloaded += 1

    print(f"\n=== Summary ===")
    print(f"Downloaded: {downloaded}")
    print(f"Skipped:    {skipped}")
    print(f"Saved to:   {local_dir}")

    sftp.close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} config.yml")
        sys.exit(1)
    main(sys.argv[1])
