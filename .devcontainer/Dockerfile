# [Choice] Ubuntu version (use jammy or bionic on local arm64/Apple Silicon): jammy, focal, bionic
ARG VARIANT="bullseye"
FROM mcr.microsoft.com/vscode/devcontainers/base:0-${VARIANT}

# Use USTC mirror of Ubuntu
ADD sources.list /etc/apt/
# Install useful tools
RUN apt-get update && apt-get -y install tree vim tmux python3-pip gcc cmake build-essential git gdb linux-tools-generic
# Link perf
RUN ln -fs /usr/lib/linux-tools/*/perf /usr/bin/perf
