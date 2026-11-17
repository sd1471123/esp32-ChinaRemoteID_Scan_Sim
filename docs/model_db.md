# 机型库可信清单（FAA UAS DoC）

> 数据源：FAA UAS Declaration of Compliance（<https://uasdoc.faa.gov>），快照 `data/faa_doc_2026-11-11.txt`（516 条声明 / 170 家厂商）。
> 本文档由 `python tools/models_faa.py --doc` 从同一份快照生成，与固件 `main/crid_web.c` 中的机型预设表共用一份数据，不要手工改表。

## 一、判定规则

一条机型记录只有在**同时满足**下面两点时才算「已核实」：

1. **前缀成立**：前缀等于官方 `serialNumberStart`，或是它的合法截短（≥7 位，仍落在官方号段内）；
2. **型号对得上**：型号名能与 FAA `model` 字段一一对应（允许 `M3E`↔`Mavic 3E` 这类官方缩写）。

不满足的，一律不进固件：官方没给号段的（Alta X、Wingtra、Dragonfish Standard）不猜；
把型号代号当序列号的（`SDR21V1`、`SR47PCV`）不算；「厂商码 + 000/001 递增」这类推断前缀不算。

## 二、结果

| 判定 | 条数 |
| --- | --- |
| ✅ 已核实，已进入固件 | **66**（63 个前缀） |
| ⚠️ 官方有型号、但前缀不成立（需修正，未进固件） | 22 |
| ❌ 官方清单无该型号（未进固件） | 21 |

## 三、已核实清单（固件当前使用的全部条目）

### 消费级（20 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F0M6` | Mavic 2 Zoom | Mavic 2 Zoom | `1581F0M6` |  |
| `1581F11V` | Phantom 4 Pro V2.0 | Phantom 4 Pro V2.0 | `1581F11V` |  |
| `1581F163` | Mavic 2 Pro | Mavic 2 Pro | `1581F163` |  |
| `1581F1WN` | Mavic Air 2 | Mavic Air 2 (MA2UE1N) | `1581F1WN` | 官方区分 MA2UE1N/MA2UE3W |
| `1581F3N3` | Mavic Air 2 | Mavic Air 2 (MA2UE3W) | `1581F3N3` |  |
| `1581F3YT` | Air 2S | Air 2S | `1581F3YT` |  |
| `1581F45T` | Mavic 3 | Mavic 3 | `1581F45T` |  |
| `1581F4QZ` | Mavic 3 Cine | Mavic 3 Cine | `1581F4QZ` |  |
| `1581F4XF` | Mini 3 Pro | Mini 3 Pro | `1581F4XF` |  |
| `1581F5YH` | Mini 3 | Mini 3 | `1581F5YH` |  |
| `1581F67P` | Mavic 3 Classic | Mavic 3 Classic | `1581F67P` |  |
| `1581F67Q` | Mavic 3 Pro | Mavic 3 Pro | `1581F67Q` |  |
| `1581F6L5` | Mini 3 Pro | Mini 3 Pro | `1581F6L5` |  |
| `1581F6MK` | Mavic 3 Pro Cine | Mavic 3 Pro Cine | `1581F6MK` |  |
| `1581F6N8` | Air 3 | Air 3 | `1581F6N8` |  |
| `1581F6Z9` | Mini 4 Pro | Mini 4 Pro | `1581F6Z9` |  |
| `1581F895` | Air 3S | Air 3S | `1581F895` |  |
| `1581F8LQ` | Mavic 4 Pro (64GB) | Mavic 4 Pro (64 GB) | `1581F8LQ` |  |
| `1581F986` | Mavic 4 Pro (512GB) | Mavic 4 Pro (512 GB) | `1581F986` |  |
| `1581F9DE` | Mini 5 Pro | Mini 5 Pro | `1581F9DE` |  |

### 穿越机（5 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F37Q` | FPV | FPV | `1581F37Q` |  |
| `1581F4QW` | Avata | AVATA | `1581F4QW` | 官方写作 AVATA |
| `1581F6W8` | Avata 2 | Avata 2 | `1581F6W8` |  |
| `1581FA8J` | Avata 360 | Avata 360 (DVN3XT) | `1581FA8J` |  |
| `1581FBLK` | Avata 360 | DJI Avata 360 (DVN3NT) | `1581FBLK` | 两个硬件版本共用显示名 |

