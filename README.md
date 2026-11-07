# Plain Craft Launcher-download-Edition


极简的Minecraft下载器及实例管理器！（豆包最有用的一次）

本下载器为PCL 2.99.0关闭下载Minecraft入口而生！
使用了部分PCL 2的代码！

本下载器仅供下载Minecraft使用，无任何Minecraft源码，与mojang，微软和网易无关！
如果你是下载PCL时，下载到了这个下载器，请更换PCL官方！

使用了BMCLAPI（bmclapi2.bangbang93.com）！

## 功能

1. 下载全版本Minecraft java版！支持fabric、neoforge、forge以及OF（高清优化）！
2. 实例的打开与删除，一键搞定！
3. 下载前自定义实例名，实例管理页支持重命名！
4. 8线程并行下载，支持取消！

## 编译

需要 MinGW-w64 交叉编译器：

```bash
sudo apt install mingw-w64
./build.sh
```

产物：`PCL-Download-Edition.exe`（Windows x64 GUI）

## 依赖

- C++17
- Win32 API / WinHTTP
- nlohmann/json（头文件已包含在 `nlohmann/` 目录）
