# Offline tools for the circuit work: ngspice (tools/spice_check.py), poppler (reading the local manual scans), numpy/scipy, gcc.
# docker build -t p5tools tools/docker
# docker run --rm -u $(id -u):$(id -g) -v $PWD:/w -w /w p5tools python3 tools/spice_check.py
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends poppler-utils ngspice python3 python3-numpy python3-scipy python3-matplotlib tesseract-ocr gcc libc6-dev && rm -rf /var/lib/apt/lists/*
