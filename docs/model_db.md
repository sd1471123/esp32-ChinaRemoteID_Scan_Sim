# 机型库可信清单（FAA UAS DoC 官方序列号）

> 数据源：FAA UAS Declaration of Compliance（<https://uasdoc.faa.gov>），序列号快照 `data/faa_serial_2026-11-17.tsv`（516 条声明 / 5004 条序列号条目）。
> 本文档与固件 `main/crid_web.c` 的机型预设表由 `python tools/models_faa.py --doc` 从同一份快照生成，不要手工改表。

## 一、什么算「已核实」

官方登记的是**序列号本身**，不是一个「前缀字段」。所以本表的前缀一律由序列号推导，规则固定：

| 官方登记形态 | 推导方式 | 例子 |
| --- | --- | --- |
| `RANGE`（起止区间） | 取起点与终点的公共前缀 | `1581F45T000000000000-1581F45TZZZZZZZZZZZZ` → `1581F45T` |
| 序列号列表 | 全部值取公共前缀；不足 5 位时取前 8 位众数（占比 ≥60%） | `1668F002…`/`1668F000…` → `1668F00` |
| 单条序列号 | 取前 8 位，标记低置信 | `1587A540002APD` → `1587A540` |
| `RANGE` 起点 | 起点前 8 位作为合法截短（官方号段内更细的型号段） | `1748CHL7822390001` → `1748CHL7` |

反过来，下面这些一律不算官方依据：官方没登记该型号、官方登记了型号但没有序列号、
把型号代号当序列号前缀（`SDR21V1`、`SR47PCV`）、「厂商码 + 000/001 递增」这类凭空构造。
为了自测需要，这些条目仍以测试预设的形式收进固件（接收端展示时加 `-?-`，见第四、五节），但依然没有官方依据。

## 二、结果

| 判定 | 条数 |
| --- | --- |
| ✅ 已核实，已进入固件 | **90**（87 个前缀） |
| ⚠️ 第三方草稿库中前缀需修正 | 21 |
| ⚠️ 第三方草稿库中前缀可截短使用 | 5 |
| ❌ 官方无该型号登记 | 23 |
| 🧪 未核实，接收端展示加 `-?-` | 32 |

草稿库共 108 条：59 条与官方完全一致，21 条前缀需替换为官方值，5 条前缀可截短，23 条查无此型号。

## 三、已核实清单（有官方依据的条目）

