# syntax=docker/dockerfile:1.6
FROM ubuntu:18.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PREFIX=/opt/sdl-static

RUN apt-get update && apt-get install -y \
  build-essential \
  gcc g++ make cmake pkg-config \
  git \
  curl \
  zip \
  autoconf automake libtool \
  nasm yasm \
  libasound2-dev libpulse-dev libaudio-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxinerama-dev libxi-dev \
  libgl1-mesa-dev libdrm-dev libgbm-dev \
  libfreetype6-dev libharfbuzz-dev \
  libpng-dev libjpeg-dev libtiff-dev libwebp-dev \
  libogg-dev libvorbis-dev libflac-dev libmpg123-dev \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/*

# Git LFS (recommended / reliable for Ubuntu 18.04)
RUN apt-get update && apt-get install -y --no-install-recommends \
  curl ca-certificates gnupg \
  && curl -s https://packagecloud.io/install/repositories/github/git-lfs/script.deb.sh | bash \
  && apt-get install -y --no-install-recommends git-lfs \
  && git lfs version \
  && git lfs install --system \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# ---- SDL2 ----
RUN git clone --branch release-2.30.10 --depth 1 https://github.com/libsdl-org/SDL.git SDL2 && \
  cd SDL2 && \
  ./configure \
  --prefix=$PREFIX \
  --enable-static \
  --disable-shared \
  --disable-joystick \
  --disable-haptic \
  --disable-sensor && \
  make -j$(nproc) && make install

# ---- SDL2_image ----
RUN git clone --branch release-2.8.2 --depth 1 https://github.com/libsdl-org/SDL_image.git SDL2_image && \
  cd SDL2_image && \
  ./autogen.sh && \
  ./configure \
  --prefix=$PREFIX \
  --enable-static \
  --disable-shared \
  --disable-imageio \
  --disable-avif && \
  make -j$(nproc) && make install

# ---- SDL2_mixer ----
RUN git clone --branch release-2.8.0 --depth 1 https://github.com/libsdl-org/SDL_mixer.git SDL2_mixer && \
  cd SDL2_mixer && \
  ./autogen.sh && \
  ./configure \
  --prefix=$PREFIX \
  --enable-static \
  --disable-shared \
  --disable-music-mod \
  --disable-music-opus && \
  make -j$(nproc) && make install

# ---- SDL2_ttf (tarball, NOT git) ----
RUN curl -L https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.20.2/SDL2_ttf-2.20.2.tar.gz \
  -o SDL2_ttf.tar.gz && \
  tar xf SDL2_ttf.tar.gz && \
  cd SDL2_ttf-2.20.2 && \
  ./configure \
  --prefix=$PREFIX \
  --enable-static \
  --disable-shared && \
  make -j$(nproc) && make install

ENV PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
ENV CFLAGS="-I$PREFIX/include"
ENV CXXFLAGS="-I$PREFIX/include"
ENV LDFLAGS="-L$PREFIX/lib"

WORKDIR /work
CMD ["/bin/bash"]
