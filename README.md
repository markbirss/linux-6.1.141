# linux-6.1.141

```
git clone https://github.com/markbirss/linux-6.1.141.git
cd linux-6.1.141
mv .git dot_git
make rv1126b_luckfox_defconfig; \
#make menuconfig
make -j4;
#make -j4 Image modules modules_install;
make -j4 Image; \
make -j4 modules_prepare; \
make -j4 modules; \
sudo make -j4 modules_install; \
sudo depmod -a; \
sudo make -j4 INSTALL_HDR_PATH=/usr/src/linux-headers-6.1.141 headers_install; \
sudo nice make -j$(nproc) bindeb-pkg

```
