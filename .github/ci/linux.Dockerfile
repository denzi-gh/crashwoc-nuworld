FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y wget lsb-release software-properties-common gnupg cmake ninja-build git python3 \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
    libxtst-dev libxkbcommon-dev libwayland-dev libegl1-mesa-dev libgl1-mesa-dev \
    libdrm-dev libgbm-dev libasound2-dev libpulse-dev libpipewire-0.3-dev \
    libdbus-1-dev libudev-dev libibus-1.0-dev
RUN wget -q https://apt.llvm.org/llvm.sh && bash llvm.sh 22 && apt-get install -y libclang-rt-22-dev
ENV PATH=/usr/lib/llvm-22/bin:$PATH
RUN pip3 --version || apt-get install -y python3-pip
RUN apt-get update && apt-get install -y python3.11 python3.11-venv ccache && python3.11 -m venv /opt/py
COPY requirements.txt /tmp/requirements.txt
RUN /opt/py/bin/pip install -q -r /tmp/requirements.txt cmake
ENV PATH=/opt/py/bin:$PATH