### 消费级（19 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F0M6` | Mavic 2 Zoom | Mavic 2 Zoom | `1581F0M6000000000000-1581F0M6ZZZZZZZZZZZZ（区间）` |  |
| `1581F11V` | Phantom 4 Pro V2.0 | Phantom 4 Pro V2.0 | `1581F11V000000000000-1581F11VZZZZZZZZZZZZ（区间）` |  |
| `1581F163` | Mavic 2 Pro | Mavic 2 Pro | `1581F163000000000000-1581F163ZZZZZZZZZZZZ（区间）` |  |
| `1581F1WN` | Mavic Air 2 | Mavic Air 2 (MA2UE1N) | `1581F1WN000000000000-1581F1WNZZZZZZZZZZZZ（区间）` | 官方区分 MA2UE1N / MA2UE3W |
| `1581F3N3` | Mavic Air 2 | Mavic Air 2 (MA2UE3W) | `1581F3N3000000000000-1581F3N3ZZZZZZZZZZZZ（区间）` |  |
| `1581F3YT` | Air 2S | Air 2S | `1581F3YT000000000000-1581F3YTZZZZZZZZZZZZ（区间）` |  |
| `1581F45T` | Mavic 3 | Mavic 3 | `1581F45T000000000000-1581F45TZZZZZZZZZZZZ（区间）` |  |
| `1581F4QZ` | Mavic 3 Cine | Mavic 3 Cine | `1581F4QZ000000000000-1581F4QZZZZZZZZZZZZZ（区间）` |  |
| `1581F5YH` | Mini 3 | Mini 3 | `1581F5YH000000000000-1581F5YHZZZZZZZZZZZZ（区间）` |  |
| `1581F67P` | Mavic 3 Classic | Mavic 3 Classic | `1581F67P000000000000-1581F67PZZZZZZZZZZZZ（区间）` |  |
| `1581F67Q` | Mavic 3 Pro | Mavic 3 Pro | `1581F67Q000000000000-1581F67QZZZZZZZZZZZZ（区间）` |  |
| `1581F6L5` | Mini 3 Pro | Mini 3 Pro | `1581F6L5000000000000-1581F6L5ZZZZZZZZZZZZ（区间）` | 官方号段 1581F6L5… |
| `1581F6MK` | Mavic 3 Pro Cine | Mavic 3 Pro Cine | `1581F6MK000000000000-1581F6MKZZZZZZZZZZZZ（区间）` |  |
| `1581F6N8` | Air 3 | Air 3 | `1581F6N8000000000000-1581F6N8ZZZZZZZZZZZZ（区间）` |  |
| `1581F6Z9` | Mini 4 Pro | Mini 4 Pro | `1581F6Z9000000000000-1581F6Z9ZZZZZZZZZZZZ（区间）` |  |
| `1581F895` | Air 3S | Air 3S | `1581F895000000000000-1581F895ZZZZZZZZZZZZ（区间）` |  |
| `1581F8LQ` | Mavic 4 Pro (64GB) | Mavic 4 Pro (64 GB) | `1581F8LQ000000000000-1581F8LQZZZZZZZZZZZZ（区间）` |  |
| `1581F986` | Mavic 4 Pro (512GB) | Mavic 4 Pro (512 GB) | `1581F986000000000000-1581F986ZZZZZZZZZZZZ（区间）` |  |
| `1581F9DE` | Mini 5 Pro | Mini 5 Pro | `1581F9DE000000000000-1581F9DEZZZZZZZZZZZZ（区间）` |  |

### 穿越机（5 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F37Q` | FPV | FPV | `1581F37Q000000000000-1581F37QZZZZZZZZZZZZ（区间）` |  |
| `1581F4QW` | Avata | AVATA | `1581F4QW000000000000-1581F4QWZZZZZZZZZZZZ（区间）` | 官方写作 AVATA |
| `1581F6W8` | Avata 2 | Avata 2 | `1581F6W8000000000000-1581F6W8ZZZZZZZZZZZZ（区间）` |  |
| `1581FA8J` | Avata 360 | Avata 360 (DVN3XT) | `1581FA8J000000000000-1581FA8JZZZZZZZZZZZZ（区间）` |  |
| `1581FBLK` | Avata 360 | DJI Avata 360 (DVN3NT) | `1581FBLK000000000000-1581FBLKZZZZZZZZZZZZ（区间）` | 两个硬件版本共用显示名 |