### 行业级（21 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F0UY` | Phantom 4 RTK | Phantom 4 RTK | `1581F0UY` |  |
| `1581F1ZN` | Matrice 300 RTK | M300 RTK | `1581F1ZN` |  |
| `1581F4BN` | Matrice 30 | M30 | `1581F4BN` |  |
| `1581F4GC` | Mavic 2 Enterprise Advanced | Mavic 2 Enterprise Advanced | `1581F4GC` |  |
| `1581F4Z4` | Inspire 3 | Inspire 3 | `1581F4Z4` |  |
| `1581F5BK` | Matrice 30T | M30T | `1581F5BK` |  |
| `1581F5BL` | Matrice 30 (机场版) | M30 Dock version | `1581F5BL` |  |
| `1581F5BM` | Matrice 30T (机场版) | M30T Dock version | `1581F5BM` |  |
| `1581F5FH` | Mavic 3E | M3E | `1581F5FH` |  |
| `1581F5FJ` | Mavic 3T | M3T | `1581F5FJ` |  |
| `1581F5FK` | Mavic 3M | M3M | `1581F5FK` |  |
| `1581F62H` | Matrice 30T | M30T (EU&KR) | `1581F62H` | 欧韩版，与 M30T 同名 |
| `1581F6GK` | Matrice 350 RTK | M350 RTK | `1581F6GK` |  |
| `1581F6Q8` | Matrice 3TD | M3TD | `1581F6Q8` |  |
| `1581F6QA` | Matrice 3D | M3D | `1581F6QA` |  |
| `1581F7FV` | Matrice 4E | Matrice 4E | `1581F7FV` |  |
| `1581F7K3` | Matrice 4T | Matrice 4T | `1581F7K3` |  |
| `1581F8DB` | Matrice 400 | Matrice 400 | `1581F8DB` |  |
| `1581F8HG` | Matrice 4TD | Matrice 4TD | `1581F8HG` |  |
| `1581F8HH` | Matrice 4D | Matrice 4D | `1581F8HH` |  |
| `1581FBEX` | Matrice 3TA | M3TA | `1581FBEX` |  |

### 植保（8 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F446` | Agras T10 | Agras T10 | `1581F446` | T10/T30 共用前缀 |
| `1581F446` | Agras T30 | Agras T30 | `1581F446` |  |
| `1581F574` | Agras T20P | Agras T20P | `1581F574` | T20P/T40 共用前缀 |
| `1581F574` | Agras T40 | Agras T40 | `1581F574` |  |
| `1581F6BU` | Agras T50 | Agras T50 & T25 | `1581F6BU` | 官方含 T25 别名 |
| `1581F836` | Agras T25P | Agras T25P | `1581F836` |  |
| `1581F8ZL` | Agras T100 | Agras T100 | `1581F8ZL` |  |
| `1581F8ZX` | Agras T70P | Agras T70P | `1581F8ZX` |  |

### 物流（2 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |
| --- | --- | --- | --- | --- |
| `1581F7C6` | FlyCart 30 | FlyCart 30 | `1581F7C6` |  |
| `1581FAN4` | FlyCart 100 | FlyCart 100 | `1581FAN4` |  |

### 其他厂商（10 条）

| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |
| --- | --- | --- | --- | --- |
| `1748FEV4` | Autel Alpha | Autel Alpha | `1748FEV4ALP9` | 官方 1748FEV4ALP9 |
| `1748CJD2` | Dragonfish Lite | Dragonfish Lite | `1748CJD2822390001` | 官方 1748CJD2822390001 |
| `1748FEV3` | EVO Max | EVO Max | `1748FEV3HMA111468、1748FEV3HMB925` | 与 MDX-1 共用前缀 |
| `1748FEV3` | MDX-1 | MDX-1 | `1748FEV3HMM、1748FEV3HM` |  |
| `1748CHL7` | EVO II | EVO II | `1748CHL7822390001、1748CH` | 与 EVO II V3 前缀重叠 |
| `1748CLT0` | EVO Lite | EVO Lite | `1748CLT09222856、1748CLT09214` | 官方 1748CLT09222856 |
| `1748CLTG` | EVO Lite | EVO Lite | `1748CLTG921491、1748CLT` | 官方 1748CLTG921491 |
| `1748CLTC` | EVO Lite+ | EVO Lite+ | `1748CLTC822390001` | 官方同前缀也登记 EVO Lite |
| `1748CNA0` | EVO Nano | EVO Nano | `1748CNA09221118、1748CNA0922023` | 官方 1748CNA0922023 |
| `1588E040` | Anafi AI | ANAFI Ai | `1588E040452AA000001` | Anafi 家族公共前缀（USA 为 …505） |

