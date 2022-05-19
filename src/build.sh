#!/usr/bin/bash

# install required packages
sudo apt update
sudo apt install -y cmake meson uuid-dev build-essential autoconf libtool pkg-config libaio-dev
sudo apt install -y infiniband-diags ibutils ibverbs-utils rdmacm-utils perftest \
  rdma-core mstflint libibverbs-dev librdmacm-dev libmlx5-1


# install nvme-cli
git clone https://github.com/linux-nvme/nvme-cli.git ~/nvme-cli
cd ~/nvme-cli
meson .build
ninja -C .build
sudo meson install -C .build

# install grpc
export MY_INSTALL_DIR=$HOME/.local
mkdir -p $MY_INSTALL_DIR
echo 'export PATH="~/.local/bin:$PATH"' | tee -a ~/.bashrc
source ~/.bashrc
git clone --recurse-submodules -b v1.45.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc ~/grpc
cd ~/grpc
mkdir -p cmake/build
pushd cmake/build
cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
      ../..
make -j8
make install
popd

git clone https://github.com/axboe/liburing.git ~/liburing
pushd ~/liburing
./configure
make -j8
sudo make install
popd