### 行业级（21 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F0UY` | Phantom 4 RTK | Phantom 4 RTK | `1581F0UY000000000000-1581F0UYZZZZZZZZZZZZ（区间）` |  |
| `1581F1ZN` | Matrice 300 RTK | M300 RTK | `1581F1ZN000000000000-1581F1ZNZZZZZZZZZZZZ（区间）` |  |
| `1581F4BN` | Matrice 30 | M30 | `1581F4BN000000000000-1581F4BNZZZZZZZZZZZZ（区间）` |  |
| `1581F4GC` | Mavic 2 Enterprise Advanced | Mavic 2 Enterprise Advanced | `1581F4GC000000000000-1581F4GCZZZZZZZZZZZZ（区间）` |  |
| `1581F4Z4` | Inspire 3 | Inspire 3 | `1581F4Z4000000000000-1581F4Z4ZZZZZZZZZZZZ（区间）` |  |
| `1581F5BK` | Matrice 30T | M30T | `1581F5BK000000000000-1581F5BKZZZZZZZZZZZZ（区间）` |  |
| `1581F5BL` | Matrice 30 (机场版) | M30 Dock version | `1581F5BL000000000000-1581F5BLZZZZZZZZZZZZ（区间）` |  |
| `1581F5BM` | Matrice 30T (机场版) | M30T Dock version | `1581F5BM000000000000-1581F5BMZZZZZZZZZZZZ（区间）` |  |
| `1581F5FH` | Mavic 3E | M3E | `1581F5FH000000000000-1581F5FHZZZZZZZZZZZZ（区间）` |  |
| `1581F5FJ` | Mavic 3T | M3T | `1581F5FJ000000000000-1581F5FJZZZZZZZZZZZZ（区间）` |  |
| `1581F5FK` | Mavic 3M | M3M | `1581F5FK000000000000-1581F5FKZZZZZZZZZZZZ（区间）` |  |
| `1581F62H` | Matrice 30T | M30T (EU&KR) | `1581F62H000000000000-1581F62HZZZZZZZZZZZZ（区间）` | 欧韩版，与 M30T 同名 |
| `1581F6GK` | Matrice 350 RTK | M350 RTK | `1581F6GK000000000000-1581F6GKZZZZZZZZZZZZ（区间）` |  |
| `1581F6Q8` | Matrice 3TD | M3TD | `1581F6Q8000000000000-1581F6Q8ZZZZZZZZZZZZ（区间）` |  |
| `1581F6QA` | Matrice 3D | M3D | `1581F6QA000000000000-1581F6QAZZZZZZZZZZZZ（区间）` |  |
| `1581F7FV` | Matrice 4E | Matrice 4E | `1581F7FV000000000000-1581F7FVZZZZZZZZZZZZ（区间）` |  |
| `1581F7K3` | Matrice 4T | Matrice 4T | `1581F7K3000000000000-1581F7K3ZZZZZZZZZZZZ（区间）` |  |
| `1581F8DB` | Matrice 400 | Matrice 400 | `1581F8DB000000000000-1581F8DBZZZZZZZZZZZZ（区间）` |  |
| `1581F8HG` | Matrice 4TD | Matrice 4TD | `1581F8HG000000000000-1581F8HGZZZZZZZZZZZZ（区间）` |  |
| `1581F8HH` | Matrice 4D | Matrice 4D | `1581F8HH000000000000-1581F8HHZZZZZZZZZZZZ（区间）` |  |
| `1581FBEX` | Matrice 3TA | M3TA | `1581FBEX000000000000-1581FBEXZZZZZZZZZZZZ（区间）` |  |

### 植保（8 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F446` | Agras T10 | Agras T10 | `1581F446000000000000-1581F446ZZZZZZZZZZZZ（区间）` | T10 / T30 同号段 |
| `1581F446` | Agras T30 | Agras T30 | `1581F446000000000000-1581F446ZZZZZZZZZZZZ（区间）` |  |
| `1581F574` | Agras T20P | Agras T20P | `1581F574000000000000-1581F574ZZZZZZZZZZZZ（区间）` | T20P / T40 同号段 |
| `1581F574` | Agras T40 | Agras T40 | `1581F574000000000000-1581F574ZZZZZZZZZZZZ（区间）` |  |
| `1581F6BU` | Agras T50 | Agras T50 & T25 | `1581F6BU000000000000-1581F6BUZZZZZZZZZZZZ（区间）` | 官方含 T25 别名 |
| `1581F836` | Agras T25P | Agras T25P | `1581F836000000000000-1581F836ZZZZZZZZZZZZ（区间）` |  |
| `1581F8ZL` | Agras T100 | Agras T100 | `1581F8ZL000000000000-1581F8ZLZZZZZZZZZZZZ（区间）` |  |
| `1581F8ZX` | Agras T70P | Agras T70P | `1581F8ZX000000000000-1581F8ZXZZZZZZZZZZZZ（区间）` |  |

### 物流（2 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F7C6` | FlyCart 30 | FlyCart 30 | `1581F7C6000000000000-1581F7C6ZZZZZZZZZZZZ（区间）` |  |
| `1581FAN4` | FlyCart 100 | FlyCart 100 | `1581FAN4000000000000-1581FAN4ZZZZZZZZZZZZ（区间）` |  |

