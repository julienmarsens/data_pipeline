import os
import paramiko
from scp import SCPClient
import zipfile
import gzip
import shutil
from datetime import datetime
from ruamel.yaml import YAML

def get_internal_market_data():
    # Load configs
    yaml = YAML(typ="rt")
    yaml.preserve_quotes = True
    yaml.width = 10 ** 6

    with open('./common/config/local_path.yml', 'r') as f:
        local_config = yaml.load(f)

    with open('./common/config/server_config.yml', 'r') as f:
        server_config = yaml.load(f)

    # Remote server details
    HOST = server_config["market_data"]["host"]
    USER = server_config["market_data"]["user"]
    REMOTE_BASE = "/home/ubuntu/data"
    LOCAL_BASE = local_config["paths"]["data_path"]

    print(f"Connecting to {USER}@{HOST} ...")

    # Create SSH connection
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(HOST, username=USER)

    # List remote folders
    stdin, stdout, stderr = ssh.exec_command(f"ls -1 {REMOTE_BASE}")
    remote_folders = stdout.read().decode().splitlines()
    print("Remote folders found:", remote_folders)

    today = datetime.today().strftime("%Y-%m-%d")

    with SCPClient(ssh.get_transport()) as scp:
        for folder in remote_folders:
            if folder == today:
                print(f"Skipping {folder} (today)")
                continue

            remote_path = f"{REMOTE_BASE}/{folder}"
            local_folder = os.path.join(LOCAL_BASE, folder)

            if os.path.exists(local_folder):
                print(f"Skipping {folder}, already synced.")
                continue

            print(f"Syncing {folder}...")

            # Create the folder locally
            os.makedirs(local_folder, exist_ok=True)

            # Download the contents of the remote folder into the local_folder
            # Important: download into LOCAL_BASE to avoid double nesting
            scp.get(remote_path, local_path=LOCAL_BASE, recursive=True)

            # Now clean up: move files if necessary and unzip
            inner_folder = os.path.join(local_folder, folder)
            if os.path.exists(inner_folder):
                # Move files one level up
                for fname in os.listdir(inner_folder):
                    os.rename(os.path.join(inner_folder, fname),
                              os.path.join(local_folder, fname))
                os.rmdir(inner_folder)

            # Unzip .zip or .gz files and remove archives
            for fname in os.listdir(local_folder):
                fpath = os.path.join(local_folder, fname)

                if fname.endswith(".zip"):
                    with zipfile.ZipFile(fpath, 'r') as zip_ref:
                        zip_ref.extractall(local_folder)
                    os.remove(fpath)
                    print(f"Unzipped and removed {fname}.")

                elif fname.endswith(".gz"):
                    unzipped_path = os.path.splitext(fpath)[0]  # remove .gz
                    with gzip.open(fpath, 'rb') as f_in, open(unzipped_path, 'wb') as f_out:
                        shutil.copyfileobj(f_in, f_out)
                    os.remove(fpath)
                    print(f"Unzipped and removed {fname}.")

    ssh.close()
    print("Sync complete.")

# get_internal_market_data()