# fake-nvidia: test nvidia-container-toolkit without gpu device

别称：皮假客（由网友 `WWL=WWL+1` 命名）

## why

* we can use it to reproduce nvidia-container-toolkit vulnerability without gpu device, e.g.:
  * [cve-2025-23266](https://github.com/ssst0n3/docker_archive/tree/main/vul/cve-2025-23266)

## quick-start

```shell
apt update
apt install -y git build-essential linux-headers-$(uname -r)
git clone https://github.com/ssst0n3/fake-nvidia
cd fake-nvidia
make install
./fake-nvidia-device.sh
modprobe fake_nvidia_driver
```

## usage

start up a test environment without gpu device

```shell
$ git clone https://github.com/ssst0n3/docker_archive
$ cd docker_archive/docker/v28.3.2
$ docker compose -f docker-compose.yml -f docker-compose.kvm up -d
$ ./ssh
```

install fake-nvidia

```shell
root@localhost:~# apt update
root@localhost:~# apt install -y git curl build-essential linux-headers-$(uname -r)
root@localhost:~# git clone https://github.com/ssst0n3/fake-nvidia
root@localhost:~# cd fake-nvidia
root@localhost:~/fake-nvidia# make install
root@localhost:~/fake-nvidia# cd
root@localhost:~# systemctl start fake-nvidia-device
root@localhost:~# modprobe fake_nvidia_driver
root@localhost:~# lsmod |grep nvidia
fake_nvidia_driver     12288  0
root@localhost:~# ls -lah /usr/local/lib/libnvidia-ml.so.1
-rwxr-xr-x 1 root root 21K Jul 21 01:46 /usr/local/lib/libnvidia-ml.so.1
```

install nvidia-container-toolkit

```shell
root@localhost:~# curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg \
  && curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
    sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
    tee /etc/apt/sources.list.d/nvidia-container-toolkit.list
root@localhost:~# apt update
root@localhost:~# export NVIDIA_CONTAINER_TOOLKIT_VERSION=1.17.8-1
root@localhost:~# apt-get install -y \
      nvidia-container-toolkit=${NVIDIA_CONTAINER_TOOLKIT_VERSION} \
      nvidia-container-toolkit-base=${NVIDIA_CONTAINER_TOOLKIT_VERSION} \
      libnvidia-container-tools=${NVIDIA_CONTAINER_TOOLKIT_VERSION} \
      libnvidia-container1=${NVIDIA_CONTAINER_TOOLKIT_VERSION}
root@localhost:~# nvidia-ctk runtime configure --runtime=docker
root@localhost:~# systemctl restart docker
```

nvidia-container-toolkit runs on a fake-nvidia

```shell
root@localhost:~# nvidia-container-cli info
NVRM version:   535.104.05
CUDA version:   12.2

Device Index:   0
Device Minor:   0
Model:          NVIDIA Tesla T4
Brand:          Tesla
GPU UUID:       GPU-0-FAKE-UUID
Bus Location:   00000000:00:00.0
Architecture:   7.5

Device Index:   1
Device Minor:   1
Model:          NVIDIA Tesla T4
Brand:          Tesla
GPU UUID:       GPU-1-FAKE-UUID
Bus Location:   00000000:00:00.0
Architecture:   7.5

Device Index:   2
Device Minor:   2
Model:          NVIDIA Tesla T4
Brand:          Tesla
GPU UUID:       GPU-2-FAKE-UUID
Bus Location:   00000000:00:00.0
Architecture:   7.5

Device Index:   3
Device Minor:   3
Model:          NVIDIA Tesla T4
Brand:          Tesla
GPU UUID:       GPU-3-FAKE-UUID
Bus Location:   00000000:00:00.0
Architecture:   7.5
```

```shell
root@localhost:~# docker run -ti --runtime=nvidia --gpus=all busybox
/ # 
```