## 四、需要修正的条目（官方有型号，前缀不成立）

| 分类 | 草稿前缀 | 草稿型号 | FAA 官方型号 | 官方号段 | 问题 |
| --- | --- | --- | --- | --- | --- |
| 消费级 | `11223344` | Anafi | ANAFI Ai | `1588E040452AA000001` | 8 位占位数字，非厂商码 |
| 消费级 | `44332211` | Anafi USA | ANAFI USA | `1588E040505AA000001` | 同上，且与 Anafi AI 混用 |
| 消费级 | `1581FANL` | Mini 5 Pro (4G) | Mini 5 Pro | `1581F9DE` | 官方无 4G 独立登记 |
| 消费级 | `1748FEV2` | EVO II V3 | EVO II V3 | `1748CHL7823` | 厂商码后两位写错 |
| 消费级 | `P208904` | Skydio 2 / 2+ | 2 SDRC2V1 / 2+ SDR35V1 | `1668F / 1668BS20FB` | 把型号代号当序列号前缀 |
| 行业级 | `1701A000` | Percepto Max | Percepto AirMax | `1701DSPR / 1701ASPR` | 型号名与前缀均不符 |
| 行业级 | `1748CJD1` | Dragonfish Standard | Dragonfish Standard | `无区间` | 官方只登记型号，未给序列号 |
| 行业级 | `1748CJD3` | Dragonfish Pro | Dragonfish Pro | `1748FDF3JD3924` | 前缀应为 FDF3，非 CJD3 |
| 行业级 | `SDR21V1` | Skydio X2E | X2E SDR21V1 | `1668BE10JA` | 把型号代号当序列号前缀 |
| 行业级 | `SDR35V1` | Skydio X2 | 2+ SDR35V1 | `1668BS20FB` | SDR35V1 实为 Skydio 2+，型号也错 |
| 行业级 | `SR47PCV` | Skydio X10 | X10E SR47P | `1668BR40EA` | X10 无直接登记，仅有 X10E/R10 |
| 行业级 | `1758X000` | P150 Max | P150 Max | `1863F251` | 厂商码应为 1863F，非 1758X |
| 行业级 | `1758X001` | P100 Pro | P100 Pro | `1863FPEG8219` | 同上 |
| 行业级 | `1758X002` | P100 | P100 | `1863FPEG8218` | 同上 |
| 电影级 | `18179000` | Alta X | Alta X | `无区间` | 官方只登记型号，未给序列号 |
| 电影级 | `18179001` | Alta X Gen2 | Alta X Gen2 | `无区间` | 同上 |
| 测绘级 | `1567A000` | md4-1000 | md4-1000 | `1644FMD4100000007004` | 厂商码应为 1644FMD4 |
| 测绘级 | `1567A001` | md4-3000 | md4-3000 | `1644FMD4300000001234` | 同上 |
| 测绘级 | `1587A000` | eBee X | eBee X series | `1587A051…/1587A057…` | 厂商码应为 1587A05/1587A54 |
| 测绘级 | `1587A001` | eBee TAC | eBee X series | `1587A051…` | 同上 |
| 测绘级 | `1587A002` | eBee Geo | eBee X series | `1587A051…` | 同上 |
| 测绘级 | `1688W000` | WingtraOne Gen II | WingtraOne Gen II | `无区间` | 官方只登记型号，未给序列号 |

## 五、官方清单中不存在的型号（UNVERIFIED）

这些条目只保留在文档里备查，**不进固件、不参与任何匹配**。缺失原因多为该机型未在美国登记 DoC，
不代表机器不存在，但在拿到官方依据前不应给它们编前缀。