### 其他厂商（35 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |
| --- | --- | --- | --- | --- |
| `1748FEV4AL` | Autel Alpha | Autel Alpha | `1748FEV4AL0000000000-1748FEV4ALZ999999999（区间）` |  |
| `1748FDF2JD2924` | Dragonfish Lite | Dragonfish Lite | `1748FDF2JD2924011089（列表公共前缀）` | 官方按单台序列号登记 |
| `1748FDF3JD3` | Dragonfish Pro | Dragonfish Pro | `1748FDF3JD3000000000-1748FDF3JD3999999999（区间）` |  |
| `1748FDF1JD1` | Dragonfish Standard | Dragonfish Standard | `1748FDF1JD1000000000-1748FDF1JD1999999999（区间）` |  |
| `1748CHL7` | EVO II | EVO II | `1748CHL7822390001-1748CHN2925529999（区间）` | 官方号段 1748CHL78… |
| `1748FEV2H` | EVO II V3 | EVO II V3 | `1748FEV2H00000000000-1748FEV2HZZ999999999（区间）` |  |
| `1748CLTC` | EVO Lite | EVO Lite | `1748CLTC822390001-1748CLTC925529999（区间）` | 与 EVO Lite+ 共用号段 |
| `1748CLT09214` | EVO Lite | EVO Lite | `1748CLT0921461018（列表公共前缀）` |  |
| `1748CLTG9214910` | EVO Lite | EVO Lite | `1748CLTG921491001（列表公共前缀）` |  |
| `1748CLTC` | EVO Lite+ | EVO Lite+ | `1748CLTC822390001-1748CLTC925529999（区间）` |  |
| `1748CNA` | EVO Nano | EVO Nano | `1748CNA0921511100（列表公共前缀）` |  |
| `1748FEC4NAC825` | EVO Nano+ | EVO Nano+ | `1748FEC4NAC825091208（列表公共前缀）` |  |
| `1748FEV3HM` | EVO Max | EVO Max | `1748FEV3HMB925461042（列表公共前缀）` |  |
| `1748FEV3HMP` | MDX-1 | MDX-1 | `1748FEV3HMP000000000-1748FEV3HMP999999999（区间）` |  |
| `1588E040452` | Anafi AI | ANAFI Ai | `1588E040452AA000001-1588E040452ZZ999999（区间）` | …452 = AI，…505 = USA |
| `1588E040505` | Anafi USA | ANAFI USA | `1588E040505AA000001-1588E040505ZZ999999（区间）` |  |
| `1588E040465` | Anafi UKR GOV | PARROT ANAFI UKR GOV | `1588E040465AA000001-1588E040465ZZ999999（区间）` |  |
| `1668F00` | Skydio 2 | 2 SDRC2V1 | `1668F000142680375727（列表公共前缀）` | 官方只有 5 位公共段 |
| `1668BS20` | Skydio 2+ | 2+ SDR35V1 | `1668BS20FB000000-1668BS20JB00ZZZZ（区间）` |  |
| `1668BE10JA00` | Skydio X2E | X2E SDR21V1 | `1668BE10JA000000-1668BE10JA00ZZZZ（区间）` |  |
| `1668BR40EA00` | Skydio X10E | X10E SR47P | `1668BR40EA0057U6（列表公共前缀）` |  |
| `1668BR70CA00UZ` | Skydio R10 | R10 SDR5PC | `1668BR70CA00UZ35（列表公共前缀）` |  |
| `1863FPEG8218` | XAG P100 | P100 | `1863FPEG821800000000-1863FPEG8218999ZZZZZ（区间）` |  |
| `1863FPEG8219` | XAG P100 Pro | P100 Pro | `1863FPEG821900000000-1863FPEG8219999ZZZZZ（区间）` |  |
| `1863FPEG8223` | XAG P150 | P150 | `1863FPEG822300000000-1863FPEG8223999ZZZZZ（区间）` |  |
| `1863F251` | XAG P150 Max | P150 Max | `1863F251000000000000-1863F251ZZZZZZZZZZZZ（区间）` |  |
| `1863F242` | XAG P60 | P60 | `1863F242000000000000-1863F242ZZZZZZZZZZZZ（区间）` |  |
| `1644FMD4100000007` | md4-1000 | md4-1000 | `1644FMD4100000007004-1644FMD4100000007504（区间）` |  |
| `1644FMD4300000001` | md4-3000 | md4-3000 | `1644FMD4300000001234-1644FMD4300000001334（区间）` |  |
| `1644FMD4E1VA0000` | EasyOne | EasyOne | `1644FMD4E1VA00000001-1644FMD4E1VA00009999（区间）` |  |
| `1587A051` | eBee X | eBee X series | `1587A0510003000（单条）` | 051000 = eBee X |
| `1587A540` | eBee TAC | eBee X series | `1587A540002APD（单条）` | 054000 = eBee TAC |
| `1701DSPR` | Percepto AirMax | Percepto AirMax | `1701DSPR00841120PR（单条）` |  |
| `1701DGAS` | Percepto AirMax OGI | AirMax OGI | `1701DGAS00000000PR-1701DGASXXXXXXXXPR（区间）` |  |
| `17444242` | WingtraOne Gen II | WingtraOne Gen II | `174442423-174449999（区间）` | 官方区间 174442423~174449999 |

