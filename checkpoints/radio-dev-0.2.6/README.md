# 0.2.6 精确构建检查点

源码提交、四个镜像大小及 SHA256 见 `manifest.json`。`xiaozhi.elf`、`xiaozhi.map`、`build-sdkconfig.txt` 对应这一次构建，供另一台电脑定位内存及堆栈。

此应用已刷入所选工牌，保留区写后核对通过；用户确认已有声音，但有间歇停顿，当前版尚未得到流畅度验收。它不是正式 Release。

现有工牌保留了自己的兼容 bootloader，字节不同于这里的构建 bootloader。刷写前必须阅读根目录 `HANDOFF.md`；不要把四个文件简单合并后写到 0x0，不要擦除 NVS。