| 分类 | 草稿前缀 | 型号 | 说明 |
| --- | --- | --- | --- |
| 消费级 | `1581F6CD` | Mini 2 SE | DJI 未在 DoC 登记该型号 |
| 消费级 | `1581F8PJ` | Mini 4K | DJI 未在 DoC 登记该型号（固件另一处写作 1581F8C8） |
| 消费级 | `1581FB34` | Lito X1 | 无任何厂商登记 |
| 消费级 | `1581FBV5` | Lito 1 | 无任何厂商登记 |
| 消费级 | `1778S000` | SplashDrone 4 | SwellPro 只登记 A-RID/FD2/FD3 |
| 消费级 | `PF723000` | Rolling Spider | Parrot 未登记 |
| 消费级 | `PF727078` | Mambo FLY | Parrot 未登记 |
| 消费级 | `YUNQ4KPUS` | Q500 4K Typhoon | Yuneec 全品牌未登记 |
| 穿越机 | `1581F7V2` | Flip | DJI 未在 DoC 登记该型号 |
| 穿越机 | `1581F87L` | Neo | DJI 未在 DoC 登记该型号 |
| 穿越机 | `1581FA6Q` | Neo 2 | DJI 未在 DoC 登记该型号 |
| 行业级 | `1701A001` | Percepto Sparrow | 官方只有 AirMax/OGI |
| 行业级 | `1701A002` | Percepto Air | 官方只有 AirMax/OGI |
| 行业级 | `YUNH5ETWET` | H520 Hexacopter | Yuneec 未登记 |
| 行业级 | `YUNH920202EU` | Tornado H920 | Yuneec 未登记 |
| 行业级 | `YUNTYH3EU` | Typhoon H3 | Yuneec 未登记 |
| 植保 | `1581FAE3` | Agras T70S | DJI 只登记 T70P |
| 植保 | `1758X003` | V40 | XAG 无该型号（有 P60） |
| 电影级 | `18179002` | Alta 8 | Freefly 未登记 |
| 测绘级 | `1587A003` | eBee RTK | 官方 eBee X series 不含 RTK |
| 测绘级 | `1688W001` | WingtraOne GEN I | Wingtra 只登记 Gen II / RAY |

## 六、共用一个前缀的情况

官方数据里确实存在「一个号段对应两个型号」，固件保留多条同名记录，选型时随机取前缀：

| 前缀 | 涉及型号 | 处理 |
| --- | --- | --- |
| `1581F446` | Agras T10 / T30 | 两条记录共用，型号名由使用者在预设里选 |
| `1581F574` | Agras T20P / T40 | 同上 |
| `1748FEV3` | Autel EVO Max / MDX-1 | 同上 |
| `1748CLTC` | Autel EVO Lite / EVO Lite+ | 官方同号段，显示名取 EVO Lite+ |
| `1748CHL7` | Autel EVO II / EVO II V3 | 官方号段重叠，取 EVO II |
| `1588E040` | Parrot Anafi AI / USA / UKR GOV | 家族公共前缀，取 Anafi AI |

## 七、为什么只认 FAA

- FAA DoC 是目前唯一**公开、可逐条复核**「型号 ↔ 序列号号段」的官方登记，其余来源都拿不出这个对应关系；
- 厂商官网与说明书只写型号，不公布 RID 序列号前缀；社区整理的机型库没有出处，出错也无法追溯——旧表里的
  `18179000`、`1587A000`、`1758X000` 这类「厂商码 + 递增序号」就是无从核对的产物；
- FAA 只覆盖在美国登记的机型，**未登记不等于不存在**：这类条目单列 UNVERIFIED，等拿到官方依据再入库，
  期间固件不为它们生成序列号，避免产出查无此机的号码。

## 八、维护

```bash
python tools/models_faa.py --check   # CI/本地回归：表是否与快照一致
python tools/models_faa.py --write   # 改了 CURATED 后同步进 crid_web.c
python tools/models_faa.py --doc     # 重新生成本文档
```

换新快照时：把新的 FAA 导出放到 `data/faa_doc_YYYY-MM-DD.txt`，改脚本里的 `FAA`/`SNAPSHOT_DATE`，
先跑 `--check` 看哪些条目失效，再决定是保留旧前缀还是随官方更新。

快照取自 `uasdoc.faa.gov` 的公开接口（`/api/v1/publicDOCRev`，无需登录），按公司分组整理成
「厂商 → 型号 → 匹配前缀」三段式，即 `data/faa_doc_2026-11-11.txt` 的格式。
