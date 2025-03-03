#判断交叉编译工具链是否存在，使用aarch64-poky-linux- (gcc-12.3.0)
if [ ! -e "/opt/fsl-imx-xwayland/6.1-mickledore/environment-setup-armv8a-poky-linux" ]; then
    echo ""
    echo "请先安装正点原子ATK-DLIMX93开发板光盘A-基础资料->05、开发工具->01、交叉编译器 
->fsl-imx-xwayland-glibc-x86_64-imx-image-full-armv8a-imx93evk-toolchain-6.1-mickledore.sh"
    echo ""
fi

#使用Yocto SDK里的GCC 12.3.0交叉编译器编译出厂Linux源码,可不用指定ARCH等，直接执行Make
source /opt/fsl-imx-xwayland/6.1-mickledore/environment-setup-armv8a-poky-linux
#!/bin/bash
#编译前先清除
#make clean

#配置defconfig文件
make imx_v8_defconfig -j 16

#开始编译Image
#make menuconfig
make Image -j 16

#编译正点原子各种显示设备的设备树，若用户没有屏，启动时默认会选择imx93-11x11-atk.dtb加载
make freescale/imx93-11x11-atk.dtb
make freescale/imx93-11x11-atk-mipi-dsi-5.5-720x1280.dtb
make freescale/imx93-11x11-atk-mipi-dsi-5.5-1080x1920.dtb
make freescale/imx93-11x11-atk-mipi-dsi-10.1-800x1280.dtb
make freescale/imx93-11x11-atk-lvds-10.1-1280x800.dtb

#编译内核模块
make modules -j16

#在当前目录下新建一个tmp目录，用于存放编译后的目标文件
if [ ! -e "./tmp" ]; then
    mkdir tmp
fi
rm -rf tmp/*

#将编译好的模块安装到tmp 目录，通过INSTALL_MOD_STRIP=1 移除模块调试信息
make modules_install INSTALL_MOD_PATH=tmp INSTALL_MOD_STRIP=1 -j16

#删除模块目录下的source 目录
rm -rf tmp/lib/modules/6.1.55/source

#删除模块的目录下的build 目录
rm -rf tmp/lib/modules/6.1.55/build

#跳转到模块目录
cd tmp/lib/modules

#压缩内核模块
tar -jcvf ../../modules.tar.bz2 .
cd -
rm -rf tmp/lib

#拷贝Image到tmp目录下
cp arch/arm64/boot/Image tmp

#拷贝所有编译的设备树文件到当前的tmp目录下
cp arch/arm64/boot/dts/freescale/imx93-11x11-atk*.dtb tmp
echo "编译完成，请查看当前目录下的tmp文件夹中编译好的目标文件"