另有 32 条没有官方依据的条目，已作为测试预设加入固件（接收端展示时加 `-?-`），逐条见第四、五节。

## 四、第三方草稿库逐条核对

`data/models_draft_2026-11-11.txt`（108 条）与官方序列号逐条比对。**「前缀需修正」的条目，草稿里的前缀在官方数据里找不到任何出处**，用它会生成查无此机的序列号（现已作为测试预设加入固件，接收端展示时加 `-?-`）。

| 分类 | 草稿前缀 | 草稿型号 | 官方前缀 | 官方型号 | 判定 |
| --- | --- | --- | --- | --- | --- |
| 消费级 | `11223344` | Anafi | `1588E040452、1588E040` | ANAFI Ai | 前缀需修正 |
| 消费级 | `1581F4XF` | Mini 3 Pro | `1581F6L5` | Mini 3 Pro | 前缀需修正 |
| 消费级 | `1581FANL` | Mini 5 Pro (4G) | `1581F9DE` | Mini 5 Pro | 前缀需修正 |
| 消费级 | `1748CHL2` | EVO II | `1748CH、1748CHL7` | EVO II | 前缀可截短 |
| 消费级 | `1748CLT0` | EVO Lite | `1748CLT09222856、1748CLT09214` | EVO Lite | 前缀可截短 |
| 消费级 | `1748CLTG` | EVO Lite | `1748CLTG9214910` | EVO Lite | 前缀可截短 |
| 消费级 | `1748CNA0` | EVO Nano | `1748CNA0922023、1748CNA09221118` | EVO Nano | 前缀可截短 |
| 消费级 | `1748CNAG` | EVO Nano | `1748CNA、1748CNA0922023` | EVO Nano | 前缀可截短 |
| 消费级 | `44332211` | Anafi USA | `1588E040505、1588E040` | ANAFI USA | 前缀需修正 |
| 消费级 | `P208904` | Skydio 2 / 2+ | `1668F00` | 2 SDRC2V1 | 前缀需修正 |
| 行业级 | `1748CJD1` | Dragonfish Standard | `1748FDF1JD1、1748FDF1` | Dragonfish Standard | 前缀需修正 |
| 行业级 | `1748CJD2` | Dragonfish Lite | `1748FDF2JD2924` | Dragonfish Lite | 前缀需修正 |
| 行业级 | `1748CJD3` | Dragonfish Pro | `1748FDF3JD3、1748FDF3` | Dragonfish Pro | 前缀需修正 |
| 行业级 | `SDR21V1` | Skydio X2E | `1668BE10JA00、1668BE10` | X2E SDR21V1 | 前缀需修正 |
| 行业级 | `SDR35V1` | Skydio X2 | `1668BS20` | 2+ SDR35V1 | 前缀需修正 |
| 行业级 | `SR47PCV` | Skydio X10 | `1668BR40EA00、1668BR40FA00` | X10E SR47P | 前缀需修正 |
| 植保农业 | `1758X000` | P150 Max | `1863F251` | P150 Max | 前缀需修正 |
| 植保农业 | `1758X001` | P100 Pro | `1863FPEG8219、1863FPEG` | P100 Pro | 前缀需修正 |
| 植保农业 | `1758X002` | P100 | `1863FPEG8218、1863FPEG` | P100 | 前缀需修正 |
| 电影级 | `18179001` | Alta X Gen2 | `18179132` | Alta X Gen2 | 前缀需修正 |
| 测绘级 | `1567A000` | md4-1000 | `1644FMD4100000007、1644FMD4` | md4-1000 | 前缀需修正 |
| 测绘级 | `1567A001` | md4-3000 | `1644FMD4300000001、1644FMD4` | md4-3000 | 前缀需修正 |
| 测绘级 | `1587A000` | eBee X | `1587A051、1587A540` | eBee X series | 前缀需修正 |
| 测绘级 | `1587A001` | eBee TAC | `1587A051、1587A540` | eBee X series | 前缀需修正 |
| 测绘级 | `1587A002` | eBee Geo | `1587A051、1587A540` | eBee X series | 前缀需修正 |
| 测绘级 | `1688W000` | WingtraOne Gen II | `17444、17444242` | WingtraOne Gen II | 前缀需修正 |

