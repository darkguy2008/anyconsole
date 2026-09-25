FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-pip libevdev2 libudev1 joystick && rm -rf /var/lib/apt/lists/*
RUN pip install --break-system-packages --no-cache-dir "hid-tools[uhid]==0.12"
COPY ds4.py /
ENTRYPOINT ["python3", "/ds4.py"]
