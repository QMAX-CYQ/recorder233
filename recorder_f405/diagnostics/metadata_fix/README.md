# 索引短文本旧尾巴修复

用户保留三个索引文件、删除所有 Rb 日志后重新生成 Rb0/Rb1，实际收到：

| 文件 | 长度 | 原始字节 | 解释 |
|---|---:|---|---|
| create_seq.txt | 4 | 32 00 37 00 | 当前 2、NUL、旧尾巴 7、NUL |
| lastest.txt | 10 | 52 62 31 2E 74 78 74 00 74 00 | 当前 Rb1.txt、NUL、旧尾巴 t、NUL |
| oldest.txt | 8 | 52 62 30 2E 74 78 74 00 | 旧格式的 Rb0.txt，末尾 NUL |

根因：三个 Save 函数使用 FA_OPEN_ALWAYS 从文件头覆盖，却没有截短原文件，并把 strlen+1 的 NUL 写入磁盘。较长旧值变成较短新值时，旧尾部仍保留。2 和 Rb1.txt 与当前两个文件的索引状态相符，本次异常主要是文件内容格式残留。

## 修改

- WriteMetadataText 使用 FA_CREATE_ALWAYS 截短旧文件，仅写有效文本长度，不写 NUL；检查短写和 f_close 的同步结果。
- 三个索引统一保存纯文本加 CRLF，示例：create_seq 为 2\r\n，lastest 为 Rb1.txt\r\n，oldest 为 Rb0.txt\r\n。
- ReadMetadataText 为 NUL 保留一字节，检查读/关闭返回值，去除末尾换行；兼容旧 NUL 结尾文本。
- 修复 ReadIndexFromFile 原先读取 32 字节后再写 file_name[32] 的越界风险。
- LoadCreateSeq 拒绝负数、非数字尾巴和 uint32 溢出。

磁盘文件名仍使用既有 lastest.txt，保持兼容。本次未直接修改 SD 卡内容。新固件下次保存索引时会清除残留内容；已有 Rb0/Rb1 不需要删除。

本方案不是断电原子替换：若重建短文件过程中断电，索引可能为空或不完整，仍依赖已有日志文件头扫描恢复机制。本次未重构恢复算法或掉电一致性设计。

## 验证

独立 V6.22 工程编译链接 0 错误、0 警告，原工程 V6.24 配置保留。原生 C 测试提取实际保存/读取函数，用 FatFs 内存桩复现用户的两份脏文本，验证截短、无 NUL、旧格式读取、CRLF 解析、计数重置、uint32 边界、读取长度、短写/关闭/读取失败处理，全部通过。

备份为 recorder_before_fix.c.bak。尚未烧录或完成实卡删除后重启测试。