## 五、官方清单中不存在的型号（已加入测试预设，接收端展示带 `-?-`）

这些条目没有官方序列号依据，只用于自测发射与接收链路。固件把它们收进测试预设；
**接收端展示时**若收到的序列号命中这些前缀，会在机型名前加 `-?-`（例如 `-?-Mini 2 SE`），
发射端的机型名称输入框与预设下拉框保持原样，**不代表该机型真实存在或已通过认证**。
`11223344`、`44332211` 是肉眼可见的镜像编造号段，已剔除。

| 分类 | 草稿前缀 | 型号 | 接收端展示 | 说明 |
| --- | --- | --- | --- | --- |
| 消费级 | `1581F6CD` | Mini 2 SE | `-?-Mini 2 SE` | DJI 未登记该型号的 DoC |
| 消费级 | `1581F8PJ` | Mini 4K | `-?-Mini 4K` | DJI 未登记该型号的 DoC |
| 消费级 | `1581FB34` | Lito X1 | `-?-Lito X1` | 无任何厂商登记 |
| 消费级 | `1581FBV5` | Lito 1 | `-?-Lito 1` | 无任何厂商登记 |
| 消费级 | `1778S000` | SplashDrone 4 | `-?-SplashDrone 4` | SwellPro 未登记该型号 |
| 消费级 | `PF723000` | Rolling Spider | `-?-Rolling Spider` | Parrot 未登记 |
| 消费级 | `PF727078` | Mambo FLY | `-?-Mambo FLY` | Parrot 未登记 |
| 消费级 | `YUNQ4KPUS` | Q500 4K Typhoon | `-?-Q500 4K Typhoon` | Yuneec 全品牌未登记 |
| 穿越机/FPV | `1581F7V2` | Flip | `-?-Flip` | DJI 未登记该型号的 DoC |
| 穿越机/FPV | `1581F87L` | Neo | `-?-Neo` | DJI 未登记该型号的 DoC |
| 穿越机/FPV | `1581FA6Q` | Neo 2 | `-?-Neo 2` | DJI 未登记该型号的 DoC |
| 行业级 | `1701A000` | Percepto Max | `-?-Percepto Max` | 官方型号是 AirMax / OGI |
| 行业级 | `1701A001` | Percepto Sparrow | `-?-Percepto Sparrow` | 官方只有 AirMax / OGI |
| 行业级 | `1701A002` | Percepto Air | `-?-Percepto Air` | 官方只有 AirMax / OGI |
| 行业级 | `YUNH5ETWET` | H520 Hexacopter | `-?-H520 Hexacopter` | Yuneec 未登记 |
| 行业级 | `YUNH920202EU` | Tornado H920 | `-?-Tornado H920` | Yuneec 未登记 |
| 行业级 | `YUNTYH3EU` | Typhoon H3 | `-?-Typhoon H3` | Yuneec 未登记 |
| 植保农业 | `1581FAE3` | Agras T70S | `-?-Agras T70S` | DJI 只登记 T70P |
| 植保农业 | `1758X003` | V40 | `-?-V40` | XAG 无该型号（有 P60 / P100 / P150） |
| 电影级 | `18179000` | Alta X | `-?-Alta X` | Freefly 登记了型号但无序列号 |
| 电影级 | `18179002` | Alta 8 | `-?-Alta 8` | Freefly 未登记 |
| 测绘级 | `1587A003` | eBee RTK | `-?-eBee RTK` | 官方 eBee X series 不含 RTK |
| 测绘级 | `1688W001` | WingtraOne GEN I | `-?-WingtraOne GEN I` | Wingtra 只登记 Gen II / RAY |

