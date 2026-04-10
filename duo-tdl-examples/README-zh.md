
# Milk-V Duo 系列 TDL-SDK 示例
[English](./README.md) | 简体中文

## TDL-SDK 简介

Cvitek 所提供的 TDL（Turnkey Deep Learning）集成算法，用以缩短应用程序开发所需的时间。此架构实现了 TDL 所需算法包含其前后处理， 提供统一且便捷的编程接口。

参考链接： [https://github.com/sophgo/tdl_sdk](https://github.com/sophgo/tdl_sdk)

## 本仓库简介

本仓库基于 [tdl_sdk](https://github.com/sophgo/tdl_sdk)，针对 Milk-V Duo 系列开发板创建，方便用户单独使用各示例进行验证测试，或者开发相关应用。

**该仓库的示例需在 V2 版本的固件中测试，镜像地址:** [Relese](https://github.com/milkv-duo/duo-buildroot-sdk-v2/releases)。

## 开发环境

使用本地的 Ubuntu 系统，推荐 `Ubuntu 22.04 LTS`。

也可以使用虚拟机中的 Ubuntu 系统、Windows 中 WSL 安装的 Ubuntu 或者基于 Docker 的 Ubuntu 系统。

安装编译依赖的工具：
```
sudo apt-get install wget git make
```

获取本仓库代码：
```
git clone https://github.com/milkv-duo/duo-tdl-examples.git
```

## 编译示例

以下以编译人脸检测的程序为例，介绍编译的过程。

进入代码目录：
```
cd duo-tdl-examples
```

加载编译环境：
```
source envsetup.sh
```
第一次加载会自动下载所需的编译工具链，下载后的目录名为`host-tools`，下次再加载编译环境时，会检测该目录，如果已存在则不会再次下载。

加载编译环境时需要按提示输入所需编译目标：
```
Select Product:
1. Duo (CV1800B)
2. Duo256M (SG2002) or DuoS (SG2000)
```
如果目标板是 Duo 则选择 `1`，如果目标板是 Duo256M 或者 DuoS 则选择 `2`。由于 Duo256M 和 DuoS 支持 RISCV 和 ARM 两种架构，还需要按提示继续选择：
```
Select Arch:
1. ARM64
2. RISCV64
Which would you like:
```
如果测试程序需要在 ARM 系统中运行，选择 `1`，如果是 RISCV 系统则选择 `2`。

**同一个终端中，只需要加载一次编译环境即可。**

人脸检测的示例程序为 `sample_vi_fd`，进入到 sample_vi_fd 目录：
```
cd sample_vi_fd
```
使用 `make` 命令编译：
```
make
```
将当前目录中生成的 `sample_vi_fd` 程序通过 scp 或者其他方式上传到 Duo 系列开发板中进行测试。如需清除编译生成的程序，可以执行 `make clean` 清除。

通过网络直接上传到 Duo 开发板中的程序，可能没有执行权限，需要先在开发板的系统里通过 `chmod` 命令添加可执行权限：
```
chmod +x sample_vi_fd
```

在开发板中测试该人脸检测示例的命令为 `sample_vi_fd + 人脸检测模型文件`，**注意 Duo 和 Duo256M/DuoS 中使用的模型是不同的**：

- Duo (CV180X)
  ```
  ./sample_vi_fd /mnt/cvimodel/scrfd_320_256_ir_0x.cvimodel
  ```

- Duo256M/DuoS (SG200X)
  ```
  ./sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel
  ```

此时将摄像头对准人脸，终端日志中会打印当前检测到人脸的数量。如需通过电脑实时预览视频画面，可参考 [人脸检测文档](https://milkv.io/zh/docs/duo/application-development/tdl-sdk/tdl-sdk-face-detection)。


## 示例文档

更多示例及详细使用说明，请参考 [Milk-V 文档](https://milkv.io/zh/docs/duo/application-development/tdl-sdk/tdl-sdk-introduction)。

## 模型支持列表

### TDL-SDK 数据类型

[https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/6_Data_Types.html](https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/6_Data_Types.html)

### TDK-SDK 模型下载

[https://github.com/sophgo/tdl_models/tree/main](https://github.com/sophgo/tdl_models/tree/main)

## 参考文档

<table>
<thead>
  <tr>
    <td>Chinese Version(中文版)</td>
    <td colspan="2">格式</td>
    <td>English Version</td>
    <td colspan="2">Format</td>
  </tr>
</thead>
<tbody>
  <tr>
    <td>深度学习SDK软件开发指南</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/TDL_SDK_Software_Development_Guide/build/TDLSDKSoftwareDevelopmentGuide_zh.pdf">pdf</a></td>
    <td>TDL SDK Software Development Guide</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/TDL_SDK_Software_Development_Guide/build/TDLSDKSoftwareDevelopmentGuide_en.pdf">pdf</a></td>
  </tr>
  <tr>
    <td>YOLO系列开发指南</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/YOLO_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/YOLO_Development_Guide/build/YOLODevelopmentGuide_zh.pdf">pdf</a></td>
    <td>YOLO Development Guide</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/YOLO_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/YOLO_Development_Guide/build/YOLODevelopmentGuide_en.pdf">pdf</a></td>
  </tr>
</tbody>
</table>

## 常见问题

### 该仓库和 Sophgo TDL SDK 仓库有何区别？

原始 [tdl_sdk](https://github.com/sophgo/tdl_sdk) 仓库的编译依赖固件SDK [duo-buildroot-sdk-v2](https://github.com/milkv-duo/duo-buildroot-sdk-v2)，编译过程稍复杂一些。该仓库将示例代码从 tdl_sdk 中分离出来，对于只关心 AI 程序开发的用户，可以直接编译测试。本仓库示例的源码与 tdl_sdk 是一致的，会定期同步 tdl_sdk 中相关代码的更新。

### libs 目录中的动态库 so 文件是否开源？

为了简化示例程序的编译，libs 目录中存放了示例所依赖的预编译动态库 so 文件，其中大部分的库的源码都可以在 [tdl_sdk](https://github.com/sophgo/tdl_sdk) 和 [cvi_mpi](https://github.com/sophgo/cvi_mpi) 中找到。每个库对应的源码文件请参考本仓库 libs 目录下的 README 说明。
