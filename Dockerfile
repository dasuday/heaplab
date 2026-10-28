# A fallback environment for anyone without an x86-64 Linux machine (macOS, Windows without WSL2).
#
#   docker build --platform linux/amd64 -t heaplab .
#   docker run --rm -it --platform linux/amd64 --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
#              -v "$PWD":/heaplab heaplab
#
# --platform linux/amd64 matters on Apple Silicon: the project is about x86-64 Linux.
# --cap-add/--security-opt let gdb and strace work inside the container.
FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential gdb valgrind strace python3 git ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /heaplab
CMD ["bash"]