## 六、共用一个前缀的情况

官方数据里确实存在「一个号段对应两个型号」，固件保留多条记录，选型时随机取前缀：

| 前缀 | 涉及型号 | 处理 |
| --- | --- | --- |
| `1581F446` | Agras T10 / T30 | 两条记录共用，型号名由使用者在预设里选 |
| `1581F574` | Agras T20P / T40 | 同上 |
| `1748CLTC` | Autel EVO Lite / EVO Lite+ | 官方同号段，两条记录共用 |
| `1581F4BN` / `1581F5BK` | M30 / M30T | 官方分开登记，固件也分开 |
| `1588E040…` | Parrot Anafi AI / USA / UKR GOV | 只差第 10-12 位，必须用 11 位前缀区分 |

## 七、数据怎么抓的

```
GET /api/v1/publicDOCRev?itemsPerPage=500&pageIndex=0                  # 516 条声明清单
GET /api/v1/publicDOCRev/{trackingNumber}                             # 取 publicRevision.docRevId
GET /api/v1/serialNumbers?findBy=docRevId&docRevId={id}               # 该声明登记的序列号
```

（主机名 `https://uasdoc.faa.gov`，公开接口，无需登录。
官方逐位编码说明摘录在 `data/faa_serial_notes_2026-11-11.txt`，例如 eBee 的 `051000 = eBee X /
052000 = eBee GEO / 054000 = eBee TAC`，本表拆分 eBee 型号即依据这段说明。）

注意：`publicRevision.serialNumberStart/End` 字段只对 102 条声明有值，且部分已过期
（Mini 3 Pro 的 start 字段仍是 `1581F4XF…`，序列号表里已改为 `1581F6L5…`）。
**判定一律以 `serialNumbers` 为准**，这也是官网详情页「Serial Numbers」弹窗展示的内容。

## 八、维护

```bash
python tools/models_faa.py --check     # 回归：表是否与快照一致
python tools/models_faa.py --suggest   # 看官方给某厂商推导出的前缀（可加厂商名过滤）
python tools/models_faa.py --write     # 改完 CURATED 后同步进 crid_web.c
python tools/models_faa.py --doc       # 重新生成本文档
python tools/models_faa.py --review    # 草稿库核对结果 -> data/models_reviewed_*.tsv
python tools/models_faa.py --index     # 全量前缀索引 -> data/faa_prefix_index_*.tsv
```

换快照时：重新抓一遍序列号写入 `data/faa_serial_YYYY-MM-DD.tsv`，改脚本里的 `SERIAL`/`SNAPSHOT_DATE`，
先跑 `--check` 看哪些条目失效，再决定是保留旧前缀还是随官方更新。
