# 自组织 MAC V7：Beacon 与 QoS Data 地址长度的正交比较

V6 已冻结；本目录是独立 V7 副本。V7 保留 V6 的 64 节点拓扑、100 ms 超帧（10 ms 控制窗、90 ms 数据窗）、邻居学习、节点生命史及数据窗门控，只增加独立的 `--dataAddress=standard|short` 参数，并补充逐 MPDU 帧长实测。旧的 `experiments/` 和 `analysis/` 目录仅作为 V6 历史参考，不是 V7 正式实验入口。

| 方案 | `beaconAddress` | `dataAddress` |
|---|---|---|
| A | standard | standard |
| B | short | standard |
| C | standard | short |
| D | short | short |

短地址只改变 Beacon 和三地址 Ad hoc QoS Data 的 MAC 首部实际序列化。QoS Data 首部由 26 B 降至 14 B；帧控制、Duration、序列控制、QoS 控制、FCS、ACK/RTS/CTS、PHY 不改。地址压缩并非标准 802.11n 帧，不能与未经修改的 802.11n 电台互通。仿真内部仍保留 48 位 MAC 地址，以供 ns-3 的接收匹配和 ACK/BA 逻辑使用。

开启 A-MSDU 时，只压缩**外层 QoS Data 首部的三个地址**；每个 A-MSDU 子帧的源/目的地址仍为标准长度。因此节省量是每个 MPDU 12 B，不是每个聚合子帧 12 B。若未来也改子帧地址格式，需要另立实验并重新检查互通、定界和校验。

短编号使用预入网静态映射：节点 0–63 对应 1–64；`0xffff` 为广播，`0xfffe` 为公共 IBSS BSSID，0 保留。未注册或重复映射直接报错。仿真为了在 ns-3 帧编解码层还原地址，使用了一个全局映射；真实电台必须各自维护映射，不能照搬这个全局状态。**本版不模拟入网分配、映射维护、编号回收及各电台映射存储与空口开销**，所以报告中需把它们列为短地址方案的额外代价，不能把净收益解释为部署后的完整收益。

安装到 ns-3.47 根目录下的 `self-organizing-mac-v7` 后：

```bash
chmod +x self-organizing-mac-v7/setup.sh self-organizing-mac-v7/run.sh
./self-organizing-mac-v7/setup.sh
./self-organizing-mac-v7/run.sh --duration=10 --run=1 --controlFrame=beacon --beaconAddress=standard --dataAddress=short --controlContenders=8 --neighborEncoding=adaptive --density=sparse --dataQueuePolicy=defer --amsduBytes=0 --output=self-organizing-mac-v7/results/smoke-C
```

主要新证据：每次启动都对 Beacon 和 QoS Data 执行实际 `AddHeader/RemoveHeader` 序列化往返自检；`summary.csv`、`run-config.csv` 记录两个独立地址模式；`mpdu-size-events.csv` 逐次记录 PHY 开始发送时的实际 MPDU 长度；`frame-size-distribution.csv` 由这些逐 MPDU 样本汇总，不再把聚合帧的总字节除以 MPDU 数量后冒充单帧长度。该长度含 MAC 首部和 MPDU 的其他字节，比较同一业务条件下 A/C 或 B/D 的帧长差，不能把整个帧长误称为“首部字节”。

现阶段先做编解码和 A/B/C/D 冒烟验证，再进行无聚合配对实验，最后开启 2304 B A-MSDU 上限。所有结论要以实测结果为准。

正式矩阵的入口（默认 5 个种子、每组 15 秒、含脚本化拓扑事件）：

```bash
python3 self-organizing-mac-v7/experiments/run-v7-address-matrix.py \
  --runs=1,2,3,4,5 --duration=15 --amsduBytes=0 \
  --output=self-organizing-mac-v7/results/address-matrix-no-amsdu
python3 self-organizing-mac-v7/analysis/summarize-address-matrix.py \
  self-organizing-mac-v7/results/address-matrix-no-amsdu
```

该脚本使用固定随机流，并在事后逐条核对四种模式实际产生的业务需求是否完全相同；不一致时直接报错，不纳入配对结果。它没有实现外部流量文件回放，不能把这种“经核验的等价业务”写成“预生成流量回放”。`pairing-checks.csv` 保留需求序列的摘要值，`paired-comparison.csv` 给出按种子配对后的差值，而不是把所有包简单混在一起求差。
